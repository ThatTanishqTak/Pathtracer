#include "Sandbox/SandboxClient.hpp"

#include "Sandbox/CameraChecks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace Sandbox
{
	namespace
	{
		const char* ModeName(Engine::DiagnosticMode mode)
		{
			switch (mode)
			{
				case Engine::DiagnosticMode::RayDirection:
				{
					return "ray direction";
				}
				case Engine::DiagnosticMode::Hit:
				{
					return "hit";
				}
				case Engine::DiagnosticMode::Normal:
				{
					return "normal";
				}
				case Engine::DiagnosticMode::Distance:
				{
					return "distance";
				}
				case Engine::DiagnosticMode::BaseColor:
				{
					return "base colour";
				}
				case Engine::DiagnosticMode::PathTraced:
				{
					return "path traced";
				}
				default:
				{
					return "unknown";
				}
			}
		}

		// Linear base colours the recolour key cycles through
		constexpr std::array<Engine::Math::Vector3, 6> k_Palette
		{ {
			{ 0.8f, 0.2f, 0.2f },
			{ 0.2f, 0.8f, 0.2f },
			{ 0.2f, 0.3f, 0.9f },
			{ 0.9f, 0.8f, 0.2f },
			{ 0.8f, 0.3f, 0.8f },
			{ 0.8f, 0.8f, 0.8f },
		} };
	}

	void SandboxClient::OnStart(Engine::ApplicationServices& services)
	{
		m_Services = &services;

		PT_APP_INFO("Sandbox client started, {}x{} window, {}x{} framebuffer", m_Services->GetWindowWidth(), m_Services->GetWindowHeight(), m_Services->GetFramebufferWidth(), m_Services->GetFramebufferHeight());
		PT_APP_INFO("Controls: click captures the mouse and looks around, W A S D walk, Shift runs, Escape releases the mouse and closes when it is already released, standing still lets the image converge");
		PT_APP_INFO("Render: 1-5 select a diagnostic view, 6 the path tracer, B cycles the bounce limit, R cycles the render scale, Equals and Minus step exposure");
		PT_APP_INFO("Scene: N creates a sphere, Backspace deletes the selected entity, Period selects the next entity, arrows move the selection on X and Z, PageUp and PageDown on Y, C recolours its material");

		if (!RunCameraChecks())
		{
			PT_APP_WARN("Camera checks failed, the diagnostic modes may not match the CPU camera");
		}

		BuildDemoScene();

		m_Settings.SamplesPerFrame = 1;
		m_Settings.MaxBounces = k_BounceLimits[m_BounceLimitIndex];
		m_Settings.Seed = 0;

		// The player spawn is scene content, the controller reads it once and owns the camera from here on
		m_Controller.SetVerticalFieldOfView(k_VerticalFieldOfView);
		m_Controller.Reset(m_Scene.GetPlayerSpawn());
	}

	void SandboxClient::OnStop() noexcept
	{
		PT_APP_TRACE("Sandbox client stopped");

		m_Services = nullptr;
	}

	void SandboxClient::OnEvent(const Engine::InputEvent& event)
	{
		switch (event.Type)
		{
			case Engine::InputEventType::KeyPressed:
			{
				if (!event.Repeat)
				{
					PT_APP_TRACE("Key pressed: {}", static_cast<int>(event.KeyCode));
				}
				break;
			}
			case Engine::InputEventType::KeyReleased:
			{
				PT_APP_TRACE("Key released: {}", static_cast<int>(event.KeyCode));
				break;
			}
			case Engine::InputEventType::FocusGained:
			{
				PT_APP_TRACE("Focus gained");
				break;
			}
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

	void SandboxClient::Update(const Engine::FrameTime& time, const Engine::InputState& input)
	{
		// Escape while captured belongs to the controller, which releases the mouse, so the same press never also closes
		if (input.WasKeyPressed(Engine::Key::Escape) && !input.MouseCaptured)
		{
			m_Services->RequestClose();
		}

		// Number keys select the view, the values line up with DiagnosticMode and 6 is the path tracer
		const Engine::Key k_ModeKeys[] = { Engine::Key::Number1, Engine::Key::Number2, Engine::Key::Number3, Engine::Key::Number4, Engine::Key::Number5, Engine::Key::Number6 };
		for (uint32_t i_Mode = 0; i_Mode < 6; ++i_Mode)
		{
			if (input.WasKeyPressed(k_ModeKeys[i_Mode]))
			{
				m_Mode = static_cast<Engine::DiagnosticMode>(i_Mode);

				PT_APP_INFO("Render mode {}: {}", i_Mode + 1, ModeName(m_Mode));
			}
		}

		// A new bounce limit restarts the accumulation, a new render scale recreates the view images
		if (input.WasKeyPressed(Engine::Key::B))
		{
			m_BounceLimitIndex = (m_BounceLimitIndex + 1) % k_BounceLimitCount;
			m_Settings.MaxBounces = k_BounceLimits[m_BounceLimitIndex];

			PT_APP_INFO("Bounce limit {}", m_Settings.MaxBounces);
		}

		if (input.WasKeyPressed(Engine::Key::R))
		{
			m_RenderScaleIndex = (m_RenderScaleIndex + 1) % k_RenderScaleCount;

			PT_APP_INFO("Render scale {:.2f}", k_RenderScales[m_RenderScaleIndex]);
		}

		if (input.WasKeyPressed(Engine::Key::Equals) || input.WasKeyPressed(Engine::Key::Minus))
		{
			const float l_Direction = input.WasKeyPressed(Engine::Key::Equals) ? 1.0f : -1.0f;
			m_ExposureStops = std::clamp(m_ExposureStops + l_Direction * k_ExposureStepStops, -k_ExposureRangeStops, k_ExposureRangeStops);

			PT_APP_INFO("Exposure {:+.1f} stops ({:.3f}x)", m_ExposureStops, std::exp2(m_ExposureStops));
		}

		// Scene edits, every one goes through the Scene so the renderer sees a new radiance revision
		if (input.WasKeyPressed(Engine::Key::N))
		{
			CreateSphere();
		}

		if (input.WasKeyPressed(Engine::Key::Backspace))
		{
			DestroySelected();
		}

		if (input.WasKeyPressed(Engine::Key::Period))
		{
			SelectNext();
		}

		if (input.WasKeyPressed(Engine::Key::C))
		{
			RecolorSelected();
		}

		// Up moves away from the default camera, along -Z
		if (input.WasKeyPressed(Engine::Key::Left) || input.WasKeyPressed(Engine::Key::Right) || input.WasKeyPressed(Engine::Key::Up) || input.WasKeyPressed(Engine::Key::Down) || input.WasKeyPressed(Engine::Key::PageUp) || input.WasKeyPressed(Engine::Key::PageDown))
		{
			Engine::Math::Vector3 l_Delta(0.0f);
			l_Delta.x += input.WasKeyPressed(Engine::Key::Right) ? k_MoveStep : 0.0f;
			l_Delta.x -= input.WasKeyPressed(Engine::Key::Left) ? k_MoveStep : 0.0f;
			l_Delta.y += input.WasKeyPressed(Engine::Key::PageUp) ? k_MoveStep : 0.0f;
			l_Delta.y -= input.WasKeyPressed(Engine::Key::PageDown) ? k_MoveStep : 0.0f;
			l_Delta.z -= input.WasKeyPressed(Engine::Key::Up) ? k_MoveStep : 0.0f;
			l_Delta.z += input.WasKeyPressed(Engine::Key::Down) ? k_MoveStep : 0.0f;

			MoveSelected(l_Delta);
		}

		// Look and walk from this frame's snapshot, every camera change restarts the accumulation through the render view key
		m_Controller.Update(time, input, *m_Services);

		m_StatisticsElapsed += time.ElapsedSeconds;
		m_StatisticsFrames += 1;
		m_StatisticsMouseDeltaX += input.MouseDeltaX;
		m_StatisticsMouseDeltaY += input.MouseDeltaY;

		if (m_StatisticsElapsed >= 1.0f)
		{
			const float l_AverageMilliseconds = (m_StatisticsElapsed / static_cast<float>(m_StatisticsFrames)) * 1000.0f;

			const Engine::RenderRequest l_Request = GetRenderRequest();

			const Engine::Camera& l_Camera = m_Controller.GetCamera();
			const Engine::YawPitch& l_Angles = m_Controller.GetAngles();

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, mouse delta ({:.1f}, {:.1f}), mode {}, camera ({:.2f}, {:.2f}, {:.2f}) yaw {:.1f} pitch {:.1f}, view {}x{} at scale {:.2f}, {} bounces, {} entities, {} materials, scene revision {}, selected {}", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, m_StatisticsMouseDeltaX, m_StatisticsMouseDeltaY, ModeName(m_Mode), l_Camera.Position.x, l_Camera.Position.y, l_Camera.Position.z, Engine::Math::ToDegrees(l_Angles.Yaw), Engine::Math::ToDegrees(l_Angles.Pitch), l_Request.View.Width, l_Request.View.Height, k_RenderScales[m_RenderScaleIndex], m_Settings.MaxBounces, m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), m_Scene.GetRadianceRevision(), std::to_underlying(m_SelectedEntity));

			m_StatisticsElapsed = 0.0f;
			m_StatisticsFrames = 0;
			m_StatisticsMouseDeltaX = 0.0f;
			m_StatisticsMouseDeltaY = 0.0f;
		}
	}

	void SandboxClient::BuildDemoScene()
	{
		m_Scene = Engine::Scene{};

		// Materials first, references are captured by Id because the next Create invalidates the reference
		Engine::MaterialId l_FloorMaterial;
		{
			Engine::Material& l_Material = m_Scene.CreateMaterial("Floor");
			l_Material.BaseColor = Engine::Math::Vector3(0.7f, 0.7f, 0.7f);
			l_FloorMaterial = l_Material.Id;
		}

		Engine::MaterialId l_RedMaterial;
		{
			Engine::Material& l_Material = m_Scene.CreateMaterial("Red");
			l_Material.BaseColor = k_Palette[0];
			l_RedMaterial = l_Material.Id;
		}

		Engine::MaterialId l_GreenMaterial;
		{
			Engine::Material& l_Material = m_Scene.CreateMaterial("Green");
			l_Material.BaseColor = k_Palette[1];
			l_GreenMaterial = l_Material.Id;
		}

		// The area light: emissive geometry, the emitted radiance lives on the material and nowhere else
		Engine::MaterialId l_LightMaterial;
		{
			Engine::Material& l_Material = m_Scene.CreateMaterial("Light");
			l_Material.Type = Engine::MaterialType::Emissive;
			l_Material.BaseColor = Engine::Math::Vector3(0.0f, 0.0f, 0.0f);
			l_Material.EmissionColor = Engine::Math::Vector3(1.0f, 0.95f, 0.9f);
			l_Material.EmissionStrength = 8.0f;
			l_LightMaterial = l_Material.Id;
		}

		// A quad's normal is object +Z, rotating -90 degrees about X turns it up
		{
			Engine::Entity& l_Entity = m_Scene.CreateEntity("Floor");
			l_Entity.Geometry.Type = Engine::GeometryType::Quad;
			l_Entity.Geometry.Width = 10.0f;
			l_Entity.Geometry.Height = 10.0f;
			l_Entity.Transform.Translation = Engine::Math::Vector3(0.0f, -1.0f, 0.0f);
			l_Entity.Transform.Rotation = glm::angleAxis(-Engine::Math::k_HalfPi, Engine::Math::k_Right);
			l_Entity.Material = l_FloorMaterial;
		}

		// The unit sphere at the origin the Step 5 camera checks and the orbit are written against
		{
			Engine::Entity& l_Entity = m_Scene.CreateEntity("Centre sphere");
			l_Entity.Geometry.Type = Engine::GeometryType::Sphere;
			l_Entity.Geometry.Radius = 1.0f;
			l_Entity.Material = l_RedMaterial;
			m_SelectedEntity = l_Entity.Id;
		}

		// Non-uniform scale on purpose, the object-space intersection must show a squashed sphere, not a round one
		{
			Engine::Entity& l_Entity = m_Scene.CreateEntity("Side sphere");
			l_Entity.Geometry.Type = Engine::GeometryType::Sphere;
			l_Entity.Geometry.Radius = 0.5f;
			l_Entity.Transform.Translation = Engine::Math::Vector3(-2.0f, -0.5f, 0.0f);
			l_Entity.Transform.Scale = Engine::Math::Vector3(1.0f, 0.6f, 1.0f);
			l_Entity.Material = l_GreenMaterial;
		}

		// Faces down, +90 degrees about X turns object +Z into -Y
		{
			Engine::Entity& l_Entity = m_Scene.CreateEntity("Ceiling light");
			l_Entity.Geometry.Type = Engine::GeometryType::Quad;
			l_Entity.Geometry.Width = 2.0f;
			l_Entity.Geometry.Height = 2.0f;
			l_Entity.Transform.Translation = Engine::Math::Vector3(0.0f, 3.0f, 0.0f);
			l_Entity.Transform.Rotation = glm::angleAxis(Engine::Math::k_HalfPi, Engine::Math::k_Right);
			l_Entity.Material = l_LightMaterial;
		}

		// Scene content, not workspace state: the first-person controller starts here and the Editor camera never writes it
		m_Scene.GetPlayerSpawn().Position = Engine::Math::Vector3(0.0f, 0.5f, 6.0f);
		m_Scene.GetPlayerSpawn().Orientation = Engine::Math::k_IdentityRotation;
		m_Scene.GetEnvironment().Radiance = Engine::Math::Vector3(0.05f, 0.07f, 0.1f);

		// The fields above were edited through references after the creates advanced the revision, so mark once more
		m_Scene.MarkRadianceChanged();

		PT_APP_INFO("Demo scene built: {} entities, {} materials, revision {}", m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), m_Scene.GetRadianceRevision());
	}

	void SandboxClient::CreateSphere()
	{
		m_CreatedSpheres += 1;

		// Each created sphere gets its own material, so recolouring one never changes another
		Engine::MaterialId l_MaterialId;
		{
			Engine::Material& l_Material = m_Scene.CreateMaterial(std::format("Sphere {} material", m_CreatedSpheres));
			l_Material.BaseColor = k_Palette[m_CreatedSpheres % k_Palette.size()];
			l_MaterialId = l_Material.Id;
		}

		// Spread new spheres around a ring so they do not overlap the centre sphere or each other immediately
		const float l_Angle = static_cast<float>(m_CreatedSpheres) * 0.9f;

		Engine::Entity& l_Entity = m_Scene.CreateEntity(std::format("Sphere {}", m_CreatedSpheres));
		l_Entity.Geometry.Type = Engine::GeometryType::Sphere;
		l_Entity.Geometry.Radius = k_CreatedSphereRadius;
		l_Entity.Transform.Translation = Engine::Math::Vector3(k_CreatedSphereRing * std::cos(l_Angle), -1.0f + k_CreatedSphereRadius, k_CreatedSphereRing * std::sin(l_Angle));
		l_Entity.Material = l_MaterialId;

		m_SelectedEntity = l_Entity.Id;

		// The creates advanced the revision, the edits through the references did not
		m_Scene.MarkRadianceChanged();

		PT_APP_INFO("Created '{}' ({}) at ({:.2f}, {:.2f}, {:.2f}), now selected", l_Entity.Name, std::to_underlying(l_Entity.Id), l_Entity.Transform.Translation.x, l_Entity.Transform.Translation.y, l_Entity.Transform.Translation.z);
	}

	void SandboxClient::DestroySelected()
	{
		const Engine::Entity* l_Entity = m_Scene.FindEntity(m_SelectedEntity);
		if (l_Entity == nullptr)
		{
			PT_APP_WARN("Nothing is selected");

			return;
		}

		const std::string l_Name = l_Entity->Name;
		const Engine::MaterialId l_Material = l_Entity->Material;

		m_Scene.DestroyEntity(m_SelectedEntity);

		// A material nobody else uses goes with its entity
		if (l_Material != Engine::MaterialId::Invalid && m_Scene.CountMaterialUsers(l_Material) == 0)
		{
			m_Scene.DestroyMaterial(l_Material);
		}

		PT_APP_INFO("Deleted '{}' ({})", l_Name, std::to_underlying(m_SelectedEntity));

		// The Ids of the remaining entities are untouched by the erase, so the first one is selected by its Id, not by position
		m_SelectedEntity = m_Scene.GetEntities().empty() ? Engine::EntityId::Invalid : m_Scene.GetEntities().front().Id;
	}

	void SandboxClient::SelectNext()
	{
		const std::vector<Engine::Entity>& l_Entities = m_Scene.GetEntities();
		if (l_Entities.empty())
		{
			m_SelectedEntity = Engine::EntityId::Invalid;

			return;
		}

		size_t l_Next = 0;
		for (size_t i_Entity = 0; i_Entity < l_Entities.size(); ++i_Entity)
		{
			if (l_Entities[i_Entity].Id == m_SelectedEntity)
			{
				l_Next = (i_Entity + 1) % l_Entities.size();
				break;
			}
		}

		m_SelectedEntity = l_Entities[l_Next].Id;

		PT_APP_INFO("Selected '{}' ({})", l_Entities[l_Next].Name, std::to_underlying(m_SelectedEntity));
	}

	void SandboxClient::MoveSelected(const Engine::Math::Vector3& delta)
	{
		Engine::Entity* l_Entity = m_Scene.FindEntity(m_SelectedEntity);
		if (l_Entity == nullptr)
		{
			PT_APP_WARN("Nothing is selected");

			return;
		}

		l_Entity->Transform.Translation += delta;
		m_Scene.MarkRadianceChanged();

		PT_APP_INFO("Moved '{}' ({}) to ({:.2f}, {:.2f}, {:.2f})", l_Entity->Name, std::to_underlying(l_Entity->Id), l_Entity->Transform.Translation.x, l_Entity->Transform.Translation.y, l_Entity->Transform.Translation.z);
	}

	void SandboxClient::RecolorSelected()
	{
		const Engine::Entity* l_Entity = m_Scene.FindEntity(m_SelectedEntity);
		if (l_Entity == nullptr)
		{
			PT_APP_WARN("Nothing is selected");

			return;
		}

		Engine::Material* l_Material = m_Scene.FindMaterial(l_Entity->Material);
		if (l_Material == nullptr)
		{
			PT_APP_WARN("'{}' has no material to recolour", l_Entity->Name);

			return;
		}

		m_PaletteIndex = (m_PaletteIndex + 1) % static_cast<uint32_t>(k_Palette.size());
		l_Material->BaseColor = k_Palette[m_PaletteIndex];
		m_Scene.MarkRadianceChanged();

		// A shared material recolours every entity that uses it, which is the intended meaning of editing the material rather than the entity
		PT_APP_INFO("Recoloured material '{}' of '{}' to ({:.2f}, {:.2f}, {:.2f}), {} user(s)", l_Material->Name, l_Entity->Name, l_Material->BaseColor.x, l_Material->BaseColor.y, l_Material->BaseColor.z, m_Scene.CountMaterialUsers(l_Material->Id));
	}

	Engine::RenderRequest SandboxClient::GetRenderRequest() const
	{
		// The view renders at a fraction of the framebuffer, the tone map stretches it back to the window, so a lower scale is a cheaper frame with the same framing
		const float l_Scale = k_RenderScales[m_RenderScaleIndex];
		const float l_FramebufferWidth = static_cast<float>(std::max(m_Services->GetFramebufferWidth(), 0));
		const float l_FramebufferHeight = static_cast<float>(std::max(m_Services->GetFramebufferHeight(), 0));

		Engine::RenderRequest l_Request;
		l_Request.View.Width = std::max(static_cast<uint32_t>(l_FramebufferWidth * l_Scale), 1u);
		l_Request.View.Height = std::max(static_cast<uint32_t>(l_FramebufferHeight * l_Scale), 1u);
		l_Request.View.ActiveCamera = m_Controller.GetCamera();
		l_Request.View.Settings = m_Settings;
		l_Request.View.Mode = m_Mode;
		l_Request.ActiveScene = &m_Scene;
		l_Request.Exposure = std::exp2(m_ExposureStops);

		return l_Request;
	}
}