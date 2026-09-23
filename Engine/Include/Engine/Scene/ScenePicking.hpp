#pragma once

#include "Engine/Math/Math.hpp"
#include "Engine/Scene/Camera.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Engine
{
	class AssetManager;
	class Scene;

	// What a ray through the scene hits first, the CPU twin of Hit in Modules/Intersection.slang with the entity in place of the material
	struct ScenePick
	{
		EntityId Entity = EntityId::Invalid; // Invalid when the ray hit nothing
		float Distance = -1.0f; // Along the world ray, negative on a miss
		Math::Vector3 Position{ 0.0f, 0.0f, 0.0f }; // World space, the ray origin on a miss
	};

	// Nearest distance beyond minDistance to the unit sphere at the origin along an object-space ray, negative on a miss. The direction need not be unit length, so an object-space distance is also the world distance
	float IntersectUnitSphere(const Math::Vector3& origin, const Math::Vector3& direction, float minDistance);

	// Distance beyond minDistance to the unit square in the object XY plane, both sides count, negative on a miss
	float IntersectUnitQuad(const Math::Vector3& origin, const Math::Vector3& direction, float minDistance);

	// Whether the ray overlaps an axis-aligned box somewhere inside (minDistance, maxDistance), the slab test the mesh loop rejects with before touching a triangle
	bool IntersectBounds(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& boundsMin, const Math::Vector3& boundsMax, float minDistance, float maxDistance);

	// Distance beyond minDistance to one triangle, both sides count, negative on a miss. On a hit, barycentrics holds the weights of the second and third vertex, the first vertex takes the rest
	float IntersectTriangle(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector3& p2, float minDistance, Math::Vector2& barycentrics);

	// The closest entity the renderer would draw: visible, with geometry and a usable transform, intersected in object space through the same matrices BuildRenderScene uploads. Mesh entities are resolved through the assets, and skipped when they are null. A brute-force loop over every entity and, inside a mesh's bounds, every triangle. The GPU traverses a TLAS since Step 13 Part B and the hardware's triangle test is watertight, so the two can disagree only on the shared edge of two triangles, never on which object a pixel mostly shows
	ScenePick PickClosest(const Scene& scene, const AssetManager* assets, const Ray& ray);
}