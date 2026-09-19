#pragma once

#include "Engine/Engine.hpp"

#include <cstdint>

namespace Sandbox
{
	class SandboxClient final : public Engine::ApplicationClient
	{
	public:
		void OnStart(Engine::ApplicationServices& services) override;
		void OnStop() noexcept override;
		void OnEvent(const Engine::InputEvent& event) override;
		void Update(const Engine::FrameTime& time, const Engine::InputState& input) override;
		Engine::RenderRequest GetRenderRequest() const override;

	private:
		void UpdateCamera(const Engine::FrameTime& time);

		void BuildDemoScene();
		void CreateSphere();
		void DestroySelected();
		void SelectNext();
		void MoveSelected(const Engine::Math::Vector3& delta);
		void RecolorSelected();

		Engine::ApplicationServices* m_Services = nullptr;

		// Throwaway turntable around the scene origin, Step 8 replaces it with the first-person controller
		static constexpr float k_OrbitRadius = 4.0f;
		static constexpr float k_OrbitElevation = Engine::Math::ToRadians(20.0f);
		static constexpr float k_OrbitSpeed = 0.4f; // Radians per second

		static constexpr float k_FieldOfViewStep = Engine::Math::ToRadians(5.0f);
		static constexpr float k_MinFieldOfView = Engine::Math::ToRadians(10.0f);
		static constexpr float k_MaxFieldOfView = Engine::Math::ToRadians(150.0f);

		// Exposure is stepped in photographic stops and converted to a linear scale for the render request
		static constexpr float k_ExposureStepStops = 0.5f;
		static constexpr float k_ExposureRangeStops = 8.0f;

		// Scene editing keys, enough to exercise create, move, recolour and delete against the uploaded records
		static constexpr float k_MoveStep = 0.25f; // Metres per key press
		static constexpr float k_CreatedSphereRadius = 0.4f;
		static constexpr float k_CreatedSphereRing = 2.5f; // Distance from the origin new spheres appear at

		// The client owns the scene, the renderer only sees it through the render request
		Engine::Scene m_Scene;
		Engine::EntityId m_SelectedEntity = Engine::EntityId::Invalid;
		uint32_t m_CreatedSpheres = 0;
		uint32_t m_PaletteIndex = 0;

		Engine::Camera m_Camera;
		Engine::DiagnosticMode m_Mode = Engine::DiagnosticMode::RayDirection;
		float m_OrbitAngle = 0.0f;
		bool m_OrbitPaused = false;
		float m_ExposureStops = 0.0f;
		float m_StatisticsElapsed = 0.0f;
		uint32_t m_StatisticsFrames = 0;
		float m_StatisticsMouseDeltaX = 0.0f;
		float m_StatisticsMouseDeltaY = 0.0f;
	};
}