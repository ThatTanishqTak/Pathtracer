#include "Sandbox/SandboxClient.hpp"

#include "Sandbox/CameraChecks.hpp"

#include <algorithm>
#include <cmath>

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
				case Engine::DiagnosticMode::SphereHit:
				{
					return "sphere hit";
				}
				case Engine::DiagnosticMode::SphereNormal:
				{
					return "sphere normal";
				}
				case Engine::DiagnosticMode::SphereDistance:
				{
					return "sphere distance";
				}
				default:
				{
					return "unknown";
				}
			}
		}
	}

	void SandboxClient::OnStart(Engine::ApplicationServices& services)
	{
		m_Services = &services;

		PT_APP_INFO("Sandbox client started, {}x{} window, {}x{} framebuffer", m_Services->GetWindowWidth(), m_Services->GetWindowHeight(), m_Services->GetFramebufferWidth(), m_Services->GetFramebufferHeight());
		PT_APP_INFO("Controls: 1-4 select the diagnostic mode, P pauses the orbit, LeftBracket and RightBracket step the field of view, Equals and Minus step exposure, Tab toggles mouse capture, Escape closes");

		if (!RunCameraChecks())
		{
			PT_APP_WARN("Camera checks failed, the diagnostic modes may not match the CPU camera");
		}

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
		const Engine::Key k_ModeKeys[] = { Engine::Key::Number1, Engine::Key::Number2, Engine::Key::Number3, Engine::Key::Number4 };
		for (uint32_t i_Mode = 0; i_Mode < 4; ++i_Mode)
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

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, mouse delta ({:.1f}, {:.1f}), mode {}, camera ({:.2f}, {:.2f}, {:.2f}) at {}x{}", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, m_StatisticsMouseDeltaX, m_StatisticsMouseDeltaY, ModeName(m_Mode), m_Camera.Position.x, m_Camera.Position.y, m_Camera.Position.z, m_Camera.ViewWidth, m_Camera.ViewHeight);

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

		// Circle the unit sphere at the origin, slightly above it, always looking at its centre
		const float l_Horizontal = k_OrbitRadius * std::cos(k_OrbitElevation);
		m_Camera.Position = Engine::Math::Vector3(l_Horizontal * std::sin(m_OrbitAngle), k_OrbitRadius * std::sin(k_OrbitElevation), l_Horizontal * std::cos(m_OrbitAngle));
		m_Camera.Orientation = Engine::LookAtOrientation(m_Camera.Position, Engine::Math::Vector3(0.0f, 0.0f, 0.0f));

		// The render extent is the framebuffer until RenderView owns it in Step 7, a mismatch here shows up as a stretched sphere
		m_Camera.ViewWidth = static_cast<uint32_t>(std::max(m_Services->GetFramebufferWidth(), 0));
		m_Camera.ViewHeight = static_cast<uint32_t>(std::max(m_Services->GetFramebufferHeight(), 0));
	}

	Engine::RenderRequest SandboxClient::GetRenderRequest() const
	{
		Engine::RenderRequest l_Request;
		l_Request.ActiveCamera = m_Camera;
		l_Request.Mode = m_Mode;
		l_Request.Exposure = std::exp2(m_ExposureStops);

		return l_Request;
	}
}