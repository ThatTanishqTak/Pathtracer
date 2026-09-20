#include "Editor/EditorClient.hpp"

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder, for the first-run layout only

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

namespace Editor
{
	EditorClient::EditorClient(EditorOptions options) : m_Options(std::move(options))
	{

	}

	void EditorClient::OnStart(Engine::ApplicationServices& services)
	{
		m_Services = &services;

		PT_APP_INFO("Editor client started, {}x{} window, {}x{} framebuffer, UI {}", m_Services->GetWindowWidth(), m_Services->GetWindowHeight(), m_Services->GetFramebufferWidth(), m_Services->GetFramebufferHeight(), m_Services->IsUIEnabled() ? "enabled" : "disabled");
		PT_APP_INFO("Viewport: hold the right mouse button over it to look around, W A S D fly, Q and E move down and up, Shift is faster, the keys work while the cursor is over the viewport or it has the focus");
		PT_APP_INFO("Panels: Scene lists the entities and selects one, Inspector shows the selection, Render Settings edits the mode, bounce limit, render scale, seed and exposure");

		if (!m_Services->IsUIEnabled())
		{
			PT_APP_WARN("The UI is disabled, the Editor has no panels and no viewport without it");
		}

		// Content is anchored at the executable, not at wherever the process was started from, so a shortcut or a debugger with another working directory finds the same files
		if (m_Options.AssetRoot.empty())
		{
			m_AssetRoot = m_Services->GetExecutableDirectory() / "Assets";
		}
		else
		{
			std::error_code l_Error;
			m_AssetRoot = std::filesystem::absolute(m_Options.AssetRoot, l_Error);
			if (l_Error)
			{
				PT_APP_WARN("Cannot resolve the asset root {}: {}, using it as given", m_Options.AssetRoot.string(), l_Error.message());
				m_AssetRoot = m_Options.AssetRoot;
			}
		}

		PT_APP_INFO("Asset root: {}", m_AssetRoot.string());

		// A scene from the command line, or an empty scene when there is none or it fails. The Editor builds no demo content, that is what the scene file and Step 11 are for
		if (m_Options.ScenePath.empty() || !LoadScene(ResolveScenePath(m_Options.ScenePath)))
		{
			if (!m_Options.ScenePath.empty())
			{
				PT_APP_ERROR("Starting with an empty scene instead of the file that failed");
			}

			BuildEmptyScene();
		}

		m_RenderSettings.Integrator.SamplesPerFrame = 1;
		m_RenderSettings.Integrator.MaxBounces = 4;
		m_RenderSettings.Integrator.Seed = 0;

		// The camera starts where the Sandbox would, reading the spawn once. From here on it is workspace state and the spawn is scene content, neither follows the other
		m_Camera.SetVerticalFieldOfView(k_VerticalFieldOfView);
		m_Camera.Reset(m_Scene.GetPlayerSpawn().Position, m_Scene.GetPlayerSpawn().Orientation);
	}

	void EditorClient::OnStop() noexcept
	{
		PT_APP_TRACE("Editor client stopped");

		m_Services = nullptr;
	}

	void EditorClient::OnEvent(const Engine::InputEvent& event)
	{
		// The UI backend sees the raw events through the host's hook and the camera reads the input snapshot in Update, so only the events worth a trace are handled here
		switch (event.Type)
		{
			case Engine::InputEventType::FocusLost:
			{
				// The host already released held keys and mouse capture before this arrives
				PT_APP_TRACE("Focus lost");
				break;
			}
			case Engine::InputEventType::WindowResized:
			{
				PT_APP_TRACE("Framebuffer resized to {}x{}", static_cast<int>(event.X), static_cast<int>(event.Y));
				break;
			}
			default:
			{
				break;
			}
		}
	}

	void EditorClient::BuildUI()
	{
		ImGuiIO& l_IO = ImGui::GetIO();

		// While the viewport holds the mouse in relative mode the cursor position is frozen, NoMouse keeps that position from hovering or clicking a panel. The next NewFrame reads the flag, one frame late is fine because the position does not move while captured
		const bool l_Captured = m_Services->IsMouseCaptured();
		if (l_Captured)
		{
			l_IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
		else
		{
			l_IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}

		// Docking inside the one native window, the panels dock into this space and the swapchain is cleared underneath
		const ImGuiID l_DockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

		if (!m_LayoutChecked)
		{
			m_LayoutChecked = true;

			// Settings were loaded by the first NewFrame. A Viewport entry means a layout file exists and wins, only a fresh workspace gets the default arrangement
			if (ImGui::FindWindowSettingsByID(ImHashStr("Viewport")) == nullptr)
			{
				BuildDefaultLayout(l_DockspaceId);
			}
		}

		// The texture id is the previous frame's, the retired display texture keeps it valid across a resize
		m_ViewportPanel.Draw(m_Services->GetViewTextureId(), l_Captured);
		m_SceneHierarchyPanel.Draw(m_Scene, m_SelectedEntity);
		m_InspectorPanel.Draw(m_Scene, m_SelectedEntity);

		const Engine::RenderRequest l_Request = GetRenderRequest();
		m_RenderSettingsPanel.Draw(m_RenderSettings, l_Request.View.Width, l_Request.View.Height);
	}

	void EditorClient::BuildDefaultLayout(uint32_t dockspaceId)
	{
		// The node DockSpaceOverViewport created this frame is rebuilt: the viewport takes the centre, the scene list the left, the inspector and the render settings share the right
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

		ImGuiID l_Centre = dockspaceId;
		ImGuiID l_Left = 0;
		ImGuiID l_Right = 0;
		ImGuiID l_RightBottom = 0;

		ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Left, 0.2f, &l_Left, &l_Centre);
		ImGui::DockBuilderSplitNode(l_Centre, ImGuiDir_Right, 0.25f, &l_Right, &l_Centre);
		ImGui::DockBuilderSplitNode(l_Right, ImGuiDir_Down, 0.35f, &l_RightBottom, &l_Right);

		ImGui::DockBuilderDockWindow("Scene", l_Left);
		ImGui::DockBuilderDockWindow("Inspector", l_Right);
		ImGui::DockBuilderDockWindow("Render Settings", l_RightBottom);
		ImGui::DockBuilderDockWindow("Viewport", l_Centre);
		ImGui::DockBuilderFinish(dockspaceId);

		PT_APP_INFO("Default panel layout built, the layout file records every change from here on");
	}

	void EditorClient::Update(const Engine::FrameTime& time, const Engine::InputState& input)
	{
		const ViewportState& l_Viewport = m_ViewportPanel.GetState();

		// The snapshot says what the frame started with. SetMouseCaptured updates the same state at once, so the look gate is taken before the gesture runs: the press frame's cursor travel is never a turn, and the release frame's relative motion still is
		const bool l_CapturedAtStart = input.MouseCaptured;

		// The look gesture: a right press over the viewport takes the mouse into relative mode through the services, letting go of the button gives it back. The host drops it on focus loss by itself, Escape is the second way out
		if (l_CapturedAtStart)
		{
			if (!input.IsMouseButtonDown(Engine::MouseButton::Right) || input.WasKeyPressed(Engine::Key::Escape))
			{
				m_Services->SetMouseCaptured(false);
			}
		}
		else if (l_Viewport.Hovered && input.WasMouseButtonPressed(Engine::MouseButton::Right))
		{
			m_Services->SetMouseCaptured(true);
		}

		// Look while captured, fly while the viewport owns the keyboard as BuildUI decided. Panel interaction reaches neither
		m_Camera.Update(time, input, l_CapturedAtStart, l_Viewport.KeyboardOwned);

		m_StatisticsElapsed += time.ElapsedSeconds;
		m_StatisticsFrames += 1;

		if (m_StatisticsElapsed >= 1.0f)
		{
			const float l_AverageMilliseconds = (m_StatisticsElapsed / static_cast<float>(m_StatisticsFrames)) * 1000.0f;

			const Engine::RenderRequest l_Request = GetRenderRequest();

			const Engine::Camera& l_Camera = m_Camera.GetCamera();
			const Engine::YawPitch& l_Angles = m_Camera.GetAngles();

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, viewport hovered {} focused {}, camera ({:.2f}, {:.2f}, {:.2f}) yaw {:.1f} pitch {:.1f}, view {}x{} at scale {:.2f}, {} bounces, {} entities, {} materials, scene revision {}, selected {}", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, l_Viewport.Hovered, l_Viewport.Focused, l_Camera.Position.x, l_Camera.Position.y, l_Camera.Position.z, Engine::Math::ToDegrees(l_Angles.Yaw), Engine::Math::ToDegrees(l_Angles.Pitch), l_Request.View.Width, l_Request.View.Height, m_RenderSettings.RenderScale, m_RenderSettings.Integrator.MaxBounces, m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), m_Scene.GetRadianceRevision(), std::to_underlying(m_SelectedEntity));

			m_StatisticsElapsed = 0.0f;
			m_StatisticsFrames = 0;
		}
	}

	std::filesystem::path EditorClient::ResolveScenePath(const std::filesystem::path& path) const
	{
		if (path.is_absolute())
		{
			return path;
		}

		return m_AssetRoot / path;
	}

	bool EditorClient::LoadScene(const std::filesystem::path& path)
	{
		// The serializer already logged every warning and the error with its field context, only the outcome is repeated here
		const Engine::SceneFileResult l_Result = Engine::SceneSerializer::Load(path, m_Scene);
		if (!l_Result.Succeeded)
		{
			PT_APP_ERROR("Scene load failed, the current scene is unchanged: {}", l_Result.Error);

			return false;
		}

		m_ScenePath = path;

		// The Ids in the file replace the ones that were selected, so the selection starts over at the first entity
		m_SelectedEntity = m_Scene.GetEntities().empty() ? Engine::EntityId::Invalid : m_Scene.GetEntities().front().Id;

		PT_APP_INFO("Loaded scene '{}' from {}: {} entities, {} materials, {} warning(s), revision {}", m_Scene.GetName(), path.string(), m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), l_Result.Warnings.size(), m_Scene.GetRadianceRevision());

		return true;
	}

	void EditorClient::BuildEmptyScene()
	{
		// A fresh scene carries a new revision, so the renderer drops whatever it had extracted. The default spawn and environment are the Scene's own
		m_Scene = Engine::Scene{};
		m_Scene.SetName("Untitled");

		m_ScenePath.clear();
		m_SelectedEntity = Engine::EntityId::Invalid;

		PT_APP_INFO("Empty scene '{}' ready, revision {}", m_Scene.GetName(), m_Scene.GetRadianceRevision());
	}

	Engine::RenderRequest EditorClient::GetRenderRequest() const
	{
		const ViewportState& l_Viewport = m_ViewportPanel.GetState();

		// The view is the viewport's content at the render scale, never the window: BuildUI measured it this frame, so the images resize in this Render and the aspect follows the panel. Before the first layout the content is zero and the view is one pixel
		const float l_Scale = std::clamp(m_RenderSettings.RenderScale, 0.05f, 1.0f);

		Engine::RenderRequest l_Request;
		l_Request.View.Width = std::max(static_cast<uint32_t>(static_cast<float>(l_Viewport.ContentWidth) * l_Scale), 1u);
		l_Request.View.Height = std::max(static_cast<uint32_t>(static_cast<float>(l_Viewport.ContentHeight) * l_Scale), 1u);
		l_Request.View.ActiveCamera = m_Camera.GetCamera();
		l_Request.View.Settings = m_RenderSettings.Integrator;
		l_Request.View.Mode = m_RenderSettings.Mode;
		l_Request.ActiveScene = &m_Scene;
		l_Request.Exposure = std::exp2(m_RenderSettings.ExposureStops);

		// The UI shows the view through its texture id, the swapchain is cleared under the panels instead of taking the fullscreen pass
		l_Request.DrawViewToWindow = false;

		return l_Request;
	}
}