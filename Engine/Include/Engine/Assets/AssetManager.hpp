#pragma once

#include "Engine/Assets/Mesh.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine
{
	// Owns the loaded mesh assets for the life of the process and hands out their Ids. Owned by the client next to its Scene, the renderer only sees it through the render request. Holds no GPU handles
	class AssetManager
	{
	public:
		// Sources the manager generates itself rather than reads from disk
		static constexpr std::string_view k_BuiltinPrefix = "builtin:";
		static constexpr std::string_view k_CubeSource = "builtin:cube";
		static constexpr std::string_view k_IcosphereSource = "builtin:icosphere";

		// Relative file sources resolve against this, the built-in sources never touch it. The working directory is never consulted, so a relative source with no root set is refused
		void SetAssetRoot(std::filesystem::path root) { m_AssetRoot = std::move(root); }
		const std::filesystem::path& GetAssetRoot() const { return m_AssetRoot; }

		// Resolves a source to a mesh, loading it on first use, and returns its Id. The same source always returns the same Id. A source is a built-in name or a glTF path in UTF-8, relative to the asset root or absolute, imported through ImportGltfMesh. Invalid, with the reason in error when one is given, for an unknown built-in, a file that cannot be imported or a mesh that failed validation
		MeshId LoadMesh(std::string_view source, std::string* error = nullptr);

		// Invalid when the source was never loaded
		MeshId FindMesh(std::string_view source) const;

		// The pointer stays valid until the next LoadMesh call, keep the Id instead
		const Mesh* FindMesh(MeshId id) const;
		const std::vector<Mesh>& GetMeshes() const { return m_Meshes; }

		// Advances on every mesh added, so anything that cached the list knows it is stale
		uint64_t GetRevision() const { return m_Revision; }

	private:
		bool GenerateBuiltinMesh(std::string_view source, Mesh& mesh, std::string& error) const;
		bool ImportMeshFile(std::string_view source, Mesh& mesh, std::string& error) const;
		MeshId AddMesh(Mesh mesh, std::string& error);

		std::filesystem::path m_AssetRoot;

		std::vector<Mesh> m_Meshes;
		std::unordered_map<std::string, MeshId> m_MeshIds; // Source to Id, so a second load of the same source returns the first mesh

		uint64_t m_NextMeshId = 1;
		uint64_t m_Revision = 0;
	};
}