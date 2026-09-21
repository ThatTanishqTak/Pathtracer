#include "Editor/Panels/InspectorPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace Editor
{
	namespace
	{
		// In enum order, the combo index is the enum value
		constexpr std::array<const char*, 3> k_GeometryTypeNames{ "None", "Sphere", "Quad" };
		constexpr std::array<const char*, 2> k_MaterialTypeNames{ "Diffuse", "Emissive" };

		constexpr float k_TranslationStep = 0.01f;
		constexpr float k_RotationStep = 0.5f;
		constexpr float k_ScaleStep = 0.01f;
		constexpr float k_SizeStep = 0.01f;
		constexpr float k_MinimumSize = 0.001f;
		constexpr float k_StrengthStep = 0.05f;

		constexpr float k_MaxSpawnPitchDegrees = 89.0f;

		void CopyToBuffer(const std::string& text, std::array<char, 256>& buffer)
		{
			buffer.fill('\0');
			const size_t l_Length = std::min(text.size(), buffer.size() - 1);
			std::memcpy(buffer.data(), text.data(), l_Length);
		}

		// One text field on a string: the buffer takes the current value each frame and the string takes the buffer back on a changed frame
		bool InputName(const char* label, std::string& text, std::array<char, 256>& buffer)
		{
			CopyToBuffer(text, buffer);

			if (ImGui::InputText(label, buffer.data(), buffer.size()))
			{
				text = buffer.data();

				return true;
			}

			return false;
		}
	}

	void InspectorPanel::Draw(Engine::Scene& scene, Engine::EntityId selectedEntity, EditorCommandHistory& history, EditorActions& actions)
	{
		if (ImGui::Begin("Inspector"))
		{
			// A new selection starts with fresh snapshots, an edit cannot span two entities
			if (selectedEntity != m_TrackedEntity)
			{
				m_TrackedEntity = selectedEntity;
				m_EntityEdit.InProgress = false;
				m_MaterialEdit.InProgress = false;
				m_EulerEditing = false;
			}

			Engine::Entity* l_Entity = scene.FindEntity(selectedEntity);
			if (l_Entity == nullptr)
			{
				DrawSceneSettings(scene, history, actions);
			}
			else
			{
				DrawEntity(scene, *l_Entity, history);
			}
		}

		ImGui::End();
	}

	void InspectorPanel::DrawEntity(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history)
	{
		if (!m_EntityEdit.InProgress)
		{
			m_EntityEdit.Before = entity;
		}

		ImGui::TextDisabled("Entity %llu", static_cast<unsigned long long>(std::to_underlying(entity.Id)));

		const bool l_Renamed = InputName("Name", entity.Name, m_NameBuffer);
		TrackEntityEdit(l_Renamed, false, false, scene, entity, history, "Rename");

		const bool l_VisibilityChanged = ImGui::Checkbox("Visible", &entity.Visible);
		TrackEntityEdit(l_VisibilityChanged, true, true, scene, entity, history, "Toggle visibility of");

		DrawTransform(scene, entity, history);
		DrawGeometry(scene, entity, history);
		DrawMaterial(scene, entity, history);
	}

	void InspectorPanel::DrawTransform(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history)
	{
		ImGui::SeparatorText("Transform");

		Engine::TransformComponent& l_Transform = entity.Transform;

		const bool l_Moved = ImGui::DragFloat3("Translation", &l_Transform.Translation.x, k_TranslationStep, 0.0f, 0.0f, "%.3f");
		TrackEntityEdit(l_Moved, false, true, scene, entity, history, "Move");

		if (!m_EulerEditing)
		{
			m_EulerDegrees = glm::degrees(glm::eulerAngles(Engine::GetSafeRotation(l_Transform)));
		}

		const bool l_Rotated = ImGui::DragFloat3("Rotation (deg)", &m_EulerDegrees.x, k_RotationStep, 0.0f, 0.0f, "%.1f");
		if (l_Rotated)
		{
			l_Transform.Rotation = glm::normalize(Engine::Math::Quaternion(glm::radians(m_EulerDegrees)));
		}

		m_EulerEditing = ImGui::IsItemActive();
		TrackEntityEdit(l_Rotated, false, true, scene, entity, history, "Rotate");

		const bool l_Scaled = ImGui::DragFloat3("Scale", &l_Transform.Scale.x, k_ScaleStep, 0.0f, 0.0f, "%.3f");
		TrackEntityEdit(l_Scaled, false, true, scene, entity, history, "Scale");

		const Engine::Math::Quaternion l_Rotation = Engine::GetSafeRotation(l_Transform);
		ImGui::TextDisabled("Quaternion  %.3f  %.3f  %.3f  %.3f", l_Rotation.x, l_Rotation.y, l_Rotation.z, l_Rotation.w);
	}

	void InspectorPanel::DrawGeometry(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history)
	{
		ImGui::SeparatorText("Geometry");

		Engine::GeometryComponent& l_Geometry = entity.Geometry;

		int l_Type = std::clamp(static_cast<int>(l_Geometry.Type), 0, static_cast<int>(k_GeometryTypeNames.size()) - 1);
		const bool l_TypeChanged = ImGui::Combo("Type", &l_Type, k_GeometryTypeNames.data(), static_cast<int>(k_GeometryTypeNames.size()));
		if (l_TypeChanged)
		{
			l_Geometry.Type = static_cast<Engine::GeometryType>(l_Type);
		}

		TrackEntityEdit(l_TypeChanged, true, true, scene, entity, history, "Change geometry of");

		switch (l_Geometry.Type)
		{
			case Engine::GeometryType::Sphere:
			{
				const bool l_Changed = ImGui::DragFloat("Radius", &l_Geometry.Radius, k_SizeStep, k_MinimumSize, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				TrackEntityEdit(l_Changed, false, true, scene, entity, history, "Resize");
				break;
			}
			case Engine::GeometryType::Quad:
			{
				// For an emissive quad this is the light's size: the geometry is the light, so the tracer sees the same change
				const bool l_WidthChanged = ImGui::DragFloat("Width", &l_Geometry.Width, k_SizeStep, k_MinimumSize, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				TrackEntityEdit(l_WidthChanged, false, true, scene, entity, history, "Resize");

				const bool l_HeightChanged = ImGui::DragFloat("Height", &l_Geometry.Height, k_SizeStep, k_MinimumSize, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
				TrackEntityEdit(l_HeightChanged, false, true, scene, entity, history, "Resize");
				break;
			}
			default:
			{
				ImGui::TextDisabled("Nothing to render");
				break;
			}
		}
	}

	void InspectorPanel::DrawMaterial(Engine::Scene& scene, Engine::Entity& entity, EditorCommandHistory& history)
	{
		ImGui::SeparatorText("Material");

		// Which material: a combo over the scene's materials plus none. Changing it is an entity edit, the material records stay as they are
		{
			const Engine::Material* l_Current = scene.FindMaterial(entity.Material);
			const std::string l_Preview = l_Current != nullptr ? l_Current->Name : (entity.Material == Engine::MaterialId::Invalid ? std::string("None") : std::string("Missing"));

			bool l_Changed = false;
			if (ImGui::BeginCombo("Uses", l_Preview.c_str()))
			{
				if (ImGui::Selectable("None", entity.Material == Engine::MaterialId::Invalid))
				{
					l_Changed = entity.Material != Engine::MaterialId::Invalid;
					entity.Material = Engine::MaterialId::Invalid;
				}

				for (const Engine::Material& l_Material : scene.GetMaterials())
				{
					const std::string l_Label = std::format("{}##{}", l_Material.Name, std::to_underlying(l_Material.Id));
					if (ImGui::Selectable(l_Label.c_str(), l_Material.Id == entity.Material))
					{
						l_Changed = entity.Material != l_Material.Id;
						entity.Material = l_Material.Id;
					}
				}

				ImGui::EndCombo();
			}

			TrackEntityEdit(l_Changed, true, true, scene, entity, history, "Change material of");
		}

		Engine::Material* l_Material = scene.FindMaterial(entity.Material);
		if (l_Material == nullptr)
		{
			// Invalid or deleted, the renderer draws it with the fallback material
			ImGui::TextDisabled(entity.Material == Engine::MaterialId::Invalid ? "No material, renders with the fallback" : "Missing material, renders with the fallback");

			m_MaterialEdit.InProgress = false;
			m_TrackedMaterial = Engine::MaterialId::Invalid;

			return;
		}

		if (l_Material->Id != m_TrackedMaterial)
		{
			m_TrackedMaterial = l_Material->Id;
			m_MaterialEdit.InProgress = false;
		}

		if (!m_MaterialEdit.InProgress)
		{
			m_MaterialEdit.Before = *l_Material;
		}

		ImGui::TextDisabled("Material %llu, %u user(s)", static_cast<unsigned long long>(std::to_underlying(l_Material->Id)), scene.CountMaterialUsers(l_Material->Id));

		const bool l_Renamed = InputName("Material name", l_Material->Name, m_NameBuffer);
		TrackMaterialEdit(l_Renamed, false, false, scene, *l_Material, history, "Rename material");

		int l_Type = std::clamp(static_cast<int>(l_Material->Type), 0, static_cast<int>(k_MaterialTypeNames.size()) - 1);
		const bool l_TypeChanged = ImGui::Combo("Material type", &l_Type, k_MaterialTypeNames.data(), static_cast<int>(k_MaterialTypeNames.size()));
		if (l_TypeChanged)
		{
			l_Material->Type = static_cast<Engine::MaterialType>(l_Type);
		}

		TrackMaterialEdit(l_TypeChanged, true, true, scene, *l_Material, history, "Change type of material");

		// Linear values, the picker shows them as they are
		const bool l_BaseChanged = ImGui::ColorEdit3("Base colour", &l_Material->BaseColor.x, ImGuiColorEditFlags_Float);
		TrackMaterialEdit(l_BaseChanged, false, true, scene, *l_Material, history, "Recolour material");

		if (l_Material->Type == Engine::MaterialType::Emissive)
		{
			const bool l_EmissionChanged = ImGui::ColorEdit3("Emission colour", &l_Material->EmissionColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			TrackMaterialEdit(l_EmissionChanged, false, true, scene, *l_Material, history, "Recolour emission of material");

			const bool l_StrengthChanged = ImGui::DragFloat("Emission strength", &l_Material->EmissionStrength, k_StrengthStep, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			TrackMaterialEdit(l_StrengthChanged, false, true, scene, *l_Material, history, "Change emission of material");

			// The one value the integrator sees, colour times strength
			const Engine::Math::Vector3 l_Radiance = Engine::GetEmittedRadiance(*l_Material);
			ImGui::TextDisabled("Emitted radiance  %.3f  %.3f  %.3f", l_Radiance.x, l_Radiance.y, l_Radiance.z);
		}
	}

	void InspectorPanel::DrawSceneSettings(Engine::Scene& scene, EditorCommandHistory& history, EditorActions& actions)
	{
		ImGui::TextDisabled("Nothing selected, scene settings");

		if (!m_SceneEdit.InProgress)
		{
			m_SceneEdit.Before = GetSceneSettings(scene);
		}

		{
			std::string l_Name = scene.GetName();
			const bool l_Renamed = InputName("Scene name", l_Name, m_NameBuffer);
			if (l_Renamed)
			{
				scene.SetName(l_Name);
			}

			TrackSceneEdit(l_Renamed, false, false, scene, history, "Rename scene");
		}

		ImGui::SeparatorText("Player spawn");

		Engine::PlayerSpawn& l_Spawn = scene.GetPlayerSpawn();

		const bool l_SpawnMoved = ImGui::DragFloat3("Spawn position", &l_Spawn.Position.x, k_TranslationStep, 0.0f, 0.0f, "%.3f");
		TrackSceneEdit(l_SpawnMoved, false, false, scene, history, "Move spawn");

		// The spawn never rolls, so yaw and pitch are the whole orientation and the round trip is exact
		if (!m_SpawnEditing)
		{
			const Engine::YawPitch l_Angles = Engine::YawPitchFromOrientation(l_Spawn.Orientation);
			m_SpawnDegrees = Engine::Math::Vector2(Engine::Math::ToDegrees(l_Angles.Yaw), Engine::Math::ToDegrees(l_Angles.Pitch));
		}

		const bool l_SpawnTurned = ImGui::DragFloat2("Spawn yaw, pitch (deg)", &m_SpawnDegrees.x, k_RotationStep, 0.0f, 0.0f, "%.1f");
		if (l_SpawnTurned)
		{
			m_SpawnDegrees.y = std::clamp(m_SpawnDegrees.y, -k_MaxSpawnPitchDegrees, k_MaxSpawnPitchDegrees);
			l_Spawn.Orientation = Engine::OrientationFromYawPitch(Engine::YawPitch{ Engine::Math::ToRadians(m_SpawnDegrees.x), Engine::Math::ToRadians(m_SpawnDegrees.y) });
		}

		m_SpawnEditing = ImGui::IsItemActive();
		TrackSceneEdit(l_SpawnTurned, false, false, scene, history, "Turn spawn");

		if (ImGui::Button("Place spawn at camera"))
		{
			// A command of the client's, the snapshot above is simply re-taken next frame
			actions.PlaceSpawnAtCamera();
		}

		ImGui::SeparatorText("Environment");

		const bool l_EnvironmentChanged = ImGui::ColorEdit3("Radiance", &scene.GetEnvironment().Radiance.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		TrackSceneEdit(l_EnvironmentChanged, false, true, scene, history, "Change environment");
	}

	void InspectorPanel::TrackEntityEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, const Engine::Entity& entity, EditorCommandHistory& history, const char* what)
	{
		if (changed)
		{
			if (radiance)
			{
				scene.MarkRadianceChanged();
			}

			m_EntityEdit.InProgress = true;
		}

		// Only the widget that changed may commit at once, an earlier text field still being typed in must not be cut short by a checkbox further down
		if (m_EntityEdit.InProgress && ((changed && commitNow) || ImGui::IsItemDeactivatedAfterEdit()))
		{
			history.Record(std::make_unique<EditEntityCommand>(std::format("{} '{}'", what, m_EntityEdit.Before.Name), m_EntityEdit.Before, entity));
			m_EntityEdit.InProgress = false;
		}
	}

	void InspectorPanel::TrackMaterialEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, const Engine::Material& material, EditorCommandHistory& history, const char* what)
	{
		if (changed)
		{
			if (radiance)
			{
				scene.MarkRadianceChanged();
			}

			m_MaterialEdit.InProgress = true;
		}

		if (m_MaterialEdit.InProgress && ((changed && commitNow) || ImGui::IsItemDeactivatedAfterEdit()))
		{
			history.Record(std::make_unique<EditMaterialCommand>(std::format("{} '{}'", what, m_MaterialEdit.Before.Name), m_MaterialEdit.Before, material));
			m_MaterialEdit.InProgress = false;
		}
	}

	void InspectorPanel::TrackSceneEdit(bool changed, bool commitNow, bool radiance, Engine::Scene& scene, EditorCommandHistory& history, const char* what)
	{
		if (changed)
		{
			if (radiance)
			{
				scene.MarkRadianceChanged();
			}

			m_SceneEdit.InProgress = true;
		}

		if (m_SceneEdit.InProgress && ((changed && commitNow) || ImGui::IsItemDeactivatedAfterEdit()))
		{
			history.Record(std::make_unique<EditSceneSettingsCommand>(what, m_SceneEdit.Before, GetSceneSettings(scene)));
			m_SceneEdit.InProgress = false;
		}
	}
}