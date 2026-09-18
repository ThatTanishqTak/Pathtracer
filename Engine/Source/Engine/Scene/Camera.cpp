#include "Engine/Scene/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace Engine
{
	namespace
	{
		constexpr float k_MinFieldOfView = Math::ToRadians(1.0f);
		constexpr float k_MaxFieldOfView = Math::ToRadians(179.0f);
		constexpr float k_DefaultFieldOfView = Math::ToRadians(60.0f);
		constexpr float k_MinNearPlane = 1e-4f;
		constexpr float k_Epsilon = 1e-6f;

		float ClampFieldOfView(float fieldOfView)
		{
			if (!std::isfinite(fieldOfView))
			{
				return k_DefaultFieldOfView;
			}

			return std::clamp(fieldOfView, k_MinFieldOfView, k_MaxFieldOfView);
		}

		Math::Quaternion SafeOrientation(const Math::Quaternion& orientation)
		{
			if (!Math::IsFinite(orientation) || glm::length(orientation) <= k_Epsilon)
			{
				return Math::k_IdentityRotation;
			}

			return glm::normalize(orientation);
		}

		float SafeExtent(uint32_t extent)
		{
			return static_cast<float>(std::max(extent, 1u));
		}
	}

	bool IsCameraValid(const Camera& camera)
	{
		const bool l_PoseValid = Math::IsFinite(camera.Position) && Math::IsFinite(camera.Orientation) && Math::NearlyEqual(glm::length(camera.Orientation), 1.0f, 1e-3f);
		const bool l_FieldOfViewValid = std::isfinite(camera.VerticalFieldOfView) && camera.VerticalFieldOfView >= k_MinFieldOfView && camera.VerticalFieldOfView <= k_MaxFieldOfView;
		const bool l_ExtentValid = camera.ViewWidth > 0 && camera.ViewHeight > 0;
		const bool l_DepthRangeValid = std::isfinite(camera.NearPlane) && std::isfinite(camera.FarPlane) && camera.NearPlane >= k_MinNearPlane && camera.FarPlane > camera.NearPlane;

		return l_PoseValid && l_FieldOfViewValid && l_ExtentValid && l_DepthRangeValid;
	}

	float GetAspectRatio(const Camera& camera)
	{
		if (camera.ViewWidth == 0 || camera.ViewHeight == 0)
		{
			return 1.0f;
		}

		return static_cast<float>(camera.ViewWidth) / static_cast<float>(camera.ViewHeight);
	}

	CameraBasis GetCameraBasis(const Camera& camera)
	{
		const Math::Quaternion l_Orientation = SafeOrientation(camera.Orientation);

		return CameraBasis
		{
			.Right = l_Orientation * Math::k_Right,
			.Up = l_Orientation * Math::k_Up,
			.Forward = l_Orientation * Math::k_Forward,
		};
	}

	CameraRayFrame GetCameraRayFrame(const Camera& camera)
	{
		const CameraBasis l_Basis = GetCameraBasis(camera);
		const float l_TanHalfFieldOfView = std::tan(0.5f * ClampFieldOfView(camera.VerticalFieldOfView));
		const float l_AspectRatio = GetAspectRatio(camera);

		return CameraRayFrame
		{
			.Origin = camera.Position,
			.Forward = l_Basis.Forward,
			.RightScaled = l_Basis.Right * (l_AspectRatio * l_TanHalfFieldOfView),
			.UpScaled = l_Basis.Up * l_TanHalfFieldOfView,
		};
	}

	Ray GenerateCameraRay(const CameraRayFrame& frame, uint32_t width, uint32_t height, float pixelX, float pixelY)
	{
		const float l_X = 2.0f * pixelX / SafeExtent(width) - 1.0f;
		const float l_Y = 1.0f - 2.0f * pixelY / SafeExtent(height);

		return Ray
		{
			.Origin = frame.Origin,
			.Direction = glm::normalize(frame.Forward + l_X * frame.RightScaled + l_Y * frame.UpScaled),
		};
	}

	Ray GenerateCameraRay(const Camera& camera, float pixelX, float pixelY)
	{
		return GenerateCameraRay(GetCameraRayFrame(camera), camera.ViewWidth, camera.ViewHeight, pixelX, pixelY);
	}

	Math::Matrix4 GetViewMatrix(const Camera& camera)
	{
		const Math::Quaternion l_InverseOrientation = glm::conjugate(SafeOrientation(camera.Orientation));

		return glm::mat4_cast(l_InverseOrientation) * glm::translate(Math::Matrix4(1.0f), -camera.Position);
	}

	Math::Matrix4 GetProjectionMatrix(const Camera& camera)
	{
		const float l_NearPlane = std::isfinite(camera.NearPlane) ? std::max(camera.NearPlane, k_MinNearPlane) : k_MinNearPlane;
		const float l_FarPlane = std::isfinite(camera.FarPlane) && camera.FarPlane > l_NearPlane ? camera.FarPlane : l_NearPlane * 2.0f;

		Math::Matrix4 l_Projection = glm::perspectiveRH_ZO(ClampFieldOfView(camera.VerticalFieldOfView), GetAspectRatio(camera), l_NearPlane, l_FarPlane);

		l_Projection[1][1] *= -1.0f;

		return l_Projection;
	}

	bool ProjectPoint(const Camera& camera, const Math::Vector3& worldPoint, Math::Vector2& pixel, float& depth)
	{
		const Math::Vector4 l_Clip = GetProjectionMatrix(camera) * GetViewMatrix(camera) * Math::Vector4(worldPoint, 1.0f);
		if (l_Clip.w <= k_Epsilon)
		{
			return false;
		}

		const Math::Vector3 l_Ndc = Math::Vector3(l_Clip) / l_Clip.w;

		pixel = Math::Vector2((l_Ndc.x * 0.5f + 0.5f) * SafeExtent(camera.ViewWidth), (l_Ndc.y * 0.5f + 0.5f) * SafeExtent(camera.ViewHeight));
		depth = l_Ndc.z;

		return true;
	}

	Math::Vector3 UnprojectPixel(const Camera& camera, const Math::Vector2& pixel, float depth)
	{
		const Math::Vector4 l_Ndc(2.0f * pixel.x / SafeExtent(camera.ViewWidth) - 1.0f, 2.0f * pixel.y / SafeExtent(camera.ViewHeight) - 1.0f, depth, 1.0f);
		const Math::Vector4 l_World = glm::inverse(GetProjectionMatrix(camera) * GetViewMatrix(camera)) * l_Ndc;

		return Math::Vector3(l_World) / l_World.w;
	}

	Math::Quaternion LookAtOrientation(const Math::Vector3& position, const Math::Vector3& target, const Math::Vector3& up)
	{
		const Math::Vector3 l_Offset = target - position;
		const float l_Distance = glm::length(l_Offset);
		if (!std::isfinite(l_Distance) || l_Distance <= k_Epsilon)
		{
			return Math::k_IdentityRotation;
		}

		const Math::Vector3 l_Direction = l_Offset / l_Distance;

		Math::Vector3 l_Up = up;
		if (glm::length(glm::cross(l_Direction, l_Up)) <= 1e-4f)
		{
			l_Up = std::abs(l_Direction.y) < 0.9f ? Math::k_Up : Math::k_Forward;
		}

		return glm::quatLookAtRH(l_Direction, l_Up);
	}
}