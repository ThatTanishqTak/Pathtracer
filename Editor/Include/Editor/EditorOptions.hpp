#pragma once

#include <filesystem>

namespace Editor
{
	struct EditorOptions
	{
		std::filesystem::path ScenePath;
		std::filesystem::path AssetRoot;
	};

	bool ParseEditorOptions(int argumentCount, char** arguments, EditorOptions& options);
}