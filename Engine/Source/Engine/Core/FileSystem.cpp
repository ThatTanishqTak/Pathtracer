#include "Engine/Core/FileSystem.hpp"

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <ios>

namespace Engine
{
	namespace FileSystem
	{
		std::filesystem::path GetExecutableDirectory()
		{
			// SDL owns the returned string, it must not be freed
			const char* l_BasePath = SDL_GetBasePath();
			if (l_BasePath == nullptr)
			{
				PT_CORE_ERROR("Failed SDL_GetBasePath: {}", SDL_GetError());

				return std::filesystem::path{};
			}

			return std::filesystem::path(l_BasePath);
		}

		std::filesystem::path GetShaderDirectory()
		{
			return GetExecutableDirectory() / "Shaders";
		}

		bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::byte>& contents)
		{
			contents.clear();

			std::ifstream l_File(path, std::ios::binary | std::ios::ate);
			if (!l_File.is_open())
			{
				PT_CORE_ERROR("Failed to open file: {}", path.string());

				return false;
			}

			const std::streampos l_End = l_File.tellg();
			if (l_End < 0)
			{
				PT_CORE_ERROR("Failed to query the size of file: {}", path.string());

				return false;
			}

			contents.resize(static_cast<size_t>(l_End));

			l_File.seekg(0, std::ios::beg);
			if (!contents.empty() && !l_File.read(reinterpret_cast<char*>(contents.data()), static_cast<std::streamsize>(contents.size())))
			{
				PT_CORE_ERROR("Failed to read file: {}", path.string());

				contents.clear();

				return false;
			}

			return true;
		}
	}
}