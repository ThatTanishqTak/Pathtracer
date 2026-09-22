#pragma once

#include "Engine/Math/Math.hpp"
#include "Engine/Scene/Camera.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Engine
{
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

	// The closest entity the renderer would draw: visible, with geometry and a usable transform, intersected in object space through the same matrices BuildRenderScene uploads. A brute-force loop over every entity until Step 13 adds acceleration
	ScenePick PickClosest(const Scene& scene, const Ray& ray);
}