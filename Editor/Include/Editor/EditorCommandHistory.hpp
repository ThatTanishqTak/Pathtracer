#pragma once

#include "Editor/EditorCommand.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Editor
{
	class EditorCommandHistory
	{
	public:
		void Execute(std::unique_ptr<EditorCommand> command, Engine::Scene& scene);
		void Record(std::unique_ptr<EditorCommand> command);

		bool Undo(Engine::Scene& scene);
		bool Redo(Engine::Scene& scene);

		bool CanUndo() const { return m_Cursor > 0; }
		bool CanRedo() const { return m_Cursor < m_Commands.size(); }

		std::string GetUndoName() const;
		std::string GetRedoName() const;

		void Clear();
		void MarkSaved();
		bool IsDirty() const;

		size_t GetUndoCount() const { return m_Cursor; }
		size_t GetRedoCount() const { return m_Commands.size() - m_Cursor; }

	private:
		void Push(std::unique_ptr<EditorCommand> command);

		static constexpr size_t k_MaxCommands = 256;

		std::vector<std::unique_ptr<EditorCommand>> m_Commands;
		size_t m_Cursor = 0;
		std::optional<size_t> m_SavedCursor = 0;
	};
}