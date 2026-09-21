#pragma once

#include "Editor/EditorCommand.hpp"

#include <memory>
#include <optional>
#include <string>

namespace Editor
{
	// The scene-wide values that are not entities or materials, edited as one unit
	struct SceneSettings
	{
		std::string Name;
		Engine::PlayerSpawn Spawn;
		Engine::Environment Environment;
	};

	SceneSettings GetSceneSettings(const Engine::Scene& scene);

	// Whether the renderer would see a difference, so a rename or a moved spawn never resets the accumulation
	bool AffectsRadiance(const Engine::Entity& before, const Engine::Entity& after);
	bool AffectsRadiance(const Engine::Material& before, const Engine::Material& after);
	bool AffectsRadiance(const SceneSettings& before, const SceneSettings& after);

	// Adds an entity and, when given, the material it uses. The first Execute hands out the Ids, every later one restores the same ones, so a redo brings back exactly what the undo removed and later commands that captured those Ids stay valid
	class CreateEntityCommand final : public EditorCommand
	{
	public:
		CreateEntityCommand(std::string name, Engine::Entity entity, std::optional<Engine::Material> material);

		const std::string& GetName() const override { return m_Name; }

		void Execute(Engine::Scene& scene) override;
		void Undo(Engine::Scene& scene) override;

		// Invalid until the first Execute
		Engine::EntityId GetEntityId() const { return m_Entity.Id; }

	private:
		std::string m_Name;
		Engine::Entity m_Entity;
		std::optional<Engine::Material> m_Material;
	};

	// Removes an entity, and its material when nothing else uses it. Undo restores both with their original Ids, at the end of the list
	class DeleteEntityCommand final : public EditorCommand
	{
	public:
		DeleteEntityCommand(const Engine::Scene& scene, Engine::EntityId id);

		const std::string& GetName() const override { return m_Name; }

		void Execute(Engine::Scene& scene) override;
		void Undo(Engine::Scene& scene) override;

	private:
		std::string m_Name;
		Engine::Entity m_Entity;
		std::optional<Engine::Material> m_Material;
	};

	// Replaces every field of one entity, the Id stays. Rename, transform, geometry, material choice and visibility all go through here
	class EditEntityCommand final : public EditorCommand
	{
	public:
		EditEntityCommand(std::string name, Engine::Entity before, Engine::Entity after);

		const std::string& GetName() const override { return m_Name; }

		void Execute(Engine::Scene& scene) override;
		void Undo(Engine::Scene& scene) override;

	private:
		static void Apply(Engine::Scene& scene, const Engine::Entity& from, const Engine::Entity& to);

		std::string m_Name;
		Engine::Entity m_Before;
		Engine::Entity m_After;
	};

	// The material twin of EditEntityCommand. A shared material changes for every entity that uses it
	class EditMaterialCommand final : public EditorCommand
	{
	public:
		EditMaterialCommand(std::string name, Engine::Material before, Engine::Material after);

		const std::string& GetName() const override { return m_Name; }

		void Execute(Engine::Scene& scene) override;
		void Undo(Engine::Scene& scene) override;

	private:
		static void Apply(Engine::Scene& scene, const Engine::Material& from, const Engine::Material& to);

		std::string m_Name;
		Engine::Material m_Before;
		Engine::Material m_After;
	};

	// Scene name, player spawn and environment
	class EditSceneSettingsCommand final : public EditorCommand
	{
	public:
		EditSceneSettingsCommand(std::string name, SceneSettings before, SceneSettings after);

		const std::string& GetName() const override { return m_Name; }

		void Execute(Engine::Scene& scene) override;
		void Undo(Engine::Scene& scene) override;

	private:
		static void Apply(Engine::Scene& scene, const SceneSettings& from, const SceneSettings& to);

		std::string m_Name;
		SceneSettings m_Before;
		SceneSettings m_After;
	};

	// A copy of an existing entity and, when it has one, of its material, both under new Ids. Null when the entity does not exist
	std::unique_ptr<CreateEntityCommand> MakeDuplicateCommand(const Engine::Scene& scene, Engine::EntityId id);
}