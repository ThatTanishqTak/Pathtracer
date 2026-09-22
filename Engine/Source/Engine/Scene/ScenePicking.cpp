#include "Engine/Scene/ScenePicking.hpp"

#include "Engine/Scene/Scene.hpp"

#include <cmath>

namespace Engine
{
	namespace
	{
		// Negative distances mean a miss everywhere below, as in Modules/Intersection.slang
		constexpr float k_Miss = -1.0f;

		// The same limit BuildRenderScene rejects at, so a click can only pick what the image shows
		constexpr float k_MinimumScale = 1e-6f;

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

	ScenePick PickClosest(const Scene& scene, const Ray& ray)
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