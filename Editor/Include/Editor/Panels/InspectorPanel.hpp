#pragma once

#include "Engine/Engine.hpp"

#include "Editor/EditorActions.hpp"
#include "Editor/EditorCommandHistory.hpp"
#include "Editor/EditorCommands.hpp"

#include <array>

namespace Editor
{
	class InspectorPanel
	{
	public:
		void Draw(Engine::Scene& scene, Engine::EntityId selectedEntity, EditorCommandHistory& history, EditorActions& actions);

	private:
		template <typename T>
		struct EditTracker
		{
			T Before{};
			bool InProgress = false;
		};

		void DrawEntity(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history);
		void DrawTransform(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history);
		void DrawGeometry(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history);
		void DrawMaterial(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history);
		void DrawSceneSettings(Engine::Scene& scene, EditorCommandHistory& history, EditorActions& actions);

		void TrackEntityEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, const Engine::Entity& entity, EditorCommandHistory& history, const char* what);
		void TrackMaterialEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, const Engine::Material& material, EditorCommandHistory& history, const char* what);
		void TrackSceneEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, EditorCommandHistory& history, const char* what);

		Engine::EntityId m_TrackedEntity = Engine::EntityId::Invalid;
		Engine::MaterialId m_TrackedMaterial = Engine::MaterialId::Invalid;

		EditTracker<Engine::Entity> m_EntityEdit;
		EditTracker<Engine::Material> m_MaterialEdit;
		EditTracker<SceneSettings> m_SceneEdit;

		std::array<char, 256> m_NameBuffer{};
		Engine::Math::Vector3 m_EulerDegrees{ 0.0f, 0.0f, 0.0f };
		bool m_EulerEditing = false;
		Engine::Math::Vector2 m_SpawnDegrees{ 0.0f, 0.0f };
		bool m_SpawnEditing = false;
	};
}