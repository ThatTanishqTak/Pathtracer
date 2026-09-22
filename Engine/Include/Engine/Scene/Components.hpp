#pragma once

#include "Engine/Math/Math.hpp"

#include <cstdint>

namespace Engine
{
	// Translate * rotate * scale, column-major, the same conventions as Camera. Non-uniform scale is honoured by the renderer through object-space intersection
	struct TransformComponent
	{
		Math::Vector3 Translation{ 0.0f, 0.0f, 0.0f };
		Math::Quaternion Rotation = Math::k_IdentityRotation; // Unit length, a zero or non-finite value is treated as the identity
		Math::Vector3 Scale{ 1.0f, 1.0f, 1.0f };
	};

	// Values match the constants in Diagnostic.slang
	enum class GeometryType : uint8_t
	{
		None = 0, // The entity has no renderable shape
		Sphere, // Centre at the object origin, Radius before the transform's scale
		Quad, // Width by Height in the object XY plane, centred on the origin, normal along object +Z, two-sided
	};

	struct GeometryComponent
	{
		GeometryType Type = GeometryType::None;

		float Radius = 0.5f; // Sphere
		float Width = 1.0f; // Quad
		float Height = 1.0f; // Quad
	};

	inline Math::Quaternion GetSafeRotation(const TransformComponent& transform)
	{
		const float l_Length = glm::length(transform.Rotation);
		if (!Math::IsFinite(transform.Rotation) || l_Length <= 1e-6f)
		{
			return Math::k_IdentityRotation;
		}

		return transform.Rotation / l_Length;
	}

	// Object space to world space, a point transforms as M * p
	inline Math::Matrix4 GetLocalToWorld(const TransformComponent& transform)
	{
		return glm::translate(Math::Matrix4(1.0f), transform.Translation) * glm::mat4_cast(GetSafeRotation(transform)) * glm::scale(Math::Matrix4(1.0f), transform.Scale);
	}

	// The object-space scale that turns the unit shape into the geometry: a sphere's radius on every axis, a quad's width and height in its plane. The renderer and picking both fold it behind the transform so they intersect the same unit shapes. False for None or a non-finite parameter
	inline bool GetGeometryScale(const GeometryComponent& geometry, Math::Vector3& scale)
	{
		switch (geometry.Type)
		{
			case GeometryType::Sphere:
			{
				scale = Math::Vector3(geometry.Radius);

				return std::isfinite(geometry.Radius);
			}
			case GeometryType::Quad:
			{
				scale = Math::Vector3(geometry.Width, geometry.Height, 1.0f);

				return std::isfinite(geometry.Width) && std::isfinite(geometry.Height);
			}
			default:
			{
				return false;
			}
		}
	}
}