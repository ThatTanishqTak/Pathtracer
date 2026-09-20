#pragma once

#include "Engine/Engine.hpp"

namespace Editor
{
	// Lists the scene's entities and selects one by its stable Id, the way the Sandbox's Period key does. Read-only at Step 10, Step 11 adds create, rename, duplicate and delete through commands
	class SceneHierarchyPanel
	{
	public:
		void Draw(const Engine::Scene& scene, Engine::EntityId& selectedEntity);
	};
}