#include "Editor/EditorCameraController.hpp"

#include <algorithm>
#include <cmath>

namespace Editor
{
	namespace
	{
		constexpr float k_Epsilon = 1e-6f;
	}

	void EditorCameraController::Reset(const Engine::Math::Vector3& position, const Engine::Math::Quaternion& orientation)
	{
		m_Camera.Position = position;

		m_Angles = Engine::YawPitchFromOrientation(orientation);
		m_Angles.Pitch = std::clamp(m_Angles.Pitch, -m_Settings.MaxPitch, m_Settings.MaxPitch);
		m_Camera.Orientation = Engine::OrientationFromYawPitch(m_Angles);

		PT_APP_INFO("Editor camera reset to ({:.2f}, {:.2f}, {:.2f}), yaw {:.1f} degrees, pitch {:.1f} degrees", m_Camera.Position.x, m_Camera.Position.y, m_Camera.Position.z, Engine::Math::ToDegrees(m_Angles.Yaw), Engine::Math::ToDegrees(m_Angles.Pitch));
	}

	void EditorCameraController::Update(const Engine::FrameTime& time, const Engine::InputState& input, bool lookEnabled, bool moveEnabled)
	{
		if (lookEnabled)
		{
			UpdateLook(input);
		}

		if (moveEnabled)
		{
			UpdateMovement(time, input);
		}
	}

	void EditorCameraController::UpdateLook(const Engine::InputState& input)
	{
		if (input.MouseDeltaX == 0.0f && input.MouseDeltaY == 0.0f)
		{
			return;
		}

		// The same composition as the Sandbox controller, so GetCameraBasis reads both cameras the same way: mouse right turns right, a negative yaw about +Y, and mouse up is a negative window Y that looks up
		m_Angles.Yaw = std::remainder(m_Angles.Yaw - input.MouseDeltaX * m_Settings.MouseSensitivity, Engine::Math::k_TwoPi);
		m_Angles.Pitch = std::clamp(m_Angles.Pitch - input.MouseDeltaY * m_Settings.MouseSensitivity, -m_Settings.MaxPitch, m_Settings.MaxPitch);

		m_Camera.Orientation = Engine::OrientationFromYawPitch(m_Angles);
	}

	void EditorCameraController::UpdateMovement(const Engine::FrameTime& time, const Engine::InputState& input)
	{
		// A free camera flies where it looks: forward and right are the camera axes with their tilt kept, unlike the Sandbox's ground-plane projection, and Q and E move along world up so a tilted camera still rises straight
		const Engine::CameraBasis l_Basis = Engine::GetCameraBasis(m_Camera);

		Engine::Math::Vector3 l_Direction(0.0f);
		if (input.IsKeyDown(Engine::Key::W))
		{
			l_Direction += l_Basis.Forward;
		}

		if (input.IsKeyDown(Engine::Key::S))
		{
			l_Direction -= l_Basis.Forward;
		}

		if (input.IsKeyDown(Engine::Key::D))
		{
			l_Direction += l_Basis.Right;
		}

		if (input.IsKeyDown(Engine::Key::A))
		{
			l_Direction -= l_Basis.Right;
		}

		if (input.IsKeyDown(Engine::Key::E))
		{
			l_Direction += Engine::Math::k_Up;
		}

		if (input.IsKeyDown(Engine::Key::Q))
		{
			l_Direction -= Engine::Math::k_Up;
		}

		const float l_Length = glm::length(l_Direction);
		if (l_Length <= k_Epsilon)
		{
			return;
		}

		// A diagonal is normalized so it is no faster than a straight line, and the step scales with the clamped delta so the speed is the same at every frame rate
		const bool l_Fast = input.IsKeyDown(Engine::Key::LeftShift) || input.IsKeyDown(Engine::Key::RightShift);
		const float l_Speed = l_Fast ? m_Settings.FastSpeed : m_Settings.FlySpeed;

		m_Camera.Position += l_Direction * (l_Speed * time.DeltaSeconds / l_Length);
	}
}