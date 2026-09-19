#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Math/Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Engine
{
	class Scene;

	// Values match the constants in Diagnostic.slang
	enum class RenderPrimitiveType : uint32_t
	{
		Sphere = 0, // Unit sphere at the object origin
		Quad = 1, // Unit square in the object XY plane, normal along +Z
	};

	// Must match PrimitiveRecord in Diagnostic.slang, std430 layout. The matrices are stored as four float4 columns so the shader never depends on a matrix layout convention
	struct RenderPrimitiveRecord
	{
		std::array<float, 16> ObjectToWorld{}; // Column-major, world = C0 * x + C1 * y + C2 * z + C3
		std::array<float, 16> WorldToObject{}; // Column-major inverse
		uint32_t Type = 0;
		uint32_t MaterialIndex = 0; // Index into RenderScene::Materials, 0 is the fallback material
		uint32_t EntityIdLow = 0; // The stable EntityId, split so picking can report which object a pixel shows
		uint32_t EntityIdHigh = 0;
	};

	static_assert(sizeof(RenderPrimitiveRecord) == 144, "RenderPrimitiveRecord must match the 144 byte PrimitiveRecord in Diagnostic.slang");
	static_assert(offsetof(RenderPrimitiveRecord, ObjectToWorld) == 0, "ObjectToWorld must sit at std430 offset 0");
	static_assert(offsetof(RenderPrimitiveRecord, WorldToObject) == 64, "WorldToObject must sit at std430 offset 64");
	static_assert(offsetof(RenderPrimitiveRecord, Type) == 128, "Type must sit at std430 offset 128");
	static_assert(offsetof(RenderPrimitiveRecord, MaterialIndex) == 132, "MaterialIndex must sit at std430 offset 132");
	static_assert(offsetof(RenderPrimitiveRecord, EntityIdLow) == 136, "EntityIdLow must sit at std430 offset 136");
	static_assert(offsetof(RenderPrimitiveRecord, EntityIdHigh) == 140, "EntityIdHigh must sit at std430 offset 140");

	// Must match MaterialRecord in Diagnostic.slang, std430 layout
	struct RenderMaterialRecord
	{
		std::array<float, 4> BaseColor{}; // Linear RGB, w unused
		std::array<float, 4> EmittedRadiance{}; // Linear RGB, w unused
		uint32_t Type = 0; // MaterialType
		uint32_t Padding0 = 0;
		uint32_t Padding1 = 0;
		uint32_t Padding2 = 0;
	};

	static_assert(sizeof(RenderMaterialRecord) == 48, "RenderMaterialRecord must match the 48 byte MaterialRecord in Diagnostic.slang");
	static_assert(offsetof(RenderMaterialRecord, BaseColor) == 0, "BaseColor must sit at std430 offset 0");
	static_assert(offsetof(RenderMaterialRecord, EmittedRadiance) == 16, "EmittedRadiance must sit at std430 offset 16");
	static_assert(offsetof(RenderMaterialRecord, Type) == 32, "Type must sit at std430 offset 32");

	// The packed, upload-ready view of a Scene. Array positions are extraction details that change whenever the scene does, only the EntityIds inside the records are stable
	struct RenderScene
	{
		std::vector<RenderPrimitiveRecord> Primitives;
		std::vector<RenderMaterialRecord> Materials; // Never empty after a build, index 0 is the fallback material

		Math::Vector3 EnvironmentRadiance{ 0.0f, 0.0f, 0.0f }; // Extracted now, uploaded by the Step 7 integrator

		uint64_t Revision = 0; // The Scene::GetRadianceRevision the records were built from
	};

	// Rebuilds every record from the scene. Invisible entities, entities without geometry and degenerate transforms are skipped with a logged reason
	void BuildRenderScene(const Scene& scene, RenderScene& renderScene);
}