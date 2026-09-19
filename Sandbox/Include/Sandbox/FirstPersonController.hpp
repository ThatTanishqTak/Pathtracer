#pragma once

#include "Engine/Engine.hpp"

namespace Sandbox
{
	// Provisional navigation for the Sandbox: mouse look and horizontal walking at the spawn's eye height through an open scene. Step 17 adds collision, grounding and jumping
	class FirstPersonController
	{
	public:
		struct Settings
		{
			float WalkSpeed = 3.0f; // Metres per second
			float RunSpeed = 6.0f; // Metres per second while Shift is held
			float MouseSensitivity = Engine::Math::ToRadians(0.08f); // Radians per relative mouse unit, applied to the frame's accumulated delta and never to time
			float MaxPitch = Engine::Math::ToRadians(89.0f); // Short of vertical so the walking axes never collapse
		};

		// Places the camera at the spawn and takes its heading from the spawn orientation, roll is dropped and the pitch is clamped
		void Reset(const Engine::PlayerSpawn& spawn);

		// Reads held keys, the frame's mouse delta and the capture gesture from the input snapshot. Escape releases capture, a left click while uncaptured requests it again through the services
		void Update(const Engine::FrameTime& time, const Engine::InputState& input, Engine::ApplicationServices& services);

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
		float m_EyeHeight = 0.0f; // World Y the camera is pinned to until Step 17 grounds the player
	};
}