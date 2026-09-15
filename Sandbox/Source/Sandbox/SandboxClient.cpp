#include "Sandbox/SandboxClient.hpp"

namespace Sandbox
{
	void SandboxClient::OnStart(Engine::ApplicationServices& services)
	{
		m_Services = &services;

		PT_APP_INFO("Sandbox client started, {}x{} window", m_Services->GetWindowWidth(), m_Services->GetWindowHeight());
		PT_APP_INFO("Controls: Space cycles the clear color, Tab toggles mouse capture, Escape closes");
	}

	void SandboxClient::OnStop() noexcept
	{
		PT_APP_INFO("Sandbox client stopped");

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

		if (input.WasKeyPressed(Engine::Key::Space))
		{
			m_ClearColorIndex = (m_ClearColorIndex + 1) % k_ClearColors.size();

			PT_APP_INFO("Clear color {}", m_ClearColorIndex);
		}

		if (input.WasKeyPressed(Engine::Key::Tab))
		{
			m_Services->SetMouseCaptured(!m_Services->IsMouseCaptured());

			PT_APP_INFO("Mouse capture {}", m_Services->IsMouseCaptured() ? "on" : "off");
		}

		// Held state only reads while the window has focus, which the host guarantees by clearing it on focus loss
		if (input.IsKeyDown(Engine::Key::W) || input.IsKeyDown(Engine::Key::A) || input.IsKeyDown(Engine::Key::S) || input.IsKeyDown(Engine::Key::D))
		{
			PT_APP_TRACE("Movement keys held for {:.4f}s", time.DeltaSeconds);
		}

		m_StatisticsElapsed += time.ElapsedSeconds;
		m_StatisticsFrames += 1;
		m_StatisticsMouseDeltaX += input.MouseDeltaX;
		m_StatisticsMouseDeltaY += input.MouseDeltaY;

		if (m_StatisticsElapsed >= 1.0f)
		{
			const float l_AverageMilliseconds = (m_StatisticsElapsed / static_cast<float>(m_StatisticsFrames)) * 1000.0f;

			PT_APP_TRACE("Frame {}: {} frames in {:.2f}s, {:.2f} ms average, focus {}, capture {}, mouse delta ({:.1f}, {:.1f})", time.FrameIndex, m_StatisticsFrames, m_StatisticsElapsed, l_AverageMilliseconds, input.HasFocus, input.MouseCaptured, m_StatisticsMouseDeltaX, m_StatisticsMouseDeltaY);

			m_StatisticsElapsed = 0.0f;
			m_StatisticsFrames = 0;
			m_StatisticsMouseDeltaX = 0.0f;
			m_StatisticsMouseDeltaY = 0.0f;
		}
	}

	Engine::RenderRequest SandboxClient::GetRenderRequest() const
	{
		Engine::RenderRequest l_Request;
		l_Request.ClearColor = k_ClearColors[m_ClearColorIndex];

		return l_Request;
	}
}