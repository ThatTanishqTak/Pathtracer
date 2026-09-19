#include "Sandbox/FirstPersonController.hpp"

#include <algorithm>
#include <cmath>

namespace Sandbox
{
	namespace
	{
		constexpr float k_Epsilon = 1e-6f;

		// Drops the vertical component and normalizes, so a tilted camera axis becomes a walking direction on the ground plane
		bool ProjectOntoGround(const Engine::Math::Vector3& axis, Engine::Math::Vector3& projected)
		{
			projected = Engine::Math::Vector3(axis.x, 0.0f, axis.z);

			const float l_Length = glm::length(projected);
			if (l_Length <= k_Epsilon)
			{
				return false;
			}

			projected /= l_Length;

			return true;
		}
	}

	void FirstPersonController::Reset(const Engine::PlayerSpawn& spawn)
	{
		m_Camera.Position = spawn.Position;
		m_EyeHeight = spawn.Position.y;

		m_Angles = Engine::YawPitchFromOrientation(spawn.Orientation);
		m_Angles.Pitch = std::clamp(m_Angles.Pitch, -m_Settings.MaxPitch, m_Settings.MaxPitch);
		m_Camera.Orientation = Engine::OrientationFromYawPitch(m_Angles);

		PT_APP_INFO("First-person controller reset to ({:.2f}, {:.2f}, {:.2f}), yaw {:.1f} degrees, pitch {:.1f} degrees", m_Camera.Position.x, m_Camera.Position.y, m_Camera.Position.z, Engine::Math::ToDegrees(m_Angles.Yaw), Engine::Math::ToDegrees(m_Angles.Pitch));
	}

	void FirstPersonController::Update(const Engine::FrameTime& time, const Engine::InputState& input, Engine::ApplicationServices& services)
	{
		// Capture is a gesture and nothing else: Escape releases it, a click while uncaptured asks for it back. The host drops it on focus loss and at stop by itself
		if (input.MouseCaptured)
		{
			if (input.WasKeyPressed(Engine::Key::Escape))
			{
				services.SetMouseCaptured(false);

				if (!services.IsMouseCaptured())
				{
					PT_APP_INFO("Mouse released, click the window to look around again, Escape again closes");
				}
			}
		}
		else if (input.WasMouseButtonPressed(Engine::MouseButton::Left))
		{
			services.SetMouseCaptured(true);

			if (services.IsMouseCaptured())
			{
				PT_APP_INFO("Mouse captured, Escape releases it");
			}
		}

		// The snapshot still says what the frame started with, so the click frame's cursor movement is never read as a look
		if (input.MouseCaptured)
		{
			UpdateLook(input);
		}

		UpdateMovement(time, input);
	}

	void FirstPersonController::UpdateLook(const Engine::InputState& input)
	{
		if (input.MouseDeltaX == 0.0f && input.MouseDeltaY == 0.0f)
		{
			return;
		}

		// The delta is already the whole frame's motion in relative units, so sensitivity alone scales it. Mouse right turns right, which is a negative yaw about +Y, and mouse up is a negative window Y that looks up
		m_Angles.Yaw = std::remainder(m_Angles.Yaw - input.MouseDeltaX * m_Settings.MouseSensitivity, Engine::Math::k_TwoPi);
		m_Angles.Pitch = std::clamp(m_Angles.Pitch - input.MouseDeltaY * m_Settings.MouseSensitivity, -m_Settings.MaxPitch, m_Settings.MaxPitch);

		m_Camera.Orientation = Engine::OrientationFromYawPitch(m_Angles);
	}

	void FirstPersonController::UpdateMovement(const Engine::FrameTime& time, const Engine::InputState& input)
	{
		// Walking axes are the camera basis projected onto the ground, so looking up never lifts a forward step. The pitch clamp keeps both projections non-zero
		const Engine::CameraBasis l_Basis = Engine::GetCameraBasis(m_Camera);

		Engine::Math::Vector3 l_Forward;
		Engine::Math::Vector3 l_Right;
		if (!ProjectOntoGround(l_Basis.Forward, l_Forward) || !ProjectOntoGround(l_Basis.Right, l_Right))
		{
			return;
		}

		// Held keys only read while the window has focus, the host clears them on focus loss so nothing sticks across alt-tab
		Engine::Math::Vector3 l_Direction(0.0f);
		if (input.IsKeyDown(Engine::Key::W))
		{
			l_Direction += l_Forward;
		}

		if (input.IsKeyDown(Engine::Key::S))
		{
			l_Direction -= l_Forward;
		}

		if (input.IsKeyDown(Engine::Key::D))
		{
			l_Direction += l_Right;
		}

		if (input.IsKeyDown(Engine::Key::A))
		{
			l_Direction -= l_Right;
		}

		const float l_Length = glm::length(l_Direction);
		if (l_Length <= k_Epsilon)
		{
			return;
		}

		// A diagonal is normalized so it is no faster than a straight line, and the step scales with the clamped delta so the speed is the same at every frame rate
		const bool l_Running = input.IsKeyDown(Engine::Key::LeftShift) || input.IsKeyDown(Engine::Key::RightShift);
		const float l_Speed = l_Running ? m_Settings.RunSpeed : m_Settings.WalkSpeed;

		m_Camera.Position += l_Direction * (l_Speed * time.DeltaSeconds / l_Length);
		m_Camera.Position.y = m_EyeHeight;
	}
}