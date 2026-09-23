#pragma once

#include "Engine/Engine.hpp"

#include "Editor/EditorActions.hpp"
#include "Editor/EditorCameraController.hpp"
#include "Editor/EditorCommandHistory.hpp"
#include "Editor/EditorOptions.hpp"
#include "Editor/Panels/InspectorPanel.hpp"
#include "Editor/Panels/RenderSettingsPanel.hpp"
#include "Editor/Panels/SceneHierarchyPanel.hpp"
#include "Editor/Panels/ViewportPanel.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Editor
{
	class EditorClient final : public Engine::ApplicationClient, public EditorActions
	{
	public:
		explicit EditorClient(EditorOptions options);

		void OnStart(Engine::ApplicationServices& services) override;
		void OnStop() noexcept override;
		void OnEvent(const Engine::InputEvent& event) override;
		void BuildUI() override;
		void Update(const Engine::FrameTime& time, const Engine::InputState& input) override;
		Engine::RenderRequest GetRenderRequest() const override;

		bool OnCloseRequested() override;
		void CreateEntity(CreateEntityKind kind) override;
		void ImportMesh() override;
		void RenameEntity(Engine::EntityId id, std::string name) override;
		void DuplicateEntity(Engine::EntityId id) override;
		void DeleteEntity(Engine::EntityId id) override;
		void PlaceSpawnAtCamera() override;

	private:
		enum class PendingAction : uint8_t
		{
			None,
			NewScene,
			OpenScene,
			LaunchSandbox,
			Exit,
		};

		// What the open dialog was shown for, the result only says that it was an open dialog
		enum class OpenDialogPurpose : uint8_t
		{
			Scene,
			MeshImport,
		};

		std::filesystem::path ResolveScenePath(const std::filesystem::path& path) const;
		std::filesystem::path NormalizeScenePath(std::filesystem::path path) const;

		bool LoadScene(const std::filesystem::path& path);
		bool SaveSceneTo(const std::filesystem::path& path);
		void BuildEmptyScene();

		void BuildDefaultLayout(uint32_t dockspaceId);

		void DrawMainMenuBar();
		void DrawUnsavedChangesPrompt();
		void HandleShortcuts();
		void PollFileDialog();

		void RequestNewScene();
		void RequestOpenScene();
		void RequestLaunchSandbox();
		void RequestExit();
		void RunPendingAction();

		void LaunchSandbox();
		void PollSandbox();

		void NewScene();
		void ShowOpenDialog();
		void ShowSaveAsDialog();
		void SaveScene();

		void Undo();
		void Redo();

		void PickEntity(float mouseX, float mouseY);
		void SetFileStatus(std::string summary, const Engine::SceneFileResult& result);

		void ImportMeshFrom(const std::filesystem::path& path);
		void ExecuteCreate(Engine::Entity entity, Engine::Material material);
		Engine::Math::Vector3 GetCreatePosition() const;

		Engine::ApplicationServices* m_Services = nullptr;

		EditorOptions m_Options;
		std::filesystem::path m_AssetRoot;
		std::filesystem::path m_ScenePath;

		static constexpr float k_VerticalFieldOfView = Engine::Math::ToRadians(60.0f);
		static constexpr float k_CreateDistance = 4.0f;
		static constexpr float k_AreaLightHeight = 2.0f;

		Engine::Scene m_Scene;
		Engine::AssetManager m_Assets; // The meshes the scene references, process-wide: a new or opened scene never clears it
		Engine::EntityId m_SelectedEntity = Engine::EntityId::Invalid;

		EditorCommandHistory m_History;
		SceneFileStatus m_FileStatus;
		uint32_t m_CreatedEntities = 0;

		PendingAction m_PendingAction = PendingAction::None;
		OpenDialogPurpose m_OpenDialogPurpose = OpenDialogPurpose::Scene;
		bool m_OpenPromptRequested = false;
		bool m_PromptOpen = false;
		bool m_RunPendingAfterSave = false;
		bool m_ExitConfirmed = false;

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