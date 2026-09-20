#include "Sandbox/SandboxOptions.hpp"

#include "Engine/Engine.hpp"

#include <string_view>

namespace Sandbox
{
	namespace
	{
		void LogUsage()
		{
			PT_APP_INFO("Usage: Sandbox [--scene <path>] [<path>] [--assets <directory>]");
			PT_APP_INFO("  <path>       a .scene.json file to walk through, relative paths resolve against the asset root, without one the demo scene is built");
			PT_APP_INFO("  --assets     the asset root, defaults to the Assets directory next to the executable");
		}
	}

	bool ParseSandboxOptions(int argumentCount, char** arguments, SandboxOptions& options)
	{
		options = SandboxOptions{};

		for (int i_Argument = 1; i_Argument < argumentCount; ++i_Argument)
		{
			const std::string_view l_Argument = arguments[i_Argument] != nullptr ? arguments[i_Argument] : "";

			if (l_Argument == "--scene" || l_Argument == "--assets")
			{
				if (i_Argument + 1 >= argumentCount || arguments[i_Argument + 1] == nullptr)
				{
					PT_APP_ERROR("Option {} needs a value", l_Argument);
					LogUsage();

					return false;
				}

				std::filesystem::path& l_Target = l_Argument == "--scene" ? options.ScenePath : options.AssetRoot;
				if (!l_Target.empty())
				{
					PT_APP_ERROR("Option {} was given twice", l_Argument);
					LogUsage();

					return false;
				}

				l_Target = arguments[++i_Argument];

				continue;
			}

			if (l_Argument.starts_with("-"))
			{
				// A typo in an option must not silently fall through as a scene path
				PT_APP_ERROR("Unknown option {}", l_Argument);
				LogUsage();

				return false;
			}

			if (!options.ScenePath.empty())
			{
				PT_APP_ERROR("More than one scene path was given: {} and {}", options.ScenePath.string(), l_Argument);
				LogUsage();

				return false;
			}

			options.ScenePath = l_Argument;
		}

		return true;
	}
}