#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <numbers>

namespace Engine
{
	namespace Math
	{
		using Vector2 = glm::vec2;
		using Vector3 = glm::vec3;
		using Vector4 = glm::vec4;
		using Quaternion = glm::quat;
		using Matrix3 = glm::mat3;
		using Matrix4 = glm::mat4;

		constexpr float k_Pi = std::numbers::pi_v<float>;
		constexpr float k_TwoPi = 2.0f * k_Pi;
		constexpr float k_HalfPi = 0.5f * k_Pi;

		// World axes the conventions above are written against
		constexpr Vector3 k_Right{ 1.0f, 0.0f, 0.0f };
		constexpr Vector3 k_Up{ 0.0f, 1.0f, 0.0f };
		constexpr Vector3 k_Forward{ 0.0f, 0.0f, -1.0f };

		constexpr Quaternion k_IdentityRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		constexpr float ToRadians(float degrees) { return degrees * (k_Pi / 180.0f); }
		constexpr float ToDegrees(float radians) { return radians * (180.0f / k_Pi); }

		inline bool IsFinite(const Vector3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
		inline bool IsFinite(const Quaternion& value) { return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

		inline bool NearlyEqual(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }
		inline bool NearlyEqual(const Vector3& a, const Vector3& b, float tolerance = 1e-5f) { return glm::length(a - b) <= tolerance; }
	}
}