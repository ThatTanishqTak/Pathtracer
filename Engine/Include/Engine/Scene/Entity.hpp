#pragma once

#include "Engine/Scene/Components.hpp"
#include "Engine/Scene/Material.hpp"

#include <cstdint>
#include <string>

namespace Engine
{
	// Stable entity identity, unique inside one scene for its whole life and never reused. Selection, undo and scene files refer to this, never to a list position
	enum class EntityId : uint64_t
	{
		Invalid = 0,
	};

	// One flat scene object, parenting arrives with the editor
	struct Entity
	{
		EntityId Id = EntityId::Invalid;
		std::string Name;

		TransformComponent Transform;
		GeometryComponent Geometry;
		MaterialId Material = MaterialId::Invalid; // Invalid or a deleted material renders with the fallback material

		bool Visible = true;
	};
}