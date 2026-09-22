#include "Editor/Panels/ViewportPanel.hpp"

#include "Editor/EditorCommands.hpp"

// ImGuizmo's header names ImGui's types without including them, so Dear ImGui comes first
#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <span>

namespace Editor
{
	namespace
	{
		constexpr float k_ToolbarOffset = 8.0f;

		constexpr ImU32 k_OutlineColor = IM_COL32(255, 170, 40, 230);
		constexpr float k_OutlineThickness = 1.5f;
		constexpr int k_OutlineSegments = 48;

		// Ctrl while dragging snaps: half a metre, fifteen degrees, a quarter of the scale
		constexpr std::array<float, 3> k_TranslateSnap{ 0.5f, 0.5f, 0.5f };
		constexpr std::array<float, 3> k_RotateSnap{ 15.0f, 15.0f, 15.0f };
		constexpr std::array<float, 3> k_ScaleSnap{ 0.25f, 0.25f, 0.25f };

		// A column this short has no direction, the rotation is kept rather than read from it
		constexpr float k_MinimumAxisLength = 1e-6f;

		float SafeExtent(uint32_t extent)
		{
			return static_cast<float>(std::max(extent, 1u));
		}

		ImGuizmo::OPERATION ToImGuizmo(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Rotate:
				{
					return ImGuizmo::ROTATE;
				}
				case GizmoOperation::Scale:
				{
					return ImGuizmo::SCALE;
				}
				default:
				{
					return ImGuizmo::TRANSLATE;
				}
			}
		}

		const float* GetSnap(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Rotate:
				{
					return k_RotateSnap.data();
				}
				case GizmoOperation::Scale:
				{
					return k_ScaleSnap.data();
				}
				default:
				{
					return k_TranslateSnap.data();
				}
			}
		}

		const char* GetCommandVerb(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Rotate:
				{
					return "Rotate";
				}
				case GizmoOperation::Scale:
				{
					return "Scale";
				}
				default:
				{
					return "Move";
				}
			}
		}

		// The gizmo edits the full matrix, the transform keeps its three parts. Column lengths are the scale, each with the sign the transform already had so a mirrored entity stays mirrored, and the columns divided by them are the rotation
		void ApplyMatrix(const Engine::Math::Matrix4& matrix, Engine::TransformComponent& transform)
		{
			transform.Translation = Engine::Math::Vector3(matrix[3]);

			Engine::Math::Vector3 l_Scale = transform.Scale;
			Engine::Math::Matrix3 l_Rotation(1.0f);
			for (int i_Axis = 0; i_Axis < 3; ++i_Axis)
			{
				const Engine::Math::Vector3 l_Column = Engine::Math::Vector3(matrix[i_Axis]);
				const float l_Length = glm::length(l_Column);
				if (l_Length < k_MinimumAxisLength)
				{
					return;
				}

				l_Scale[i_Axis] = std::copysign(l_Length, transform.Scale[i_Axis]);
				l_Rotation[i_Axis] = l_Column / l_Scale[i_Axis];
			}

			transform.Scale = l_Scale;
			transform.Rotation = glm::normalize(glm::quat_cast(l_Rotation));
		}

		bool SameTransform(const Engine::TransformComponent& a, const Engine::TransformComponent& b)
		{
			return a.Translation == b.Translation && a.Rotation == b.Rotation && a.Scale == b.Scale;
		}

		// A toolbar button that reads as pressed while its choice is the current one
		bool ToolbarButton(const char* label, bool selected, const char* tooltip)
		{
			if (selected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}

			const bool l_Pressed = ImGui::SmallButton(label);

			if (selected)
			{
				ImGui::PopStyleColor();
			}

			ImGui::SetItemTooltip("%s", tooltip);

			return l_Pressed;
		}
	}

	void ViewportPanel::Draw(uint64_t textureId, bool mouseCaptured, const Engine::Camera& camera, Engine::Scene& scene, const Engine::AssetManager& assets, Engine::EntityId selectedEntity, EditorCommandHistory& history)
	{
		const ImGuiIO& l_IO = ImGui::GetIO();

		// Once per frame, before any gizmo: hands over the hover it accumulated during the previous frame
		ImGuizmo::BeginFrame();

		// No padding, the image fills the panel edge to edge. NoNavInputs keeps keyboard navigation off while the viewport is focused, so WantCaptureKeyboard only reports a text field or a modal and the movement keys can reach the camera
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool l_Open = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoCollapse);
		ImGui::PopStyleVar();

		bool l_Hovered = false;
		bool l_Focused = false;
		bool l_GizmoBlocks = false;
		bool l_GizmoDrawn = false;

		uint32_t l_Width = 0;
		uint32_t l_Height = 0;

		m_State.ImageX = 0.0f;
		m_State.ImageY = 0.0f;
		m_State.ImageWidth = 0.0f;
		m_State.ImageHeight = 0.0f;

		if (l_Open)
		{
			const ImVec2 l_ContentMin = ImGui::GetCursorScreenPos();
			const ImVec2 l_Available = ImGui::GetContentRegionAvail();

			// Window coordinates to framebuffer pixels, the SDL3 backend reports the ratio on a high-DPI display
			l_Width = static_cast<uint32_t>(std::max(l_Available.x * l_IO.DisplayFramebufferScale.x, 0.0f));
			l_Height = static_cast<uint32_t>(std::max(l_Available.y * l_IO.DisplayFramebufferScale.y, 0.0f));

			if (textureId != 0 && l_Available.x > 0.0f && l_Available.y > 0.0f)
			{
				// The display texture is already sRGB encoded, the UI pass samples it with the backend's linear sampler
				ImGui::Image(static_cast<ImTextureID>(textureId), l_Available);

				l_Hovered = ImGui::IsItemHovered();

				// Where the image landed this frame, in the coordinates the mouse reports. The overlays below and the client's pick both convert through it
				const ImVec2 l_ImageMin = ImGui::GetItemRectMin();
				const ImVec2 l_ImageSize = ImGui::GetItemRectSize();
				m_State.ImageX = l_ImageMin.x;
				m_State.ImageY = l_ImageMin.y;
				m_State.ImageWidth = l_ImageSize.x;
				m_State.ImageHeight = l_ImageSize.y;

				// The overlays sit over the image in the UI pass and never touch the scene's radiance history. The outline first, the gizmo draws over it, neither while the frozen cursor of the look gesture could hit a handle
				Engine::Entity* l_Selected = scene.FindEntity(selectedEntity);
				if (l_Selected != nullptr)
				{
					const Engine::Camera l_ViewCamera = GetViewCamera(camera);

					DrawSelectionOutline(l_ViewCamera, assets, *l_Selected);

					if (m_GizmoOperation != GizmoOperation::None && !mouseCaptured)
					{
						l_GizmoBlocks = DrawGizmo(l_ViewCamera, scene, *l_Selected, history);
						l_GizmoDrawn = true;
					}
				}
			}
			else
			{
				// Zero before the first frame, the panel is an empty rectangle that is still hoverable
				l_Hovered = ImGui::IsWindowHovered();
			}

			// The toolbar last, so its buttons take the hover over the image and the gizmo
			ImGui::SetCursorScreenPos(ImVec2(l_ContentMin.x + k_ToolbarOffset, l_ContentMin.y + k_ToolbarOffset));
			DrawToolbar();

			l_Focused = ImGui::IsWindowFocused();
		}

		// A drag whose gizmo went away under it, the selection changed or the gizmo was hidden, still ends as one command
		if (!l_GizmoDrawn)
		{
			EndGizmoEdit(scene, history);
		}

		// A pick needs the cursor on the image itself: not on a toolbar button or a gizmo handle, and with no widget elsewhere still active, so a text field mid-edit finishes its own command before the Inspector moves to another entity
		const bool l_PickReady = l_Hovered && !mouseCaptured && !l_GizmoBlocks && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

		ImGui::End();

		// Debounce: a size that arrives with the mouse up, a window resize or a dock change, applies at once. One seen during a splitter drag must hold for a few frames first, so the drag does not wait for every frame on each of its steps
		if (l_Width != m_PendingWidth || l_Height != m_PendingHeight)
		{
			m_PendingWidth = l_Width;
			m_PendingHeight = l_Height;
			m_PendingFrames = 0;
		}
		else
		{
			m_PendingFrames = std::min(m_PendingFrames + 1, k_SettleFrames);
		}

		const bool l_Dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		if (m_PendingWidth > 0 && m_PendingHeight > 0 && (!l_Dragging || m_PendingFrames >= k_SettleFrames))
		{
			// A hidden or collapsed viewport measures zero and keeps the last extent, so the view never resizes to nothing
			m_State.ContentWidth = m_PendingWidth;
			m_State.ContentHeight = m_PendingHeight;
		}

		// Ownership: the two WantCapture flags say what the UI wants, the hover and focus say whether the viewport is the part of the UI that wants it. WantCaptureMouse is true over the viewport itself, so the hover test is the finer one for the mouse. The keyboard is the camera's while the viewport is hovered or focused, unless a text field, a modal or another focused panel's keyboard navigation took it
		m_State.Hovered = l_Hovered && !mouseCaptured;
		m_State.Focused = l_Focused;
		m_State.MouseOwned = mouseCaptured || m_State.Hovered;
		m_State.KeyboardOwned = mouseCaptured || ((l_Focused || m_State.Hovered) && !l_IO.WantCaptureKeyboard);
		m_State.PickReady = l_PickReady;
		m_State.GizmoActive = l_GizmoDrawn && ImGuizmo::IsUsing();
	}

	Engine::Camera ViewportPanel::GetViewCamera(const Engine::Camera& camera) const
	{
		Engine::Camera l_Camera = camera;
		l_Camera.ViewWidth = m_State.ContentWidth;
		l_Camera.ViewHeight = m_State.ContentHeight;

		return l_Camera;
	}

	void ViewportPanel::DrawToolbar()
	{
		if (ToolbarButton("Translate", m_GizmoOperation == GizmoOperation::Translate, "Move the selection along an axis or a plane (1)"))
		{
			m_GizmoOperation = GizmoOperation::Translate;
		}

		ImGui::SameLine();

		if (ToolbarButton("Rotate", m_GizmoOperation == GizmoOperation::Rotate, "Turn the selection about an axis (2)"))
		{
			m_GizmoOperation = GizmoOperation::Rotate;
		}

		ImGui::SameLine();

		if (ToolbarButton("Scale", m_GizmoOperation == GizmoOperation::Scale, "Scale the selection along an axis, always in its own space (3)"))
		{
			m_GizmoOperation = GizmoOperation::Scale;
		}

		ImGui::SameLine();

		if (ToolbarButton("None", m_GizmoOperation == GizmoOperation::None, "Hide the gizmo, the outline stays (4)"))
		{
			m_GizmoOperation = GizmoOperation::None;
		}

		ImGui::SameLine();

		if (ToolbarButton(m_GizmoWorldSpace ? "World" : "Local", false, "The axes the gizmo moves and turns along: the world's or the selection's own"))
		{
			m_GizmoWorldSpace = !m_GizmoWorldSpace;
		}

		ImGui::SameLine();
		ImGui::TextDisabled("Ctrl snaps");
	}

	void ViewportPanel::DrawSelectionOutline(const Engine::Camera& camera, const Engine::AssetManager& assets, const Engine::Entity& entity) const
	{
		// What the renderer draws: the unit shape behind the transform and the geometry scale. An invisible entity shows nothing to outline
		Engine::Math::Vector3 l_GeometryScale{};
		if (!entity.Visible || !Engine::GetGeometryScale(entity.Geometry, l_GeometryScale))
		{
			return;
		}

		const Engine::Math::Matrix4 l_ObjectToWorld = Engine::GetLocalToWorld(entity.Transform) * glm::scale(Engine::Math::Matrix4(1.0f), l_GeometryScale);

		// View pixels to the image's screen rectangle, the same stretch the display texture gets
		const ImVec2 l_ImageMin(m_State.ImageX, m_State.ImageY);
		const ImVec2 l_ImageMax(m_State.ImageX + m_State.ImageWidth, m_State.ImageY + m_State.ImageHeight);
		const ImVec2 l_PixelToScreen(m_State.ImageWidth / SafeExtent(camera.ViewWidth), m_State.ImageHeight / SafeExtent(camera.ViewHeight));

		ImDrawList* l_DrawList = ImGui::GetWindowDrawList();
		l_DrawList->PushClipRect(l_ImageMin, l_ImageMax, true);

		// False behind the camera or outside the raster depth range, a segment with such an end is left out rather than drawn through the camera
		const auto l_Project = [&](const Engine::Math::Vector3& objectPoint, ImVec2& screen)
		{
			const Engine::Math::Vector3 l_World = Engine::Math::Vector3(l_ObjectToWorld * Engine::Math::Vector4(objectPoint, 1.0f));

			Engine::Math::Vector2 l_Pixel;
			float l_Depth = 0.0f;
			if (!Engine::ProjectPoint(camera, l_World, l_Pixel, l_Depth) || l_Depth < 0.0f || l_Depth > 1.0f)
			{
				return false;
			}

			screen = ImVec2(l_ImageMin.x + l_Pixel.x * l_PixelToScreen.x, l_ImageMin.y + l_Pixel.y * l_PixelToScreen.y);

			return true;
		};

		const auto l_DrawLoop = [&](std::span<const Engine::Math::Vector3> points)
		{
			for (size_t i_Point = 0; i_Point < points.size(); ++i_Point)
			{
				ImVec2 l_From;
				ImVec2 l_To;
				if (l_Project(points[i_Point], l_From) && l_Project(points[(i_Point + 1) % points.size()], l_To))
				{
					l_DrawList->AddLine(l_From, l_To, k_OutlineColor, k_OutlineThickness);
				}
			}
		};

		switch (entity.Geometry.Type)
		{
			case Engine::GeometryType::Sphere:
			{
				// Three great circles of the unit sphere, so a squashed sphere outlines as the ellipsoid it renders as
				std::array<Engine::Math::Vector3, k_OutlineSegments> l_Ring{};
				for (int i_Plane = 0; i_Plane < 3; ++i_Plane)
				{
					for (int i_Segment = 0; i_Segment < k_OutlineSegments; ++i_Segment)
					{
						const float l_Angle = Engine::Math::k_TwoPi * static_cast<float>(i_Segment) / static_cast<float>(k_OutlineSegments);
						const float l_Cos = std::cos(l_Angle);
						const float l_Sin = std::sin(l_Angle);

						l_Ring[static_cast<size_t>(i_Segment)] = i_Plane == 0 ? Engine::Math::Vector3(l_Cos, l_Sin, 0.0f) : (i_Plane == 1 ? Engine::Math::Vector3(0.0f, l_Cos, l_Sin) : Engine::Math::Vector3(l_Cos, 0.0f, l_Sin));
					}

					l_DrawLoop(l_Ring);
				}
				break;
			}
			case Engine::GeometryType::Quad:
			{
				constexpr std::array<Engine::Math::Vector3, 4> k_Corners
				{
					Engine::Math::Vector3(-0.5f, -0.5f, 0.0f),
					Engine::Math::Vector3(0.5f, -0.5f, 0.0f),
					Engine::Math::Vector3(0.5f, 0.5f, 0.0f),
					Engine::Math::Vector3(-0.5f, 0.5f, 0.0f),
				};

				l_DrawLoop(k_Corners);
				break;
			}
			case Engine::GeometryType::Mesh:
			{
				// The object-space bounds as a box: a loop around the bottom, one around the top and the four uprights between them. A mesh the assets do not hold renders nothing and outlines nothing
				const Engine::Mesh* l_Mesh = assets.FindMesh(entity.Geometry.Mesh);
				if (l_Mesh == nullptr)
				{
					break;
				}

				const Engine::Math::Vector3& l_Min = l_Mesh->Bounds.Min;
				const Engine::Math::Vector3& l_Max = l_Mesh->Bounds.Max;

				const std::array<Engine::Math::Vector3, 4> l_Bottom
				{
					Engine::Math::Vector3(l_Min.x, l_Min.y, l_Min.z),
					Engine::Math::Vector3(l_Max.x, l_Min.y, l_Min.z),
					Engine::Math::Vector3(l_Max.x, l_Min.y, l_Max.z),
					Engine::Math::Vector3(l_Min.x, l_Min.y, l_Max.z),
				};

				const std::array<Engine::Math::Vector3, 4> l_Top
				{
					Engine::Math::Vector3(l_Min.x, l_Max.y, l_Min.z),
					Engine::Math::Vector3(l_Max.x, l_Max.y, l_Min.z),
					Engine::Math::Vector3(l_Max.x, l_Max.y, l_Max.z),
					Engine::Math::Vector3(l_Min.x, l_Max.y, l_Max.z),
				};

				l_DrawLoop(l_Bottom);
				l_DrawLoop(l_Top);

				for (size_t i_Corner = 0; i_Corner < l_Bottom.size(); ++i_Corner)
				{
					ImVec2 l_From;
					ImVec2 l_To;
					if (l_Project(l_Bottom[i_Corner], l_From) && l_Project(l_Top[i_Corner], l_To))
					{
						l_DrawList->AddLine(l_From, l_To, k_OutlineColor, k_OutlineThickness);
					}
				}
				break;
			}
			default:
			{
				break;
			}
		}

		l_DrawList->PopClipRect();
	}

	bool ViewportPanel::DrawGizmo(const Engine::Camera& camera, Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history)
	{
		// A new selection starts with a fresh snapshot, a drag cannot span two entities
		if (entity.Id != m_GizmoEntity)
		{
			m_GizmoEntity = entity.Id;
			m_GizmoEditing = false;
		}

		// ImGuizmo still reports the previous frame's drag here, a release clears it inside Manipulate below. So the snapshot is retaken on every frame without a drag and frozen from the press on
		if (!ImGuizmo::IsUsing())
		{
			m_GizmoBefore = entity;
			m_GizmoBeforeOperation = m_GizmoOperation;
			m_GizmoEditing = false;
		}

		// The gizmo shows the transform alone, the geometry scale stays the Inspector's
		Engine::Math::Matrix4 l_Model = Engine::GetLocalToWorld(entity.Transform);
		const Engine::Math::Matrix4 l_View = Engine::GetViewMatrix(camera);

		// ImGuizmo maps Y-up clip space to the screen itself, so the camera's Vulkan flip is undone here and nowhere else
		Engine::Math::Matrix4 l_Projection = Engine::GetProjectionMatrix(camera);
		l_Projection[1][1] *= -1.0f;

		// Into this window's draw list over the image, hit-tested and clipped to the image rectangle
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(m_State.ImageX, m_State.ImageY, m_State.ImageWidth, m_State.ImageHeight);

		const ImGuizmo::MODE l_Mode = m_GizmoWorldSpace ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
		const float* l_Snap = ImGui::GetIO().KeyCtrl ? GetSnap(m_GizmoOperation) : nullptr;

		if (ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), ToImGuizmo(m_GizmoOperation), l_Mode, glm::value_ptr(l_Model), nullptr, l_Snap))
		{
			// Only a frame that moved something marks the scene, so the image keeps accumulating while the handle is held still
			Engine::TransformComponent l_Transform = entity.Transform;
			ApplyMatrix(l_Model, l_Transform);

			if (!SameTransform(l_Transform, entity.Transform))
			{
				entity.Transform = l_Transform;
				scene.MarkRadianceChanged();
				m_GizmoEditing = true;
			}
		}

		// The drag ended this frame: one command from the snapshot
		if (!ImGuizmo::IsUsing())
		{
			EndGizmoEdit(scene, history);
		}

		return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
	}

	void ViewportPanel::EndGizmoEdit(Engine::Scene& scene, EditorCommandHistory& history)
	{
		if (!m_GizmoEditing)
		{
			return;
		}

		m_GizmoEditing = false;

		// Recorded rather than executed, the entity already holds the after state and the tracer saw every frame of it
		const Engine::Entity* l_Entity = scene.FindEntity(m_GizmoEntity);
		if (l_Entity != nullptr)
		{
			history.Record(std::make_unique<EditEntityCommand>(std::format("{} '{}'", GetCommandVerb(m_GizmoBeforeOperation), m_GizmoBefore.Name), m_GizmoBefore, *l_Entity));
		}
	}
}