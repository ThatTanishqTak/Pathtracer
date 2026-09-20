#pragma once

#include <filesystem>

namespace Sandbox
{
	struct SandboxOptions
	{
		std::filesystem::path ScenePath;
		std::filesystem::path AssetRoot;
	};

	bool ParseSandboxOptions(int argumentCount, char** arguments, SandboxOptions& options);
}