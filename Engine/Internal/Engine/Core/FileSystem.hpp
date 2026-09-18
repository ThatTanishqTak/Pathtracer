#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <cstddef>
#include <filesystem>
#include <vector>

namespace Engine
{
	namespace FileSystem
	{
		std::filesystem::path GetExecutableDirectory();
		std::filesystem::path GetShaderDirectory();

		bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::byte>& contents);
	}
}