#pragma once

#include "Engine/Engine.hpp"

#include "Sandbox/FirstPersonController.hpp"
#include "Sandbox/SandboxOptions.hpp"

#include <cstdint>
#include <filesystem>

namespace Sandbox
{
	class SandboxClient final : public Engine::ApplicationClient
	{
	public:
		explicit SandboxClient(SandboxOptions options);

		void OnStart(Engine::ApplicationServices& services) override;
		void OnStop() noexcept override;
		void OnEvent(const Engine::InputEvent& event) override;
		void Update(const Engine::FrameTime& time, const Engine::InputState& input) override;
		Engine::RenderRequest GetRenderRequest() const override;

	private:
		// A relative path is anchored at the asset root, an absolute one is taken as given
		std::filesystem::path ResolveScenePath(const std::filesystem::path& path) const;

		// Loading is a transaction: on failure the current scene, demo or file, is exactly what it was. Success remembers the path for F5 and reselects the first entity
		bool LoadScene(const std::filesystem::path& path);
		void SaveScene();

		void BuildDemoScene();
		void CreateSphere();
		void CreateMesh();
		void DestroySelected();
		void SelectNext();
		void MoveSelected(const Engine::Math::Vector3& delta);
		void RecolorSelected();

		Engine::ApplicationServices* m_Services = nullptr;

		// Where the scene came from and where F5 writes it back to. The default is what a fresh Sandbox saves the demo scene as
		static constexpr const char* k_DefaultScenePath = "Scenes/FirstScene.scene.json";

		SandboxOptions m_Options;
		std::filesystem::path m_AssetRoot;
		std::filesystem::path m_ScenePath; // Empty until a scene was loaded or saved

		// The controller owns the camera, the client only chooses the lens
		static constexpr float k_VerticalFieldOfView = Engine::Math::ToRadians(60.0f);

		// Exposure is stepped in photographic stops and converted to a linear scale for the render request
		static constexpr float k_ExposureStepStops = 0.5f;
		static constexpr float k_ExposureRangeStops = 8.0f;

		// Scene editing keys, enough to exercise create, move, recolour and delete against the uploaded records
		static constexpr float k_MoveStep = 0.25f; // Metres per key press
		static constexpr float k_CreatedSphereRadius = 0.4f;
		static constexpr float k_CreatedSphereRing = 2.5f; // Distance from the origin new spheres and meshes appear at
		static constexpr float k_CreatedMeshSize = 0.8f; // The transform scale of a created cube or icosphere, both fit the unit cube

		// The view renders at the framebuffer size times this scale, so the render extent can differ from the window
		static constexpr uint32_t k_RenderScaleCount = 3;
		static constexpr float k_RenderScales[k_RenderScaleCount] = { 1.0f, 0.5f, 0.25f };

		// Bounce limits the B key cycles through, zero shows emitters and the environment only
		static constexpr uint32_t k_BounceLimitCount = 5;
		static constexpr uint32_t k_BounceLimits[k_BounceLimitCount] = { 0, 1, 2, 4, 8 };

		// The client owns the scene and the mesh assets it references, the renderer only sees them through the render request
		Engine::Scene m_Scene;
		Engine::AssetManager m_Assets;
		Engine::EntityId m_SelectedEntity = Engine::EntityId::Invalid;
		uint32_t m_CreatedSpheres = 0;
		uint32_t m_CreatedMeshes = 0;
		uint32_t m_PaletteIndex = 0;

		// Provisional first-person navigation, starts at the scene's player spawn
		FirstPersonController m_Controller;

		Engine::DiagnosticMode m_Mode = Engine::DiagnosticMode::PathTraced;
		Engine::RenderSettings m_Settings;
		uint32_t m_RenderScaleIndex = 0;
		uint32_t m_BounceLimitIndex = 3;
		float m_ExposureStops = 0.0f;
		float m_StatisticsElapsed = 0.0f;
		uint32_t m_StatisticsFrames = 0;
		float m_StatisticsMouseDeltaX = 0.0f;
		float m_StatisticsMouseDeltaY = 0.0f;
	};
}