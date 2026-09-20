#include "Editor/Panels/SceneHierarchyPanel.hpp"

#include <imgui.h>

#include <format>
#include <string>
#include <utility>

namespace Editor
{
	void SceneHierarchyPanel::Draw(const Engine::Scene& scene, Engine::EntityId& selectedEntity)
	{
		if (ImGui::Begin("Scene"))
		{
			ImGui::TextUnformatted(scene.GetName().empty() ? "<unnamed scene>" : scene.GetName().c_str());
			ImGui::TextDisabled("%zu entities, %zu materials, revision %llu", scene.GetEntities().size(), scene.GetMaterials().size(), static_cast<unsigned long long>(scene.GetRadianceRevision()));
			ImGui::Separator();

			// The selection survives a reorder or a reload as long as the Id does, a deleted entity leaves nothing highlighted rather than the row that took its place
			bool l_SelectionFound = false;

			for (const Engine::Entity& l_Entity : scene.GetEntities())
			{
				const bool l_Selected = l_Entity.Id == selectedEntity;
				l_SelectionFound = l_SelectionFound || l_Selected;

				// The label is keyed on the Id after the separator, so two entities with the same name stay two rows
				const std::string l_Label = std::format("{}##{}", l_Entity.Name, std::to_underlying(l_Entity.Id));

				if (!l_Entity.Visible)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				}

				if (ImGui::Selectable(l_Label.c_str(), l_Selected))
				{
					selectedEntity = l_Entity.Id;
				}

				if (!l_Entity.Visible)
				{
					ImGui::PopStyleColor();
				}
			}

			if (!l_SelectionFound)
			{
				selectedEntity = Engine::EntityId::Invalid;
			}
		}

		ImGui::End();
	}
}