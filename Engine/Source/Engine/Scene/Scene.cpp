#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace Engine
{
	namespace
	{
		// One counter for every scene in the process, so two scenes never share a revision and a renderer that sees a new value always re-extracts, even after the client swapped scenes
		std::atomic<uint64_t> s_NextRadianceRevision{ 1 };

		uint64_t NextRadianceRevision()
		{
			return s_NextRadianceRevision.fetch_add(1, std::memory_order_relaxed);
		}

		// The largest Id a Restore accepts, one past it must still be a valid counter value
		constexpr uint64_t k_MaxRestorableId = std::numeric_limits<uint64_t>::max() - 1;
	}

	Scene::Scene()
	{
		m_RadianceRevision = NextRadianceRevision();
	}

	Entity& Scene::CreateEntity(std::string name)
	{
		Entity& l_Entity = m_Entities.emplace_back();
		l_Entity.Id = static_cast<EntityId>(m_NextEntityId++);
		l_Entity.Name = std::move(name);

		MarkRadianceChanged();

		return l_Entity;
	}

	bool Scene::DestroyEntity(EntityId id)
	{
		const auto l_Found = std::find_if(m_Entities.begin(), m_Entities.end(), [id](const Entity& entity) { return entity.Id == id; });
		if (l_Found == m_Entities.end())
		{
			return false;
		}

		// Order is preserved, so the remaining entities keep their relative positions and their Ids
		m_Entities.erase(l_Found);

		MarkRadianceChanged();

		return true;
	}

	Entity* Scene::RestoreEntity(Entity entity)
	{
		const uint64_t l_Id = std::to_underlying(entity.Id);
		if (entity.Id == EntityId::Invalid || l_Id > k_MaxRestorableId || FindEntity(entity.Id) != nullptr)
		{
			return nullptr;
		}

		// Never lowered: a file that stores a counter below its largest Id still gets a counter past every Id present
		m_NextEntityId = std::max(m_NextEntityId, l_Id + 1);

		Entity& l_Entity = m_Entities.emplace_back(std::move(entity));

		MarkRadianceChanged();

		return &l_Entity;
	}

	Entity* Scene::FindEntity(EntityId id)
	{
		const auto l_Found = std::find_if(m_Entities.begin(), m_Entities.end(), [id](const Entity& entity) { return entity.Id == id; });

		return l_Found != m_Entities.end() ? &*l_Found : nullptr;
	}

	const Entity* Scene::FindEntity(EntityId id) const
	{
		const auto l_Found = std::find_if(m_Entities.begin(), m_Entities.end(), [id](const Entity& entity) { return entity.Id == id; });

		return l_Found != m_Entities.end() ? &*l_Found : nullptr;
	}

	Material& Scene::CreateMaterial(std::string name)
	{
		Material& l_Material = m_Materials.emplace_back();
		l_Material.Id = static_cast<MaterialId>(m_NextMaterialId++);
		l_Material.Name = std::move(name);

		MarkRadianceChanged();

		return l_Material;
	}

	bool Scene::DestroyMaterial(MaterialId id)
	{
		const auto l_Found = std::find_if(m_Materials.begin(), m_Materials.end(), [id](const Material& material) { return material.Id == id; });
		if (l_Found == m_Materials.end())
		{
			return false;
		}

		m_Materials.erase(l_Found);

		// A dangling reference would otherwise warn on every extraction
		for (Entity& l_Entity : m_Entities)
		{
			if (l_Entity.Material == id)
			{
				l_Entity.Material = MaterialId::Invalid;
			}
		}

		MarkRadianceChanged();

		return true;
	}

	Material* Scene::RestoreMaterial(Material material)
	{
		const uint64_t l_Id = std::to_underlying(material.Id);
		if (material.Id == MaterialId::Invalid || l_Id > k_MaxRestorableId || FindMaterial(material.Id) != nullptr)
		{
			return nullptr;
		}

		m_NextMaterialId = std::max(m_NextMaterialId, l_Id + 1);

		Material& l_Material = m_Materials.emplace_back(std::move(material));

		MarkRadianceChanged();

		return &l_Material;
	}

	Material* Scene::FindMaterial(MaterialId id)
	{
		const auto l_Found = std::find_if(m_Materials.begin(), m_Materials.end(), [id](const Material& material) { return material.Id == id; });

		return l_Found != m_Materials.end() ? &*l_Found : nullptr;
	}

	const Material* Scene::FindMaterial(MaterialId id) const
	{
		const auto l_Found = std::find_if(m_Materials.begin(), m_Materials.end(), [id](const Material& material) { return material.Id == id; });

		return l_Found != m_Materials.end() ? &*l_Found : nullptr;
	}

	uint32_t Scene::CountMaterialUsers(MaterialId id) const
	{
		return static_cast<uint32_t>(std::count_if(m_Entities.begin(), m_Entities.end(), [id](const Entity& entity) { return entity.Material == id; }));
	}

	void Scene::ReserveEntityIds(uint64_t nextId)
	{
		m_NextEntityId = std::max(m_NextEntityId, nextId);
	}

	void Scene::ReserveMaterialIds(uint64_t nextId)
	{
		m_NextMaterialId = std::max(m_NextMaterialId, nextId);
	}

	void Scene::MarkRadianceChanged()
	{
		m_RadianceRevision = NextRadianceRevision();
	}
}