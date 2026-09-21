#pragma once

#include "Engine/Engine.hpp"

#include <cstdint>
#include <string>

namespace Editor
{
	enum class CreateEntityKind : uint8_t
	{
		Sphere,
		Quad,
		AreaLight,
	};

	class EditorActions
	{
	public:
		virtual ~EditorActions() = default;

		virtual void CreateEntity(CreateEntityKind kind) = 0;
		virtual void RenameEntity(Engine::EntityId id, std::string name) = 0;
		virtual void DuplicateEntity(Engine::EntityId id) = 0;
		virtual void DeleteEntity(Engine::EntityId id) = 0;
		virtual void PlaceSpawnAtCamera() = 0;
	};
}