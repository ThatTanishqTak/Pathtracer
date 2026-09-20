#pragma once

#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Material.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Engine
{
	// Scene content, not workspace state: the Sandbox starts here, the Editor camera does not touch it
	struct PlayerSpawn
	{
		Math::Vector3 Position{ 0.0f, 1.7f, 5.0f };
		Math::Quaternion Orientation = Math::k_IdentityRotation; // Starting view, identity looks along -Z
	};

	struct Environment
	{
		Math::Vector3 Radiance{ 0.05f, 0.07f, 0.1f }; // Constant linear radiance returned on a miss
	};

	// The authoritative CPU scene. Holds no GPU handles, the renderer derives its own records from it
	class Scene
	{
	public:
		Scene();

		// Display name, scene content like everything else here
		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		// The reference stays valid until the next CreateEntity or DestroyEntity call, keep the Id instead
		Entity& CreateEntity(std::string name);
		bool DestroyEntity(EntityId id);

		// Inserts an entity with the Id it already carries, for scene files and undo. Returns null for Invalid or an Id already in use, otherwise moves the next-Id counter past it so nothing created later collides. The pointer is valid until the next Create, Restore or Destroy call
		Entity* RestoreEntity(Entity entity);

		Entity* FindEntity(EntityId id);
		const Entity* FindEntity(EntityId id) const;
		const std::vector<Entity>& GetEntities() const { return m_Entities; }

		// The reference stays valid until the next CreateMaterial or DestroyMaterial call, keep the Id instead
		Material& CreateMaterial(std::string name);
		bool DestroyMaterial(MaterialId id); // Entities that used it fall back to MaterialId::Invalid

		// The material twin of RestoreEntity
		Material* RestoreMaterial(Material material);

		Material* FindMaterial(MaterialId id);
		const Material* FindMaterial(MaterialId id) const;
		const std::vector<Material>& GetMaterials() const { return m_Materials; }
		uint32_t CountMaterialUsers(MaterialId id) const;

		// The Ids the next Create calls hand out. A scene file stores them so a loaded scene never reuses an Id that a deleted entity once had, Reserve only ever raises them
		uint64_t GetNextEntityId() const { return m_NextEntityId; }
		uint64_t GetNextMaterialId() const { return m_NextMaterialId; }
		void ReserveEntityIds(uint64_t nextId);
		void ReserveMaterialIds(uint64_t nextId);

		PlayerSpawn& GetPlayerSpawn() { return m_PlayerSpawn; }
		const PlayerSpawn& GetPlayerSpawn() const { return m_PlayerSpawn; }

		Environment& GetEnvironment() { return m_Environment; }
		const Environment& GetEnvironment() const { return m_Environment; }

		// Changes through Create and Destroy advance the revision themselves. After editing an entity, material or the environment through a mutable pointer, call MarkRadianceChanged so the renderer re-extracts
		uint64_t GetRadianceRevision() const { return m_RadianceRevision; }
		void MarkRadianceChanged();

	private:
		std::string m_Name;

		std::vector<Entity> m_Entities;
		std::vector<Material> m_Materials;

		PlayerSpawn m_PlayerSpawn;
		Environment m_Environment;

		uint64_t m_NextEntityId = 1;
		uint64_t m_NextMaterialId = 1;
		uint64_t m_RadianceRevision = 0;
	};
}