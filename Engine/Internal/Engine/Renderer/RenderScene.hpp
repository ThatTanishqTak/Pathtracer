#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Assets/Mesh.hpp"
#include "Engine/Math/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine
{
	class AssetManager;
	class Scene;

	// Values match the constants in Modules/SceneRecords.slang
	enum class RenderPrimitiveType : uint32_t
	{
		Sphere = 0, // Unit sphere at the object origin
		Quad = 1, // Unit square in the object XY plane, normal along +Z
		Mesh = 2, // TriangleCount triangles from FirstTriangle, in object space, inside BoundsMin to BoundsMax
	};

	// Must match PrimitiveRecord in Modules/SceneRecords.slang, std430 layout. The matrices are stored as four float4 columns so the shader never depends on a matrix layout convention
	struct RenderPrimitiveRecord
	{
		std::array<float, 16> ObjectToWorld{}; // Column-major, world = C0 * x + C1 * y + C2 * z + C3
		std::array<float, 16> WorldToObject{}; // Column-major inverse
		uint32_t Type = 0;
		uint32_t MaterialIndex = 0; // Index into RenderScene::Materials, 0 is the fallback material
		uint32_t EntityIdLow = 0; // The stable EntityId, split so picking can report which object a pixel shows
		uint32_t EntityIdHigh = 0;
		uint32_t FirstTriangle = 0; // Mesh only: the range in RenderScene::Triangles, zero triangles for the analytic shapes
		uint32_t TriangleCount = 0;
		uint32_t MeshIndex = 0; // Mesh only: index into RenderScene::Meshes, which BLAS the instance references. Still Padding0 in Modules/SceneRecords.slang, nothing on the GPU reads it
		uint32_t Padding1 = 0;
		std::array<float, 4> BoundsMin{}; // Mesh only: object-space bounds the shader tests before the triangle loop, w unused
		std::array<float, 4> BoundsMax{};
	};

	static_assert(sizeof(RenderPrimitiveRecord) == 192, "RenderPrimitiveRecord must match the 192 byte PrimitiveRecord in Modules/SceneRecords.slang");
	static_assert(offsetof(RenderPrimitiveRecord, ObjectToWorld) == 0, "ObjectToWorld must sit at std430 offset 0");
	static_assert(offsetof(RenderPrimitiveRecord, WorldToObject) == 64, "WorldToObject must sit at std430 offset 64");
	static_assert(offsetof(RenderPrimitiveRecord, Type) == 128, "Type must sit at std430 offset 128");
	static_assert(offsetof(RenderPrimitiveRecord, MaterialIndex) == 132, "MaterialIndex must sit at std430 offset 132");
	static_assert(offsetof(RenderPrimitiveRecord, EntityIdLow) == 136, "EntityIdLow must sit at std430 offset 136");
	static_assert(offsetof(RenderPrimitiveRecord, EntityIdHigh) == 140, "EntityIdHigh must sit at std430 offset 140");
	static_assert(offsetof(RenderPrimitiveRecord, FirstTriangle) == 144, "FirstTriangle must sit at std430 offset 144");
	static_assert(offsetof(RenderPrimitiveRecord, TriangleCount) == 148, "TriangleCount must sit at std430 offset 148");
	static_assert(offsetof(RenderPrimitiveRecord, MeshIndex) == 152, "MeshIndex must sit at std430 offset 152, the first padding word");
	static_assert(offsetof(RenderPrimitiveRecord, BoundsMin) == 160, "BoundsMin must sit at std430 offset 160");
	static_assert(offsetof(RenderPrimitiveRecord, BoundsMax) == 176, "BoundsMax must sit at std430 offset 176");

	// Must match MaterialRecord in Modules/SceneRecords.slang, std430 layout
	struct RenderMaterialRecord
	{
		std::array<float, 4> BaseColor{}; // Linear RGB, w unused
		std::array<float, 4> EmittedRadiance{}; // Linear RGB, w unused
		uint32_t Type = 0; // MaterialType
		uint32_t Padding0 = 0;
		uint32_t Padding1 = 0;
		uint32_t Padding2 = 0;
	};

	static_assert(sizeof(RenderMaterialRecord) == 48, "RenderMaterialRecord must match the 48 byte MaterialRecord in Modules/SceneRecords.slang");
	static_assert(offsetof(RenderMaterialRecord, BaseColor) == 0, "BaseColor must sit at std430 offset 0");
	static_assert(offsetof(RenderMaterialRecord, EmittedRadiance) == 16, "EmittedRadiance must sit at std430 offset 16");
	static_assert(offsetof(RenderMaterialRecord, Type) == 32, "Type must sit at std430 offset 32");

	// Must match VertexRecord in Modules/SceneRecords.slang, std430 layout. Object space, the texture coordinate rides in the two w components so a vertex is two float4 and nothing else
	struct RenderVertexRecord
	{
		std::array<float, 4> PositionU{}; // xyz position, w the texture coordinate's u
		std::array<float, 4> NormalV{}; // xyz unit normal, w the texture coordinate's v
	};

	static_assert(sizeof(RenderVertexRecord) == 32, "RenderVertexRecord must match the 32 byte VertexRecord in Modules/SceneRecords.slang");
	static_assert(offsetof(RenderVertexRecord, PositionU) == 0, "PositionU must sit at std430 offset 0");
	static_assert(offsetof(RenderVertexRecord, NormalV) == 16, "NormalV must sit at std430 offset 16");

	// Must match TriangleRecord in Modules/SceneRecords.slang, std430 layout. Indices into RenderScene::Vertices, already offset by the mesh's first vertex
	struct RenderTriangleRecord
	{
		uint32_t V0 = 0;
		uint32_t V1 = 0;
		uint32_t V2 = 0;
		uint32_t Padding0 = 0;
	};

	static_assert(sizeof(RenderTriangleRecord) == 16, "RenderTriangleRecord must match the 16 byte TriangleRecord in Modules/SceneRecords.slang");
	static_assert(offsetof(RenderTriangleRecord, V0) == 0, "V0 must sit at std430 offset 0");
	static_assert(offsetof(RenderTriangleRecord, V2) == 8, "V2 must sit at std430 offset 8");

	// Where one mesh landed in the packed arrays, CPU side only: every entity that shares the mesh points at this range, and the renderer builds one BLAS per entry
	struct RenderMeshRange
	{
		MeshId Id = MeshId::Invalid;
		uint32_t FirstTriangle = 0; // Into RenderScene::Triangles, and times three into RenderScene::Indices
		uint32_t TriangleCount = 0;
	};

	// The packed, upload-ready view of a Scene. Array positions are extraction details that change whenever the scene does, only the EntityIds inside the records are stable
	struct RenderScene
	{
		std::vector<RenderPrimitiveRecord> Primitives;
		std::vector<RenderMaterialRecord> Materials; // Never empty after a build, index 0 is the fallback material
		std::vector<RenderVertexRecord> Vertices; // Every mesh the primitives reference, appended once however many of them share it
		std::vector<RenderTriangleRecord> Triangles;
		std::vector<uint32_t> Indices; // The same triangles as three tightly packed indices each, the BLAS build input: a build cannot skip the triangle record's padding word
		std::vector<RenderMeshRange> Meshes; // In the order the meshes were appended, RenderPrimitiveRecord::MeshIndex points here

		Math::Vector3 EnvironmentRadiance{ 0.0f, 0.0f, 0.0f }; // Uploaded with the integrator constants, returned by every path that escapes the scene

		uint64_t Revision = 0; // The Scene::GetRadianceRevision the records were built from
	};

	// Rebuilds every record from the scene. Invisible entities, entities without geometry, degenerate transforms and mesh entities whose mesh the assets do not hold are skipped with a logged reason. Null assets skip every mesh entity
	void BuildRenderScene(const Scene& scene, const AssetManager* assets, RenderScene& renderScene);
}