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
		PT_APP_INFO("Controls: 1-5 select the diagnostic mode, P pauses the orbit, LeftBracket and RightBracket step the field of view, Equals and Minus step exposure, Tab toggles mouse capture, Escape closes");
		PT_APP_INFO("Scene: N creates a sphere, Backspace deletes the selected entity, Period selects the next entity, arrows move the selection on X and Z, PageUp and PageDown on Y, C recolours its material");

		if (!RunCameraChecks())
		{
			PT_APP_WARN("Camera checks failed, the diagnostic modes may not match the CPU camera");
		}

		BuildDemoScene();

		// Fill the camera once now so it is complete before the first Update, a zero step leaves the orbit where it starts
		m_Camera.VerticalFieldOfView = Engine::Math::ToRadians(60.0f);
		UpdateCamera(Engine::FrameTime{});
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
		if (input.WasKeyPressed(Engine::Key::Escape))
		{
			m_Services->RequestClose();
		}

		// Number keys select the diagnostic view, the values line up with DiagnosticMode
		const Engine::Key k_ModeKeys[] = { Engine::Key::Number1, Engine::Key::Number2, Engine::Key::Number3, Engine::Key::Number4, Engine::Key::Number5 };
		for (uint32_t i_Mode = 0; i_Mode < 5; ++i_Mode)
		{
			if (input.WasKeyPressed(k_ModeKeys[i_Mode]))
			{
				m_Mode = static_cast<Engine::DiagnosticMode>(i_Mode);

				PT_APP_INFO("Diagnostic mode {}: {}", i_Mode + 1, ModeName(m_Mode));
			}
		}

		if (input.WasKeyPressed(Engine::Key::P))
		{
			m_OrbitPaused = !m_OrbitPaused;

			PT_APP_INFO("Orbit {}", m_OrbitPaused ? "paused" : "running");
		}

		if (input.WasKeyPressed(Engine::Key::LeftBracket) || input.WasKeyPressed(Engine::Key::RightBracket))
		{
			const float l_Direction = input.WasKeyPressed(Engine::Key::RightBracket) ? 1.0f : -1.0f;
			m_Camera.VerticalFieldOfView = std::clamp(m_Camera.VerticalFieldOfView + l_Direction * k_FieldOfViewStep, k_MinFieldOfView, k_MaxFieldOfView);

			PT_APP_INFO("Vertical field of view {:.0f} degrees", Engine::Math::ToDegrees(m_Camera.VerticalFieldOfView));
		}

		if (input.WasKeyPressed(Engine::Key::Equals) || input.WasKeyPressed(Engine::Key::Minus))
		{
			const float l_Direction = input.WasKeyPressed(Engine::Key::Equals) ? 1.0f : -1.0f;
			m_ExposureStops = std::clamp(m_ExposureStops + l_Direction * k_ExposureStepStops, -k_ExposureRangeStops, k_ExposureRangeStops);

			PT_APP_INFO("Exposure {:+.1f} stops ({:.3f}x)", m_ExposureStops, std::exp2(m_ExposureStops));
		}

		if (input.WasKeyPressed(Engine::Key::Tab))
		{
			m_Services->SetMouseCaptured(!m_Services->IsMouseCaptured());

			PT_APP_TRACE("Mouse capture {}", m_Services->IsMouseCaptured() ? "on" : "off");
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

		// Held state only reads while the window has focus, which the host guarantees by clearing it on focus loss
		if (input.IsKeyDown(Engine::Key::W) || input.IsKeyDown(Engine::Key::A) || input.IsKeyDown(Engine::Key::S) || input.IsKeyDown(Engine::Key::D))
		{
			PT_APP_TRACE("Movement keys held for {:.4f}s", time.DeltaSeconds);
		}

		UpdateCamera(time);

		m_StatisticsElapsed += time.ElapsedSeconds;
		m_StatisticsFrames += 1;
		m_StatisticsMouseDeltaX += input.MouseDeltaX;
		m_StatisticsMouseDeltaY += input.MouseDeltaY;

		if (m_StatisticsElapsed >= 1.0f)
		{
			const float l_AverageMilliseconds = (m_StatisticsElapsed / static_cast<float>(m_StatisticsFrames)) * 1000.0f;

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, mouse delta ({:.1f}, {:.1f}), mode {}, camera ({:.2f}, {:.2f}, {:.2f}) at {}x{}, {} entities, {} materials, scene revision {}, selected {}", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, m_StatisticsMouseDeltaX, m_StatisticsMouseDeltaY, ModeName(m_Mode), m_Camera.Position.x, m_Camera.Position.y, m_Camera.Position.z, m_Camera.ViewWidth, m_Camera.ViewHeight, m_Scene.GetEntities().size(), m_Scene.GetMaterials().size(), m_Scene.GetRadianceRevision(), std::to_underlying(m_SelectedEntity));

			m_StatisticsElapsed = 0.0f;
			m_StatisticsFrames = 0;
			m_StatisticsMouseDeltaX = 0.0f;
			m_StatisticsMouseDeltaY = 0.0f;
		}
	}

	void SandboxClient::UpdateCamera(const Engine::FrameTime& time)
	{
		// The clamped simulation step drives the orbit, so a long stall moves the camera by at most one clamped step
		if (!m_OrbitPaused)
		{
			m_OrbitAngle = std::fmod(m_OrbitAngle + k_OrbitSpeed * time.DeltaSeconds, Engine::Math::k_TwoPi);
		}

		// Circle the scene origin, slightly above it, always looking at the centre sphere
		const float l_Horizontal = k_OrbitRadius * std::cos(k_OrbitElevation);
		m_Camera.Position = Engine::Math::Vector3(l_Horizontal * std::sin(m_OrbitAngle), k_OrbitRadius * std::sin(k_OrbitElevation), l_Horizontal * std::cos(m_OrbitAngle));
		m_Camera.Orientation = Engine::LookAtOrientation(m_Camera.Position, Engine::Math::Vector3(0.0f, 0.0f, 0.0f));

		// The render extent is the framebuffer until RenderView owns it in Step 7, a mismatch here shows up as a stretched sphere
		m_Camera.ViewWidth = static_cast<uint32_t>(std::max(m_Services->GetFramebufferWidth(), 0));
		m_Camera.ViewHeight = static_cast<uint32_t>(std::max(m_Services->GetFramebufferHeight(), 0));
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

		// Scene content the Step 8 controller starts from, distinct from the orbit camera above
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
		Engine::RenderRequest l_Request;
		l_Request.ActiveCamera = m_Camera;
		l_Request.ActiveScene = &m_Scene;
		l_Request.Mode = m_Mode;
		l_Request.Exposure = std::exp2(m_ExposureStops);

		return l_Request;
	}
}