#pragma once

#include "Engine/Engine.hpp"

#include "Editor/EditorCameraController.hpp"
#include "Editor/EditorOptions.hpp"
#include "Editor/Panels/InspectorPanel.hpp"
#include "Editor/Panels/RenderSettingsPanel.hpp"
#include "Editor/Panels/SceneHierarchyPanel.hpp"
#include "Editor/Panels/ViewportPanel.hpp"

#include <cstdint>
#include <filesystem>

namespace Editor
{
	class EditorClient final : public Engine::ApplicationClient
	{
	public:
		explicit EditorClient(EditorOptions options);

		void OnStart(Engine::ApplicationServices& services) override;
		void OnStop() noexcept override;
		void OnEvent(const Engine::InputEvent& event) override;
		void BuildUI() override;
		void Update(const Engine::FrameTime& time, const Engine::InputState& input) override;
		Engine::RenderRequest GetRenderRequest() const override;

	private:
		// A relative path is anchored at the asset root, an absolute one is taken as given
		std::filesystem::path ResolveScenePath(const std::filesystem::path& path) const;

		// Loading is a transaction: on failure the current scene is exactly what it was. Success remembers the path and selects the first entity
		bool LoadScene(const std::filesystem::path& path);
		void BuildEmptyScene();

		// The first run has no layout file, so the four panels are docked around the viewport once. A saved layout is never touched
		void BuildDefaultLayout(uint32_t dockspaceId);

		Engine::ApplicationServices* m_Services = nullptr;

		EditorOptions m_Options;
		std::filesystem::path m_AssetRoot;
		std::filesystem::path m_ScenePath; // Empty until a scene was loaded, Step 11 saves back to it

		// The controller owns the camera, the client only chooses the lens
		static constexpr float k_VerticalFieldOfView = Engine::Math::ToRadians(60.0f);

		// The client owns the scene, the renderer only sees it through the render request
		Engine::Scene m_Scene;
		Engine::EntityId m_SelectedEntity = Engine::EntityId::Invalid;

		// Workspace state: where the Editor looks from and how it renders, never written into the scene
		EditorCameraController m_Camera;
		EditorRenderSettings m_RenderSettings;

		ViewportPanel m_ViewportPanel;
		SceneHierarchyPanel m_SceneHierarchyPanel;
		InspectorPanel m_InspectorPanel;
		RenderSettingsPanel m_RenderSettingsPanel;

		bool m_LayoutChecked = false;

		float m_StatisticsElapsed = 0.0f;
		uint32_t m_StatisticsFrames = 0;
	};
}