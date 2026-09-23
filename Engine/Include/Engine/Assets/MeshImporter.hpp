#pragma once

#include "Engine/Assets/Mesh.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Engine
{
	bool ImportGltfMesh(const std::filesystem::path& path, Mesh& mesh, std::string& error);
	bool ImportGltfMeshFromMemory(std::span<const std::byte> document, std::string_view name, Mesh& mesh, std::string& error);
}