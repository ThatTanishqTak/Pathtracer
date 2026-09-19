#pragma once

#include "Engine/Scene/Camera.hpp"

#include <cstdint>

namespace Engine
{
	class Scene;

	// Diagnostic views of the primary rays against the uploaded scene, the path tracer replaces them in Step 7. Values must match the constants in Diagnostic.slang
	enum class DiagnosticMode : uint32_t
	{
		RayDirection = 0, // World-space ray direction as RGB
		Hit, // White where the ray hits any primitive, sky or ground otherwise
		Normal, // Geometric normal facing the ray as RGB, black on a miss
		Distance, // Hit distance, brighter is closer, black on a miss
		BaseColor, // Linear base colour of the hit material, black on a miss, magenta for a material index the upload does not cover
	};

	struct RenderRequest
	{
		Camera ActiveCamera; // The client keeps ViewWidth and ViewHeight equal to the framebuffer size until RenderView owns the extent in Step 7
		const Scene* ActiveScene = nullptr; // Owned by the client and alive for the call, the renderer re-extracts when the scene's radiance revision changes. Null renders an empty scene
		DiagnosticMode Mode = DiagnosticMode::RayDirection;
		float Exposure = 1.0f; // Linear scene-light scale applied before the tone map curve, 1.0 is neutral
	};
}