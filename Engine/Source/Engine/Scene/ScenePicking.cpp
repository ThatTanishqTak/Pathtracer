#include "Engine/Scene/ScenePicking.hpp"

#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace Engine
{
	namespace
	{
		// Negative distances mean a miss everywhere below, as in Modules/Intersection.slang for the unit shapes
		constexpr float k_Miss = -1.0f;

		// The same limit BuildRenderScene rejects at, so a click can only pick what the image shows
		constexpr float k_MinimumScale = 1e-6f;

		// A direction component below this runs parallel to the slab, and a determinant below it a ray parallel to the triangle's plane. The unit shapes match the shader, the triangles are the hardware's on the GPU
		constexpr float k_ParallelEpsilon = 1e-12f;

		// Nothing in a scene is this far away, the same far limit the shader's primary rays use
		constexpr float k_MaxRayDistance = 1e30f;

		bool IsFinite(const Math::Matrix4& matrix)
		{
			for (int i_Column = 0; i_Column < 4; ++i_Column)
			{
				for (int i_Row = 0; i_Row < 4; ++i_Row)
				{
					if (!std::isfinite(matrix[i_Column][i_Row]))
					{
						return false;
					}
				}
			}

			return true;
		}

		// The closest triangle of one mesh along an object-space ray, what the mesh BLAS answers on the GPU. Only triangles nearer than maxDistance count, so the bounds test and the loop prune against the best hit so far
		float IntersectMesh(const Mesh& mesh, const Math::Vector3& origin, const Math::Vector3& direction, float minDistance, float maxDistance)
		{
			if (!IntersectBounds(origin, direction, mesh.Bounds.Min, mesh.Bounds.Max, minDistance, maxDistance))
			{
				return k_Miss;
			}

			float l_Closest = k_Miss;
			float l_Limit = maxDistance;

			for (size_t i_Index = 0; i_Index + 2 < mesh.Indices.size(); i_Index += 3)
			{
				Math::Vector2 l_Barycentrics;
				const float l_Distance = IntersectTriangle(origin, direction, mesh.Vertices[mesh.Indices[i_Index]].Position, mesh.Vertices[mesh.Indices[i_Index + 1]].Position, mesh.Vertices[mesh.Indices[i_Index + 2]].Position, minDistance, l_Barycentrics);
				if (l_Distance > 0.0f && l_Distance < l_Limit)
				{
					l_Closest = l_Distance;
					l_Limit = l_Distance;
				}
			}

			return l_Closest;
		}
	}

	float IntersectUnitSphere(const Math::Vector3& origin, const Math::Vector3& direction, float minDistance)
	{
		const float l_A = glm::dot(direction, direction);
		const float l_B = glm::dot(origin, direction);
		const float l_C = glm::dot(origin, origin) - 1.0f;
		const float l_Discriminant = l_B * l_B - l_A * l_C;
		if (l_Discriminant < 0.0f || l_A <= 0.0f)
		{
			return k_Miss;
		}

		const float l_Root = std::sqrt(l_Discriminant);

		const float l_Near = (-l_B - l_Root) / l_A;
		if (l_Near > minDistance)
		{
			return l_Near;
		}

		// The origin is inside the sphere or sits on its surface, the far intersection is the one in front
		const float l_Far = (-l_B + l_Root) / l_A;

		return l_Far > minDistance ? l_Far : k_Miss;
	}

	float IntersectUnitQuad(const Math::Vector3& origin, const Math::Vector3& direction, float minDistance)
	{
		if (std::abs(direction.z) < 1e-8f)
		{
			return k_Miss;
		}

		const float l_Distance = -origin.z / direction.z;
		if (l_Distance <= minDistance)
		{
			return k_Miss;
		}

		const Math::Vector2 l_Point = Math::Vector2(origin) + Math::Vector2(direction) * l_Distance;
		if (std::abs(l_Point.x) > 0.5f || std::abs(l_Point.y) > 0.5f)
		{
			return k_Miss;
		}

		return l_Distance;
	}

	bool IntersectBounds(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& boundsMin, const Math::Vector3& boundsMax, float minDistance, float maxDistance)
	{
		float l_Enter = minDistance;
		float l_Exit = maxDistance;

		for (int i_Axis = 0; i_Axis < 3; ++i_Axis)
		{
			// Parallel to this pair of slabs: the ray is inside them for its whole length or misses the box outright, no division and no NaN either way
			if (std::abs(direction[i_Axis]) < k_ParallelEpsilon)
			{
				if (origin[i_Axis] < boundsMin[i_Axis] || origin[i_Axis] > boundsMax[i_Axis])
				{
					return false;
				}

				continue;
			}

			const float l_Inverse = 1.0f / direction[i_Axis];
			float l_Near = (boundsMin[i_Axis] - origin[i_Axis]) * l_Inverse;
			float l_Far = (boundsMax[i_Axis] - origin[i_Axis]) * l_Inverse;
			if (l_Near > l_Far)
			{
				std::swap(l_Near, l_Far);
			}

			l_Enter = std::max(l_Enter, l_Near);
			l_Exit = std::min(l_Exit, l_Far);
			if (l_Enter > l_Exit)
			{
				return false;
			}
		}

		return true;
	}

	float IntersectTriangle(const Math::Vector3& origin, const Math::Vector3& direction, const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector3& p2, float minDistance, Math::Vector2& barycentrics)
	{
		// Möller-Trumbore, the Part A shader's steps in the same order. The GPU's hardware test is watertight, so the two may differ only exactly on an edge
		barycentrics = Math::Vector2(0.0f, 0.0f);

		const Math::Vector3 l_Edge1 = p1 - p0;
		const Math::Vector3 l_Edge2 = p2 - p0;
		const Math::Vector3 l_P = glm::cross(direction, l_Edge2);
		const float l_Determinant = glm::dot(l_Edge1, l_P);
		if (std::abs(l_Determinant) < k_ParallelEpsilon)
		{
			return k_Miss;
		}

		const float l_InverseDeterminant = 1.0f / l_Determinant;
		const Math::Vector3 l_T = origin - p0;
		const float l_U = glm::dot(l_T, l_P) * l_InverseDeterminant;
		if (l_U < 0.0f || l_U > 1.0f)
		{
			return k_Miss;
		}

		const Math::Vector3 l_Q = glm::cross(l_T, l_Edge1);
		const float l_V = glm::dot(direction, l_Q) * l_InverseDeterminant;
		if (l_V < 0.0f || l_U + l_V > 1.0f)
		{
			return k_Miss;
		}

		const float l_Distance = glm::dot(l_Edge2, l_Q) * l_InverseDeterminant;
		if (l_Distance <= minDistance)
		{
			return k_Miss;
		}

		barycentrics = Math::Vector2(l_U, l_V);

		return l_Distance;
	}

	ScenePick PickClosest(const Scene& scene, const AssetManager* assets, const Ray& ray)
	{
		ScenePick l_Pick;
		l_Pick.Position = ray.Origin;

		for (const Entity& l_Entity : scene.GetEntities())
		{
			if (!l_Entity.Visible)
			{
				continue;
			}

			Math::Vector3 l_GeometryScale{};
			if (!GetGeometryScale(l_Entity.Geometry, l_GeometryScale))
			{
				continue;
			}

			const Math::Vector3 l_TotalScale = l_Entity.Transform.Scale * l_GeometryScale;
			if (!Math::IsFinite(l_Entity.Transform.Translation) || !Math::IsFinite(l_TotalScale) || std::abs(l_TotalScale.x) < k_MinimumScale || std::abs(l_TotalScale.y) < k_MinimumScale || std::abs(l_TotalScale.z) < k_MinimumScale)
			{
				continue;
			}

			// Exactly the record's WorldToObject: the transform with the geometry scale folded behind it
			const Math::Matrix4 l_WorldToObject = glm::inverse(GetLocalToWorld(l_Entity.Transform) * glm::scale(Math::Matrix4(1.0f), l_GeometryScale));
			if (!IsFinite(l_WorldToObject))
			{
				continue;
			}

			// The origin as a point and the direction as a direction, not normalized on purpose so the object-space distance is the world distance
			const Math::Vector3 l_Origin = Math::Vector3(l_WorldToObject * Math::Vector4(ray.Origin, 1.0f));
			const Math::Vector3 l_Direction = Math::Vector3(l_WorldToObject * Math::Vector4(ray.Direction, 0.0f));

			// A primary ray, so nothing is skipped near the origin
			float l_Distance = k_Miss;
			switch (l_Entity.Geometry.Type)
			{
				case GeometryType::Sphere:
				{
					l_Distance = IntersectUnitSphere(l_Origin, l_Direction, 0.0f);
					break;
				}
				case GeometryType::Quad:
				{
					l_Distance = IntersectUnitQuad(l_Origin, l_Direction, 0.0f);
					break;
				}
				case GeometryType::Mesh:
				{
					// A mesh the renderer skipped, unloaded or unknown to the assets, cannot be picked either
					const Mesh* l_Mesh = assets != nullptr ? assets->FindMesh(l_Entity.Geometry.Mesh) : nullptr;
					if (l_Mesh != nullptr)
					{
						l_Distance = IntersectMesh(*l_Mesh, l_Origin, l_Direction, 0.0f, l_Pick.Distance > 0.0f ? l_Pick.Distance : k_MaxRayDistance);
					}
					break;
				}
				default:
				{
					break;
				}
			}

			if (l_Distance > 0.0f && (l_Pick.Distance < 0.0f || l_Distance < l_Pick.Distance))
			{
				l_Pick.Entity = l_Entity.Id;
				l_Pick.Distance = l_Distance;
			}
		}

		if (l_Pick.Distance > 0.0f)
		{
			l_Pick.Position = ray.Origin + ray.Direction * l_Pick.Distance;
		}

		return l_Pick;
	}
}