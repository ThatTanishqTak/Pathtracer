#pragma once

#include "Engine/Scene/Camera.hpp"

#include <cstdint>

namespace Engine
{
	// How a view is rendered. The diagnostic values must match the constants in Diagnostic.slang, PathTraced selects Pathtrace.slang instead
	enum class DiagnosticMode : uint32_t
	{
		RayDirection = 0, // World-space ray direction as RGB
		Hit, // White where the ray hits any primitive, sky or ground otherwise
		Normal, // Geometric normal facing the ray as RGB, black on a miss
		Distance, // Hit distance, brighter is closer, black on a miss
		BaseColor, // Linear base colour of the hit material, black on a miss, magenta for a material index the upload does not cover
		PathTraced, // The progressive path tracer, accumulated across frames while the view stays still
	};

	// Integrator settings. Everything here except SamplesPerFrame restarts the accumulation when it changes
	struct RenderSettings
	{
		uint32_t SamplesPerFrame = 1; // Paths per pixel per frame, treated as at least one
		uint32_t MaxBounces = 4; // Reflections after the camera ray, the documented quality limit until Russian roulette arrives
		uint32_t Seed = 0; // Deterministic, the same seed and sample count reproduce the same image
	};

	// One rendered image: its own pixel extent, the camera that looks through it and how it is rendered. The renderer owns the GPU images and the accumulation behind it, and shows the result in the window until Step 10 adds a sampled display texture
	struct RenderView
	{
		uint32_t Width = 0; // Requested render extent in pixels, independent of the window size. Zero renders one pixel
		uint32_t Height = 0;

		Camera ActiveCamera; // ViewWidth and ViewHeight are replaced by Width and Height, so the aspect ratio follows the view

		RenderSettings Settings;
		DiagnosticMode Mode = DiagnosticMode::PathTraced;
	};
}