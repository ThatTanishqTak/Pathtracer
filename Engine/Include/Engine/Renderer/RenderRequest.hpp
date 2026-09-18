#pragma once

#include "Engine/Scene/Camera.hpp"

#include <cstdint>

namespace Engine
{
	// Diagnostic views of the primary rays, the path tracer replaces them in Step 7. Values must match the constants in Diagnostic.slang
	enum class DiagnosticMode : uint32_t
	{
		RayDirection = 0, // World-space ray direction as RGB
		SphereHit, // White where the ray hits the unit sphere at the origin, sky or ground otherwise
		SphereNormal, // Hit normal as RGB, black on a miss
		SphereDistance, // Hit distance, brighter is closer, black on a miss
	};

	struct RenderRequest
	{
		Camera ActiveCamera; // The client keeps ViewWidth and ViewHeight equal to the framebuffer size until RenderView owns the extent in Step 7
		DiagnosticMode Mode = DiagnosticMode::RayDirection;
		float Exposure = 1.0f; // Linear scene-light scale applied before the tone map curve, 1.0 is neutral
	};
}