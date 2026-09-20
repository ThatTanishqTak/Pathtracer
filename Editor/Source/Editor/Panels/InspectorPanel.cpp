#include "Editor/Panels/InspectorPanel.hpp"

#include <imgui.h>

#include <utility>

namespace Editor
{
	namespace
	{
		const char* GeometryTypeName(Engine::GeometryType type)
		{
			switch (type)
			{
				case Engine::GeometryType::None:
				{
					return "None";
				}
				case Engine::GeometryType::Sphere:
				{
					return "Sphere";
				}
				case Engine::GeometryType::Quad:
				{
					return "Quad";
				}
				default:
				{
					return "Unknown";
				}
			}
		}

		const char* MaterialTypeName(Engine::MaterialType type)
		{
			switch (type)
			{
				case Engine::MaterialType::Diffuse:
				{
					return "Diffuse";
				}
				case Engine::MaterialType::Emissive:
				{
					return "Emissive";
				}
				default:
				{
					return "Unknown";
				}
			}
		}

		// Read-only fields: the widgets show a copy so nothing written into them reaches the scene, Step 11 replaces the copies with commands
		void ReadOnlyVector3(const char* label, const Engine::Math::Vector3& value)
		{
			float l_Values[3] = { value.x, value.y, value.z };
			ImGui::InputFloat3(label, l_Values, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}

		void ReadOnlyFloat(const char* label, float value)
		{
			float l_Value = value;
			ImGui::InputFloat(label, &l_Value, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}

		// A colour square with the linear values beside it, no picker and no drag source so the square cannot be edited or dragged
		void ReadOnlyColor(const char* label, const Engine::Math::Vector3& value)
		{
			ImGui::ColorButton(label, ImVec4(value.x, value.y, value.z, 1.0f), ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_Float);
			ImGui::SameLine();
			ImGui::Text("%s  %.3f  %.3f  %.3f", label, value.x, value.y, value.z);
		}
	}

	void InspectorPanel::Draw(const Engine::Scene& scene, Engine::EntityId selectedEntity)
	{
		if (ImGui::Begin("Inspector"))
		{
			const Engine::Entity* l_Entity = scene.FindEntity(selectedEntity);
			if (l_Entity == nullptr)
			{
				ImGui::TextDisabled("Nothing selected");
			}
			else
			{
				ImGui::TextUnformatted(l_Entity->Name.c_str());
				ImGui::TextDisabled("Entity %llu", static_cast<unsigned long long>(std::to_underlying(l_Entity->Id)));

				bool l_Visible = l_Entity->Visible;
				ImGui::BeginDisabled();
				ImGui::Checkbox("Visible", &l_Visible);
				ImGui::EndDisabled();

				DrawTransform(l_Entity->Transform);
				DrawGeometry(l_Entity->Geometry);
				DrawMaterial(scene, l_Entity->Material);
			}
		}

		ImGui::End();
	}

	void InspectorPanel::DrawTransform(const Engine::TransformComponent& transform)
	{
		ImGui::SeparatorText("Transform");

		ReadOnlyVector3("Translation", transform.Translation);

		// The quaternion is what the file and the renderer use, the degrees are a reading aid derived from it
		const Engine::Math::Quaternion l_Rotation = Engine::GetSafeRotation(transform);
		const Engine::Math::Vector3 l_Euler = glm::degrees(glm::eulerAngles(l_Rotation));
		ReadOnlyVector3("Rotation (deg)", l_Euler);

		float l_Quaternion[4] = { l_Rotation.x, l_Rotation.y, l_Rotation.z, l_Rotation.w };
		ImGui::InputFloat4("Rotation (xyzw)", l_Quaternion, "%.3f", ImGuiInputTextFlags_ReadOnly);

		ReadOnlyVector3("Scale", transform.Scale);
	}

	void InspectorPanel::DrawGeometry(const Engine::GeometryComponent& geometry)
	{
		ImGui::SeparatorText("Geometry");

		ImGui::Text("Type: %s", GeometryTypeName(geometry.Type));

		switch (geometry.Type)
		{
			case Engine::GeometryType::Sphere:
			{
				ReadOnlyFloat("Radius", geometry.Radius);
				break;
			}
			case Engine::GeometryType::Quad:
			{
				ReadOnlyFloat("Width", geometry.Width);
				ReadOnlyFloat("Height", geometry.Height);
				break;
			}
			default:
			{
				ImGui::TextDisabled("Nothing to render");
				break;
			}
		}
	}

	void InspectorPanel::DrawMaterial(const Engine::Scene& scene, Engine::MaterialId materialId)
	{
		ImGui::SeparatorText("Material");

		const Engine::Material* l_Material = scene.FindMaterial(materialId);
		if (l_Material == nullptr)
		{
			// Invalid or deleted, the renderer draws it with the fallback material
			ImGui::TextDisabled(materialId == Engine::MaterialId::Invalid ? "No material, renders with the fallback" : "Missing material, renders with the fallback");

			return;
		}

		ImGui::TextUnformatted(l_Material->Name.c_str());
		ImGui::TextDisabled("Material %llu, %u user(s)", static_cast<unsigned long long>(std::to_underlying(l_Material->Id)), scene.CountMaterialUsers(l_Material->Id));
		ImGui::Text("Type: %s", MaterialTypeName(l_Material->Type));

		ReadOnlyColor("Base colour", l_Material->BaseColor);

		if (l_Material->Type == Engine::MaterialType::Emissive)
		{
			ReadOnlyColor("Emission colour", l_Material->EmissionColor);
			ReadOnlyFloat("Emission strength", l_Material->EmissionStrength);

			// The one value the integrator sees, colour times strength
			ReadOnlyVector3("Emitted radiance", Engine::GetEmittedRadiance(*l_Material));
		}
	}
}