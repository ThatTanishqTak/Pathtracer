#pragma once

#include "Engine/Scene/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
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

		static SceneFileResult Save(const Scene& scene, const std::filesystem::path& path);
		static SceneFileResult Load(const std::filesystem::path& path, Scene& scene);
		static SceneFileResult Write(const Scene& scene, std::string& text);
		static SceneFileResult Parse(std::string_view text, Scene& scene, std::string_view sourceName = "<text>");
	};
}