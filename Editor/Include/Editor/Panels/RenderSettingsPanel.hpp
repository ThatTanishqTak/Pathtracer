#pragma once

#include "Engine/Engine.hpp"

#include <cstdint>

namespace Editor
{
	// The Editor's render settings: workspace state the panel edits in place and GetRenderRequest reads. Everything but the exposure restarts the accumulation through the view key, so no notification is needed
	struct EditorRenderSettings
	{
		Engine::DiagnosticMode Mode = Engine::DiagnosticMode::PathTraced;
		Engine::RenderSettings Integrator;

		float RenderScale = 1.0f; // The view extent is the viewport content size times this
		float ExposureStops = 0.0f; // Photographic stops, exp2 gives the linear exposure the request carries
	};

	class RenderSettingsPanel
	{
	public:
		// The view extent is shown so a render scale change can be read off the numbers
		void Draw(EditorRenderSettings& settings, uint32_t viewWidth, uint32_t viewHeight);

	private:
		static constexpr int k_MaxBounces = 16;
		static constexpr float k_ExposureRangeStops = 8.0f;
	};
}