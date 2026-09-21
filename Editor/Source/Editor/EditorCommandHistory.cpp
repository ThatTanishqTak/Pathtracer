#include "Editor/EditorCommandHistory.hpp"

#include <utility>

namespace Editor
{
	void EditorCommandHistory::Execute(std::unique_ptr<EditorCommand> command, Engine::Scene& scene)
	{
		if (!command)
		{
			return;
		}

		command->Execute(scene);
		Push(std::move(command));
	}

	void EditorCommandHistory::Record(std::unique_ptr<EditorCommand> command)
	{
		if (!command)
		{
			return;
		}

		Push(std::move(command));
	}

	void EditorCommandHistory::Push(std::unique_ptr<EditorCommand> command)
	{
		if (m_SavedCursor && *m_SavedCursor > m_Cursor)
		{
			m_SavedCursor.reset();
		}

		m_Commands.erase(m_Commands.begin() + static_cast<std::ptrdiff_t>(m_Cursor), m_Commands.end());

		PT_APP_TRACE("Command: {}", command->GetName());

		m_Commands.push_back(std::move(command));
		m_Cursor = m_Commands.size();

		if (m_Commands.size() > k_MaxCommands)
		{
			m_Commands.erase(m_Commands.begin());
			m_Cursor -= 1;

			if (m_SavedCursor)
			{
				if (*m_SavedCursor == 0)
				{
					m_SavedCursor.reset();
				}
				else
				{
					*m_SavedCursor -= 1;
				}
			}
		}
	}

	bool EditorCommandHistory::Undo(Engine::Scene& scene)
	{
		if (!CanUndo())
		{
			return false;
		}

		m_Cursor -= 1;
		m_Commands[m_Cursor]->Undo(scene);

		PT_APP_TRACE("Undo: {}", m_Commands[m_Cursor]->GetName());

		return true;
	}

	bool EditorCommandHistory::Redo(Engine::Scene& scene)
	{
		if (!CanRedo())
		{
			return false;
		}

		m_Commands[m_Cursor]->Execute(scene);

		PT_APP_TRACE("Redo: {}", m_Commands[m_Cursor]->GetName());

		m_Cursor += 1;

		return true;
	}

	std::string EditorCommandHistory::GetUndoName() const
	{
		return CanUndo() ? m_Commands[m_Cursor - 1]->GetName() : std::string{};
	}

	std::string EditorCommandHistory::GetRedoName() const
	{
		return CanRedo() ? m_Commands[m_Cursor]->GetName() : std::string{};
	}

	void EditorCommandHistory::Clear()
	{
		m_Commands.clear();
		m_Cursor = 0;
		m_SavedCursor = 0;
	}

	void EditorCommandHistory::MarkSaved()
	{
		m_SavedCursor = m_Cursor;
	}

	bool EditorCommandHistory::IsDirty() const
	{
		return !(m_SavedCursor && *m_SavedCursor == m_Cursor);
	}
}