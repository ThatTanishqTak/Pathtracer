#pragma once

#include "Engine/Engine.hpp"

namespace Editor
{
	// The Editor free camera: mouse look and flight along the camera axes. Workspace state, it reads the player spawn once at Reset and never writes it, so moving this camera cannot move where the Sandbox starts
	class EditorCameraController
	{
	public:
		struct Settings
		{
			float FlySpeed = 3.0f; // Metres per second
			float FastSpeed = 9.0f; // Metres per second while Shift is held
			float MouseSensitivity = Engine::Math::ToRadians(0.08f); // Radians per relative mouse unit, applied to the frame's accumulated delta and never to time
			float MaxPitch = Engine::Math::ToRadians(89.0f); // Short of vertical so the yaw stays defined
		};

		// Places the camera and takes its heading from the orientation, roll is dropped and the pitch is clamped
		void Reset(const Engine::Math::Vector3& position, const Engine::Math::Quaternion& orientation);

		// Looks from the frame's mouse delta while lookEnabled, flies on W A S D Q E while moveEnabled. Both gates are the client's decision from the viewport's input ownership, the controller never talks to the services
		void Update(const Engine::FrameTime& time, const Engine::InputState& input, bool lookEnabled, bool moveEnabled);

		void SetVerticalFieldOfView(float radians) { m_Camera.VerticalFieldOfView = radians; }

		const Engine::Camera& GetCamera() const { return m_Camera; }
		const Engine::YawPitch& GetAngles() const { return m_Angles; }

		Settings& GetSettings() { return m_Settings; }
		const Settings& GetSettings() const { return m_Settings; }

	private:
		void UpdateLook(const Engine::InputState& input);
		void UpdateMovement(const Engine::FrameTime& time, const Engine::InputState& input);

		Settings m_Settings;

		// The controller owns the camera and writes only Position and Orientation, the renderer fills the extent from the view
		Engine::Camera m_Camera;
		Engine::YawPitch m_Angles;
	};
}