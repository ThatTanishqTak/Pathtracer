#pragma once

#include "Engine/Math/Math.hpp"

#include <cstdint>

namespace Engine
{
	// Shared camera state. Editor free camera and Sandbox first-person controller
	struct Camera
	{
		Math::Vector3 Position{ 0.0f, 0.0f, 0.0f };
		Math::Quaternion Orientation = Math::k_IdentityRotation; // Identity looks along -Z with +Y up

		float VerticalFieldOfView = Math::ToRadians(60.0f); // Radians, must stay inside (0, pi)

		// Pixel extent of the view this camera renders into, the aspect ratio comes from here and nowhere else
		uint32_t ViewWidth = 0;
		uint32_t ViewHeight = 0;

		// Raster-only depth range for GetProjectionMatrix, picking and gizmos. Ray generation has no near or far plane
		float NearPlane = 0.1f;
		float FarPlane = 1000.0f;
	};

	// Orthonormal camera axes in world space
	struct CameraBasis
	{
		Math::Vector3 Right;
		Math::Vector3 Up;
		Math::Vector3 Forward;
	};

	struct CameraRayFrame
	{
		Math::Vector3 Origin;
		Math::Vector3 Forward;
		Math::Vector3 RightScaled; // Right * aspect * tan(fov / 2)
		Math::Vector3 UpScaled; // Up * tan(fov / 2)
	};

	struct Ray
	{
		Math::Vector3 Origin;
		Math::Vector3 Direction; // Unit length
	};

	// Heading of a camera that never rolls: yaw about world up, then pitch about the rotated right axis. Zero and zero looks along -Z, positive yaw turns left, positive pitch looks up
	struct YawPitch
	{
		float Yaw = 0.0f; // Radians in [-pi, pi]
		float Pitch = 0.0f; // Radians in [-pi / 2, pi / 2]
	};

	bool IsCameraValid(const Camera& camera);

	float GetAspectRatio(const Camera& camera); // Width over height, 1 while either extent is zero

	CameraBasis GetCameraBasis(const Camera& camera);
	CameraRayFrame GetCameraRayFrame(const Camera& camera);

	Ray GenerateCameraRay(const Camera& camera, float pixelX, float pixelY);
	Ray GenerateCameraRay(const CameraRayFrame& frame, uint32_t width, uint32_t height, float pixelX, float pixelY);

	Math::Matrix4 GetViewMatrix(const Camera& camera);
	Math::Matrix4 GetProjectionMatrix(const Camera& camera);

	bool ProjectPoint(const Camera& camera, const Math::Vector3& worldPoint, Math::Vector2& pixel, float& depth);

	Math::Vector3 UnprojectPixel(const Camera& camera, const Math::Vector2& pixel, float depth);
	Math::Quaternion LookAtOrientation(const Math::Vector3& position, const Math::Vector3& target, const Math::Vector3& up = Math::k_Up);

	// The first-person and free-camera composition, angleAxis(yaw, k_Up) * angleAxis(pitch, k_Right), normalized
	Math::Quaternion OrientationFromYawPitch(const YawPitch& angles);

	// Inverse of OrientationFromYawPitch for the direction the orientation faces, any roll in it is discarded
	YawPitch YawPitchFromOrientation(const Math::Quaternion& orientation);
}