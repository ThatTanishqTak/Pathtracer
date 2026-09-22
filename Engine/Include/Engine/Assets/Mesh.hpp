#pragma once

#include "Engine/Math/Math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{
	// Stable mesh identity for the life of the process, handed out by the AssetManager. Never an index into a packed GPU array, and never stored in a scene file: the file stores the mesh's source and the manager hands the same source the same Id
	enum class MeshId : uint64_t
	{
		Invalid = 0,
	};

	// One vertex in object space, positions in metres. The normal is per vertex, so a vertex shared by neighbouring triangles shades smooth and one duplicated per face shades flat
	struct MeshVertex
	{
		Math::Vector3 Position{ 0.0f, 0.0f, 0.0f };
		Math::Vector3 Normal{ 0.0f, 0.0f, 1.0f }; // Unit length
		Math::Vector2 TexCoord{ 0.0f, 0.0f }; // Carried for the importer and Step 14's textures, nothing reads it yet
	};

	// Object-space axis-aligned bounds, both corners equal for an empty mesh
	struct MeshBounds
	{
		Math::Vector3 Min{ 0.0f, 0.0f, 0.0f };
		Math::Vector3 Max{ 0.0f, 0.0f, 0.0f };
	};

	// An indexed triangle list: every three indices are one triangle, wound counter-clockwise seen from outside. Immutable once the AssetManager holds it, entities reference it by Id and any number of them can share it
	struct Mesh
	{
		MeshId Id = MeshId::Invalid;
		std::string Name;
		std::string Source; // What the AssetManager loaded it from and what a scene file stores: "builtin:cube", "builtin:icosphere", later a path under the asset root

		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t> Indices;
		MeshBounds Bounds;

		uint32_t GetTriangleCount() const { return static_cast<uint32_t>(Indices.size() / 3); }
	};

	MeshBounds ComputeMeshBounds(const Mesh& mesh);

	// What the renderer and picking rely on without checking again: at least one triangle, an index count that is a multiple of three, every index inside the vertex array, finite positions and normals. False names the first problem
	bool ValidateMesh(const Mesh& mesh, std::string& error);
}