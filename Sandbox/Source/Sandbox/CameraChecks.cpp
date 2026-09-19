#include "Sandbox/CameraChecks.hpp"

#include "Engine/Engine.hpp"

#include <cmath>

namespace Sandbox
{
	namespace
	{
		using Engine::Math::Vector2;
		using Engine::Math::Vector3;

		constexpr float k_Tolerance = 1e-4f;

		bool Check(bool passed, const char* name)
		{
			if (passed)
			{
				PT_APP_TRACE("Camera check passed: {}", name);
			}
			else
			{
				PT_APP_ERROR("Camera check FAILED: {}", name);
			}

			return passed;
		}

		bool Near(const Vector3& a, const Vector3& b, float tolerance = k_Tolerance)
		{
			return Engine::Math::NearlyEqual(a, b, tolerance);
		}

		Engine::Camera MakeCamera(uint32_t width, uint32_t height, float fieldOfViewDegrees)
		{
			Engine::Camera l_Camera;
			l_Camera.ViewWidth = width;
			l_Camera.ViewHeight = height;
			l_Camera.VerticalFieldOfView = Engine::Math::ToRadians(fieldOfViewDegrees);

			return l_Camera;
		}

		// CPU twin of IntersectUnitSphere in Modules/Intersection.slang for the unit sphere at the origin with a zero minimum distance, negative on a miss
		float IntersectUnitSphere(const Engine::Ray& ray)
		{
			const float l_B = glm::dot(ray.Origin, ray.Direction);
			const float l_C = glm::dot(ray.Origin, ray.Origin) - 1.0f;
			const float l_Discriminant = l_B * l_B - l_C;
			if (l_Discriminant < 0.0f)
			{
				return -1.0f;
			}

			const float l_Near = -l_B - std::sqrt(l_Discriminant);

			return l_Near > 0.0f ? l_Near : -1.0f;
		}
	}

	bool RunCameraChecks()
	{
		using namespace Engine;

		bool l_AllPassed = true;

		// 1. The centre ray of an identity camera leaves the camera position along -Z, whatever the extent
		{
			Camera l_Camera = MakeCamera(1280, 720, 60.0f);
			l_Camera.Position = Vector3(1.0f, 2.0f, 3.0f);

			const Ray l_Ray = GenerateCameraRay(l_Camera, 640.0f, 360.0f);

			l_AllPassed = Check(Near(l_Ray.Origin, l_Camera.Position) && Near(l_Ray.Direction, Math::k_Forward), "centre ray points forward from the camera position") && l_AllPassed;
		}

		// 2. With a 90 degree field of view and a 2:1 aspect the exact image corners are (±2, ±1, -1) before normalization, row 0 at the top
		{
			const Camera l_Camera = MakeCamera(200, 100, 90.0f);

			const auto l_Expected = [](float x, float y) { return glm::normalize(Vector3(x, y, -1.0f)); };

			const bool l_TopLeft = Near(GenerateCameraRay(l_Camera, 0.0f, 0.0f).Direction, l_Expected(-2.0f, 1.0f));
			const bool l_TopRight = Near(GenerateCameraRay(l_Camera, 200.0f, 0.0f).Direction, l_Expected(2.0f, 1.0f));
			const bool l_BottomLeft = Near(GenerateCameraRay(l_Camera, 0.0f, 100.0f).Direction, l_Expected(-2.0f, -1.0f));
			const bool l_BottomRight = Near(GenerateCameraRay(l_Camera, 200.0f, 100.0f).Direction, l_Expected(2.0f, -1.0f));

			l_AllPassed = Check(l_TopLeft && l_TopRight && l_BottomLeft && l_BottomRight, "corner rays match the analytic frustum corners") && l_AllPassed;
		}

		// 3. A +90 degree yaw about world up turns the forward ray to -X and keeps the top-centre ray tilted up
		{
			Camera l_Camera = MakeCamera(100, 100, 90.0f);
			l_Camera.Orientation = glm::angleAxis(Math::k_HalfPi, Math::k_Up);

			const Ray l_Centre = GenerateCameraRay(l_Camera, 50.0f, 50.0f);
			const Ray l_Top = GenerateCameraRay(l_Camera, 50.0f, 0.0f);

			l_AllPassed = Check(Near(l_Centre.Direction, Vector3(-1.0f, 0.0f, 0.0f)) && Near(l_Top.Direction, glm::normalize(Vector3(-1.0f, 1.0f, 0.0f))), "yaw rotates the rays about the world up axis") && l_AllPassed;
		}

		// 4. Translation moves every origin and leaves every direction alone
		{
			const Camera l_Reference = MakeCamera(320, 240, 50.0f);
			Camera l_Moved = l_Reference;
			l_Moved.Position = Vector3(-4.0f, 7.0f, 2.0f);

			bool l_Same = true;
			for (const Vector2& l_Pixel : { Vector2(0.0f, 0.0f), Vector2(320.0f, 0.0f), Vector2(160.0f, 120.0f), Vector2(0.0f, 240.0f), Vector2(320.0f, 240.0f) })
			{
				const Ray l_ReferenceRay = GenerateCameraRay(l_Reference, l_Pixel.x, l_Pixel.y);
				const Ray l_MovedRay = GenerateCameraRay(l_Moved, l_Pixel.x, l_Pixel.y);

				l_Same = l_Same && Near(l_ReferenceRay.Direction, l_MovedRay.Direction) && Near(l_MovedRay.Origin, l_Moved.Position);
			}

			l_AllPassed = Check(l_Same, "translation moves the origin and preserves the directions") && l_AllPassed;
		}

		// 5. The aspect ratio changes only the horizontal spread: the top-centre ray is shared, the right-centre slope scales by the aspect ratio
		{
			const Camera l_Wide = MakeCamera(1600, 900, 60.0f);
			const Camera l_Narrow = MakeCamera(1200, 900, 60.0f);

			const Ray l_TopWide = GenerateCameraRay(l_Wide, 800.0f, 0.0f);
			const Ray l_TopNarrow = GenerateCameraRay(l_Narrow, 600.0f, 0.0f);
			const Ray l_RightWide = GenerateCameraRay(l_Wide, 1600.0f, 450.0f);
			const Ray l_RightNarrow = GenerateCameraRay(l_Narrow, 1200.0f, 450.0f);

			const float l_SlopeRatio = (l_RightWide.Direction.x / -l_RightWide.Direction.z) / (l_RightNarrow.Direction.x / -l_RightNarrow.Direction.z);
			const float l_ExpectedRatio = (16.0f / 9.0f) / (4.0f / 3.0f);

			l_AllPassed = Check(Near(l_TopWide.Direction, l_TopNarrow.Direction) && Math::NearlyEqual(l_SlopeRatio, l_ExpectedRatio, 1e-3f), "aspect ratio scales only the horizontal spread") && l_AllPassed;
		}

		// 6. Project then unproject returns the point, and the camera ray through that pixel passes through it: the raster camera and the tracer camera agree
		{
			Camera l_Camera = MakeCamera(1280, 720, 45.0f);
			l_Camera.Position = Vector3(0.5f, 1.5f, 2.0f);
			l_Camera.Orientation = glm::angleAxis(Math::ToRadians(30.0f), Math::k_Up) * glm::angleAxis(Math::ToRadians(-15.0f), Math::k_Right);

			const Vector3 l_Point(-1.2f, 0.4f, -6.0f);

			Vector2 l_Pixel(0.0f, 0.0f);
			float l_Depth = 0.0f;
			const bool l_Visible = ProjectPoint(l_Camera, l_Point, l_Pixel, l_Depth);
			const bool l_Inside = l_Pixel.x >= 0.0f && l_Pixel.x <= 1280.0f && l_Pixel.y >= 0.0f && l_Pixel.y <= 720.0f && l_Depth >= 0.0f && l_Depth <= 1.0f;

			const Vector3 l_RoundTrip = UnprojectPixel(l_Camera, l_Pixel, l_Depth);
			const Ray l_Ray = GenerateCameraRay(l_Camera, l_Pixel.x, l_Pixel.y);

			l_AllPassed = Check(l_Visible && l_Inside && Near(l_RoundTrip, l_Point, 1e-3f) && Near(l_Ray.Direction, glm::normalize(l_Point - l_Camera.Position)), "project/unproject round trip and the ray through the projected pixel agree") && l_AllPassed;
		}

		// 7. A point behind the camera does not project
		{
			Camera l_Camera = MakeCamera(640, 480, 60.0f);
			l_Camera.Position = Vector3(0.0f, 0.0f, 4.0f);

			Vector2 l_Pixel(0.0f, 0.0f);
			float l_Depth = 0.0f;

			l_AllPassed = Check(!ProjectPoint(l_Camera, Vector3(0.0f, 0.0f, 10.0f), l_Pixel, l_Depth), "points behind the camera are rejected by ProjectPoint") && l_AllPassed;
		}

		// 8. The look-at orientation faces the sphere, and the centre ray reaches it at the expected distance
		{
			Camera l_Camera = MakeCamera(640, 480, 60.0f);
			l_Camera.Position = Vector3(0.0f, 0.0f, 4.0f);
			l_Camera.Orientation = LookAtOrientation(l_Camera.Position, Vector3(0.0f, 0.0f, 0.0f));

			const CameraBasis l_Basis = GetCameraBasis(l_Camera);
			const Ray l_Ray = GenerateCameraRay(l_Camera, 320.0f, 240.0f);
			const Math::Vector4 l_OriginInView = GetViewMatrix(l_Camera) * Math::Vector4(0.0f, 0.0f, 0.0f, 1.0f);

			const bool l_Facing = Near(l_Basis.Forward, Math::k_Forward) && Near(l_Basis.Up, Math::k_Up) && Near(l_Basis.Right, Math::k_Right);
			const bool l_Hits = Math::NearlyEqual(IntersectUnitSphere(l_Ray), 3.0f, k_Tolerance);
			const bool l_ViewSpace = Near(Vector3(l_OriginInView), Vector3(0.0f, 0.0f, -4.0f));

			l_AllPassed = Check(l_Facing && l_Hits && l_ViewSpace, "look-at camera faces the diagnostic sphere and hits it at distance 3") && l_AllPassed;
		}

		// 9. Guards: a zero extent and an invalid field of view still produce a finite unit direction, and the camera reports itself invalid
		{
			Camera l_Camera;
			l_Camera.VerticalFieldOfView = 0.0f;

			const Ray l_Ray = GenerateCameraRay(l_Camera, 0.5f, 0.5f);

			l_AllPassed = Check(!IsCameraValid(l_Camera) && Math::IsFinite(l_Ray.Direction) && Math::NearlyEqual(glm::length(l_Ray.Direction), 1.0f), "invalid cameras are reported and still yield a finite unit ray") && l_AllPassed;
		}

		// 10. The yaw/pitch composition the controller builds matches the hand-written orientation of check 6 and survives a round trip through the angles
		{
			const YawPitch l_Angles{ .Yaw = Math::ToRadians(30.0f), .Pitch = Math::ToRadians(-15.0f) };

			Camera l_Expected = MakeCamera(100, 100, 60.0f);
			l_Expected.Orientation = glm::angleAxis(Math::ToRadians(30.0f), Math::k_Up) * glm::angleAxis(Math::ToRadians(-15.0f), Math::k_Right);

			Camera l_Built = l_Expected;
			l_Built.Orientation = OrientationFromYawPitch(l_Angles);

			// q and -q are the same rotation, so the frames are compared rather than the components
			const CameraBasis l_ExpectedBasis = GetCameraBasis(l_Expected);
			const CameraBasis l_BuiltBasis = GetCameraBasis(l_Built);
			const YawPitch l_RoundTrip = YawPitchFromOrientation(l_Built.Orientation);

			const bool l_SameFrame = Near(l_ExpectedBasis.Forward, l_BuiltBasis.Forward) && Near(l_ExpectedBasis.Up, l_BuiltBasis.Up) && Near(l_ExpectedBasis.Right, l_BuiltBasis.Right);
			const bool l_SameAngles = Math::NearlyEqual(l_RoundTrip.Yaw, l_Angles.Yaw) && Math::NearlyEqual(l_RoundTrip.Pitch, l_Angles.Pitch);

			l_AllPassed = Check(l_SameFrame && l_SameAngles, "yaw/pitch orientation matches the explicit composition and round-trips") && l_AllPassed;
		}

		// 11. Headings the controller starts from: identity is zero and zero, a +90 degree yaw faces -X, and a straight-up pitch still reports its yaw
		{
			const YawPitch l_Identity = YawPitchFromOrientation(Math::k_IdentityRotation);
			const YawPitch l_Left = YawPitchFromOrientation(glm::angleAxis(Math::k_HalfPi, Math::k_Up));
			const YawPitch l_Up = YawPitchFromOrientation(OrientationFromYawPitch(YawPitch{ .Yaw = Math::ToRadians(45.0f), .Pitch = Math::k_HalfPi }));

			const bool l_IdentityZero = Math::NearlyEqual(l_Identity.Yaw, 0.0f) && Math::NearlyEqual(l_Identity.Pitch, 0.0f);
			const bool l_LeftQuarter = Math::NearlyEqual(l_Left.Yaw, Math::k_HalfPi) && Math::NearlyEqual(l_Left.Pitch, 0.0f);
			const bool l_UpKeepsYaw = Math::NearlyEqual(l_Up.Yaw, Math::ToRadians(45.0f), 1e-3f) && Math::NearlyEqual(l_Up.Pitch, Math::k_HalfPi, 1e-3f);

			l_AllPassed = Check(l_IdentityZero && l_LeftQuarter && l_UpKeepsYaw, "spawn headings decompose into the expected yaw and pitch, including straight up") && l_AllPassed;
		}

		if (l_AllPassed)
		{
			PT_APP_INFO("Camera checks passed");
		}
		else
		{
			PT_APP_ERROR("One or more camera checks failed, see the messages above");
		}

		return l_AllPassed;
	}
}