#pragma once

#include "Engine/Engine.hpp"

namespace Editor
{
	// Shows the selected entity: name, transform, geometry and material. Read-only at Step 10, Step 11 turns the fields into edits routed through commands
	class InspectorPanel
	{
	public:
		void Draw(const Engine::Scene& scene, Engine::EntityId selectedEntity);

	private:
		void DrawTransform(const Engine::TransformComponent& transform);
		void DrawGeometry(const Engine::GeometryComponent& geometry);
		void DrawMaterial(const Engine::Scene& scene, Engine::MaterialId materialId);
	};
}