#include "Editor/Panels/SceneHierarchyPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <format>
#include <string>
#include <utility>

namespace Editor
{
	void DrawCreateMenuItems(EditorActions& actions)
	{
		if (ImGui::MenuItem("Sphere"))
		{
			actions.CreateEntity(CreateEntityKind::Sphere);
		}

		if (ImGui::MenuItem("Quad"))
		{
			actions.CreateEntity(CreateEntityKind::Quad);
		}

		if (ImGui::MenuItem("Cube"))
		{
			actions.CreateEntity(CreateEntityKind::Cube);
		}

		if (ImGui::MenuItem("Icosphere"))
		{
			actions.CreateEntity(CreateEntityKind::Icosphere);
		}

		if (ImGui::MenuItem("Area light"))
		{
			actions.CreateEntity(CreateEntityKind::AreaLight);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Player spawn at camera"))
		{
			actions.PlaceSpawnAtCamera();
		}
	}

	void SceneHierarchyPanel::Draw(const Engine::Scene& scene, Engine::EntityId& selectedEntity, EditorActions& actions, bool dirty, const std::filesystem::path& scenePath, const SceneFileStatus& fileStatus)
	{
		m_PendingKind = PendingKind::None;

		if (ImGui::Begin("Scene"))
		{
			ImGui::Text("%s%s", scene.GetName().empty() ? "<unnamed scene>" : scene.GetName().c_str(), dirty ? "*" : "");
			ImGui::TextDisabled("%s", scenePath.empty() ? "Not saved yet" : scenePath.string().c_str());
			ImGui::TextDisabled("%zu entities, %zu materials, revision %llu", scene.GetEntities().size(), scene.GetMaterials().size(), static_cast<unsigned long long>(scene.GetRadianceRevision()));

			DrawFileStatus(fileStatus);

			ImGui::Separator();

			// Before the list, so a create here never runs inside the loop below
			if (ImGui::Button("Create"))
			{
				ImGui::OpenPopup("CreateEntity");
			}

			if (ImGui::BeginPopup("CreateEntity"))
			{
				DrawCreateMenuItems(actions);
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			ImGui::TextDisabled("Right-click a row for rename, duplicate and delete");

			// The selection survives a reorder or a reload as long as the Id does, a deleted entity leaves nothing highlighted rather than the row that took its place
			bool l_SelectionFound = false;

			for (const Engine::Entity& l_Entity : scene.GetEntities())
			{
				l_SelectionFound = l_SelectionFound || l_Entity.Id == selectedEntity;

				DrawEntityRow(l_Entity, selectedEntity);
			}

			if (!l_SelectionFound)
			{
				selectedEntity = Engine::EntityId::Invalid;
			}

			// A rename target that vanished under the field
			if (m_RenamingEntity != Engine::EntityId::Invalid && scene.FindEntity(m_RenamingEntity) == nullptr)
			{
				m_RenamingEntity = Engine::EntityId::Invalid;
			}
		}

		ImGui::End();

		// The loop is over, the vector may move now
		switch (m_PendingKind)
		{
			case PendingKind::Rename:
			{
				actions.RenameEntity(m_PendingEntity, std::move(m_PendingName));
				break;
			}
			case PendingKind::Duplicate:
			{
				actions.DuplicateEntity(m_PendingEntity);
				break;
			}
			case PendingKind::Delete:
			{
				actions.DeleteEntity(m_PendingEntity);
				break;
			}
			default:
			{
				break;
			}
		}

		m_PendingKind = PendingKind::None;
		m_PendingName.clear();
	}

	void SceneHierarchyPanel::DrawFileStatus(const SceneFileStatus& fileStatus)
	{
		if (fileStatus.Summary.empty())
		{
			return;
		}

		if (fileStatus.Failed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			ImGui::TextWrapped("%s", fileStatus.Summary.c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::TextWrapped("%s", fileStatus.Summary.c_str());
		}

		if (!fileStatus.Error.empty())
		{
			ImGui::TextWrapped("%s", fileStatus.Error.c_str());
		}

		if (!fileStatus.Warnings.empty())
		{
			const std::string l_Label = std::format("{} warning(s)##FileWarnings", fileStatus.Warnings.size());
			if (ImGui::TreeNode(l_Label.c_str()))
			{
				for (const std::string& l_Warning : fileStatus.Warnings)
				{
					ImGui::BulletText("%s", l_Warning.c_str());
				}

				ImGui::TreePop();
			}
		}
	}

	void SceneHierarchyPanel::DrawEntityRow(const Engine::Entity& entity, Engine::EntityId& selectedEntity)
	{
		ImGui::PushID(static_cast<int>(std::to_underlying(entity.Id)));

		if (m_RenamingEntity == entity.Id)
		{
			// The field replaces the row until Enter commits or anything else, Escape or a click elsewhere, drops it
			if (m_RenameFocusPending)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocusPending = false;
			}

			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText("##Rename", m_RenameBuffer.data(), m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				if (m_RenameBuffer[0] != '\0')
				{
					m_PendingKind = PendingKind::Rename;
					m_PendingEntity = entity.Id;
					m_PendingName = m_RenameBuffer.data();
				}

				m_RenamingEntity = Engine::EntityId::Invalid;
			}
			else if (ImGui::IsItemDeactivated())
			{
				m_RenamingEntity = Engine::EntityId::Invalid;
			}

			ImGui::PopID();

			return;
		}

		const bool l_Selected = entity.Id == selectedEntity;

		// The label is keyed on the Id after the separator, so two entities with the same name stay two rows
		const std::string l_Label = std::format("{}##{}", entity.Name, std::to_underlying(entity.Id));

		if (!entity.Visible)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		if (ImGui::Selectable(l_Label.c_str(), l_Selected))
		{
			selectedEntity = entity.Id;
		}

		if (!entity.Visible)
		{
			ImGui::PopStyleColor();
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			BeginRename(entity);
		}

		// The right click selects as well, so the row the menu belongs to is the one the Inspector shows
		if (ImGui::BeginPopupContextItem())
		{
			selectedEntity = entity.Id;

			if (ImGui::MenuItem("Rename"))
			{
				BeginRename(entity);
			}

			if (ImGui::MenuItem("Duplicate"))
			{
				m_PendingKind = PendingKind::Duplicate;
				m_PendingEntity = entity.Id;
			}

			if (ImGui::MenuItem("Delete"))
			{
				m_PendingKind = PendingKind::Delete;
				m_PendingEntity = entity.Id;
			}

			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	void SceneHierarchyPanel::BeginRename(const Engine::Entity& entity)
	{
		m_RenamingEntity = entity.Id;
		m_RenameFocusPending = true;

		// Truncated to the buffer, the field shows what will be committed
		m_RenameBuffer.fill('\0');
		const size_t l_Length = std::min(entity.Name.size(), m_RenameBuffer.size() - 1);
		std::memcpy(m_RenameBuffer.data(), entity.Name.data(), l_Length);
	}
}