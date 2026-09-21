#pragma once

#include "Engine/Engine.hpp"

#include "Editor/EditorActions.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Editor
{
	struct SceneFileStatus
	{
		std::string Summary;
		std::string Error;
		std::vector<std::string> Warnings;
		bool Failed = false;
	};

	void DrawCreateMenuItems(EditorActions& actions);

	class SceneHierarchyPanel
	{
	public:
		void Draw(const Engine::Scene& scene, Engine::EntityId& selectedEntity, EditorActions& actions, bool dirty, const std::filesystem::path& scenePath, const SceneFileStatus& fileStatus);

	private:
		enum class PendingKind : uint8_t
		{
			None,
			Rename,
			Duplicate,
			Delete,
		};

		void DrawFileStatus(const SceneFileStatus& fileStatus);
		void DrawEntityRow(const Engine::Entity& entity, Engine::EntityId& selectedEntity);
		void BeginRename(const Engine::Entity& entity);

		PendingKind m_PendingKind = PendingKind::None;
		Engine::EntityId m_PendingEntity = Engine::EntityId::Invalid;
		std::string m_PendingName;

		Engine::EntityId m_RenamingEntity = Engine::EntityId::Invalid;
		std::array<char, 256> m_RenameBuffer{};
		bool m_RenameFocusPending = false;
	};
}