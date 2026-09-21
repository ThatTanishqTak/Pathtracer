#pragma once

#include "Engine/Engine.hpp"

#include <string>

namespace Editor
{
	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		virtual const std::string& GetName() const = 0;

		virtual void Execute(Engine::Scene& scene) = 0;
		virtual void Undo(Engine::Scene& scene) = 0;
	};
}