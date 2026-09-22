#pragma once

#include "Engine/Scene/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
	class AssetManager;

	struct SceneFileResult
	{
		bool Succeeded = false;
		std::string Error;
		std::vector<std::string> Warnings;
	};

	class SceneSerializer
	{
	public:
		SceneSerializer() = delete;

		static constexpr uint32_t k_SchemaVersion = 1;
		static constexpr std::string_view k_Extension = ".scene.json";

		// A mesh entity is written as its mesh's source and read back by loading that source through the assets, so the file never carries a process-local MeshId. A mesh that fails to load is a warning and the entity renders nothing, the rest of the scene still opens
		static SceneFileResult Save(const Scene& scene, const std::filesystem::path& path, const AssetManager& assets);
		static SceneFileResult Load(const std::filesystem::path& path, Scene& scene, AssetManager& assets);
		static SceneFileResult Write(const Scene& scene, std::string& text, const AssetManager& assets);
		static SceneFileResult Parse(std::string_view text, Scene& scene, AssetManager& assets, std::string_view sourceName = "<text>");
	};
}