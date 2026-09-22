#include "Editor/EditorCommands.hpp"

#include <format>
#include <utility>

namespace Editor
{
	namespace
	{
		bool SameTransform(const Engine::TransformComponent& a, const Engine::TransformComponent& b)
		{
			return a.Translation == b.Translation && a.Rotation == b.Rotation && a.Scale == b.Scale;
		}

		bool SameGeometry(const Engine::GeometryComponent& a, const Engine::GeometryComponent& b)
		{
			return a.Type == b.Type && a.Radius == b.Radius && a.Width == b.Width && a.Height == b.Height && a.Mesh == b.Mesh;
		}
	}

	SceneSettings GetSceneSettings(const Engine::Scene& scene)
	{
		return SceneSettings{ scene.GetName(), scene.GetPlayerSpawn(), scene.GetEnvironment() };
	}

	bool AffectsRadiance(const Engine::Entity& before, const Engine::Entity& after)
	{
		// The name is the one field the renderer never reads
		return before.Visible != after.Visible || before.Material != after.Material || !SameTransform(before.Transform, after.Transform) || !SameGeometry(before.Geometry, after.Geometry);
	}

	bool AffectsRadiance(const Engine::Material& before, const Engine::Material& after)
	{
		return before.Type != after.Type || before.BaseColor != after.BaseColor || before.EmissionColor != after.EmissionColor || before.EmissionStrength != after.EmissionStrength;
	}

	bool AffectsRadiance(const SceneSettings& before, const SceneSettings& after)
	{
		// The spawn is where the Sandbox starts, the tracer never looks at it
		return before.Environment.Radiance != after.Environment.Radiance;
	}

	// CreateEntityCommand --------------
	CreateEntityCommand::CreateEntityCommand(std::string name, Engine::Entity entity, std::optional<Engine::Material> material) : m_Name(std::move(name)), m_Entity(std::move(entity)), m_Material(std::move(material))
	{

	}

	void CreateEntityCommand::Execute(Engine::Scene& scene)
	{
		if (m_Material)
		{
			if (m_Material->Id == Engine::MaterialId::Invalid)
			{
				// First time: the scene hands out the Id, the command keeps it for every redo
				Engine::Material& l_Created = scene.CreateMaterial(m_Material->Name);
				m_Material->Id = l_Created.Id;
				l_Created = *m_Material;
			}
			else if (scene.RestoreMaterial(*m_Material) == nullptr)
			{
				PT_APP_ERROR("Cannot restore material '{}' ({}), its Id is already in use", m_Material->Name, std::to_underlying(m_Material->Id));
			}

			m_Entity.Material = m_Material->Id;
		}

		if (m_Entity.Id == Engine::EntityId::Invalid)
		{
			Engine::Entity& l_Created = scene.CreateEntity(m_Entity.Name);
			m_Entity.Id = l_Created.Id;
			l_Created = m_Entity;
		}
		else if (scene.RestoreEntity(m_Entity) == nullptr)
		{
			PT_APP_ERROR("Cannot restore entity '{}' ({}), its Id is already in use", m_Entity.Name, std::to_underlying(m_Entity.Id));
		}

		// The fields were copied in after Create advanced the revision
		scene.MarkRadianceChanged();
	}

	void CreateEntityCommand::Undo(Engine::Scene& scene)
	{
		scene.DestroyEntity(m_Entity.Id);

		if (m_Material)
		{
			scene.DestroyMaterial(m_Material->Id);
		}
	}

	// DeleteEntityCommand --------------
	DeleteEntityCommand::DeleteEntityCommand(const Engine::Scene& scene, Engine::EntityId id)
	{
		// Everything undo needs is copied now, while it still exists
		const Engine::Entity* l_Entity = scene.FindEntity(id);
		if (l_Entity != nullptr)
		{
			m_Entity = *l_Entity;

			// The material goes with its last user, a shared one stays for the others
			const Engine::Material* l_Material = scene.FindMaterial(l_Entity->Material);
			if (l_Material != nullptr && scene.CountMaterialUsers(l_Material->Id) == 1)
			{
				m_Material = *l_Material;
			}
		}

		m_Name = std::format("Delete '{}'", m_Entity.Name);
	}

	void DeleteEntityCommand::Execute(Engine::Scene& scene)
	{
		if (m_Entity.Id == Engine::EntityId::Invalid)
		{
			return;
		}

		scene.DestroyEntity(m_Entity.Id);

		if (m_Material)
		{
			scene.DestroyMaterial(m_Material->Id);
		}
	}

	void DeleteEntityCommand::Undo(Engine::Scene& scene)
	{
		if (m_Entity.Id == Engine::EntityId::Invalid)
		{
			return;
		}

		// The material first, so the entity's reference resolves as soon as it is back
		if (m_Material && scene.RestoreMaterial(*m_Material) == nullptr)
		{
			PT_APP_ERROR("Cannot restore material '{}' ({}), its Id is already in use", m_Material->Name, std::to_underlying(m_Material->Id));
		}

		if (scene.RestoreEntity(m_Entity) == nullptr)
		{
			PT_APP_ERROR("Cannot restore entity '{}' ({}), its Id is already in use", m_Entity.Name, std::to_underlying(m_Entity.Id));
		}
	}

	// EditEntityCommand --------------
	EditEntityCommand::EditEntityCommand(std::string name, Engine::Entity before, Engine::Entity after) : m_Name(std::move(name)), m_Before(std::move(before)), m_After(std::move(after))
	{

	}

	void EditEntityCommand::Execute(Engine::Scene& scene)
	{
		Apply(scene, m_Before, m_After);
	}

	void EditEntityCommand::Undo(Engine::Scene& scene)
	{
		Apply(scene, m_After, m_Before);
	}

	void EditEntityCommand::Apply(Engine::Scene& scene, const Engine::Entity& from, const Engine::Entity& to)
	{
		Engine::Entity* l_Entity = scene.FindEntity(to.Id);
		if (l_Entity == nullptr)
		{
			PT_APP_ERROR("Cannot edit entity '{}' ({}), it no longer exists", to.Name, std::to_underlying(to.Id));

			return;
		}

		*l_Entity = to;

		if (AffectsRadiance(from, to))
		{
			scene.MarkRadianceChanged();
		}
	}

	// EditMaterialCommand --------------
	EditMaterialCommand::EditMaterialCommand(std::string name, Engine::Material before, Engine::Material after) : m_Name(std::move(name)), m_Before(std::move(before)), m_After(std::move(after))
	{

	}

	void EditMaterialCommand::Execute(Engine::Scene& scene)
	{
		Apply(scene, m_Before, m_After);
	}

	void EditMaterialCommand::Undo(Engine::Scene& scene)
	{
		Apply(scene, m_After, m_Before);
	}

	void EditMaterialCommand::Apply(Engine::Scene& scene, const Engine::Material& from, const Engine::Material& to)
	{
		Engine::Material* l_Material = scene.FindMaterial(to.Id);
		if (l_Material == nullptr)
		{
			PT_APP_ERROR("Cannot edit material '{}' ({}), it no longer exists", to.Name, std::to_underlying(to.Id));

			return;
		}

		*l_Material = to;

		if (AffectsRadiance(from, to))
		{
			scene.MarkRadianceChanged();
		}
	}

	// EditSceneSettingsCommand --------------
	EditSceneSettingsCommand::EditSceneSettingsCommand(std::string name, SceneSettings before, SceneSettings after) : m_Name(std::move(name)), m_Before(std::move(before)), m_After(std::move(after))
	{

	}

	void EditSceneSettingsCommand::Execute(Engine::Scene& scene)
	{
		Apply(scene, m_Before, m_After);
	}

	void EditSceneSettingsCommand::Undo(Engine::Scene& scene)
	{
		Apply(scene, m_After, m_Before);
	}

	void EditSceneSettingsCommand::Apply(Engine::Scene& scene, const SceneSettings& from, const SceneSettings& to)
	{
		scene.SetName(to.Name);
		scene.GetPlayerSpawn() = to.Spawn;
		scene.GetEnvironment() = to.Environment;

		if (AffectsRadiance(from, to))
		{
			scene.MarkRadianceChanged();
		}
	}

	// Duplicate --------------
	std::unique_ptr<CreateEntityCommand> MakeDuplicateCommand(const Engine::Scene& scene, Engine::EntityId id)
	{
		const Engine::Entity* l_Source = scene.FindEntity(id);
		if (l_Source == nullptr)
		{
			return nullptr;
		}

		Engine::Entity l_Entity = *l_Source;
		l_Entity.Id = Engine::EntityId::Invalid;
		l_Entity.Name = std::format("{} copy", l_Source->Name);

		// Its own material, so recolouring the copy never changes the original. A missing or absent material stays as it was, the fallback renders both
		std::optional<Engine::Material> l_Material;
		if (const Engine::Material* l_SourceMaterial = scene.FindMaterial(l_Source->Material); l_SourceMaterial != nullptr)
		{
			l_Material = *l_SourceMaterial;
			l_Material->Id = Engine::MaterialId::Invalid;
			l_Material->Name = std::format("{} copy", l_SourceMaterial->Name);
		}

		return std::make_unique<CreateEntityCommand>(std::format("Duplicate '{}'", l_Source->Name), std::move(l_Entity), std::move(l_Material));
	}
}