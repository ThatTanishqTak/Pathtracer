#pragma once

#include "Engine/Renderer/RenderView.hpp"

namespace Engine
{
	class Scene;

	struct RenderRequest
	{
		RenderView View; // The one view this frame renders, its output fills the window or the UI viewport
		const Scene* ActiveScene = nullptr; // Owned by the client and alive for the call, the renderer re-extracts when the scene's radiance revision changes. Null renders an empty scene
		float Exposure = 1.0f; // Linear scene-light scale applied before the tone map curve, 1.0 is neutral. Display only, it never restarts the accumulation
		bool DrawViewToWindow = true; // The view's display texture fills the swapchain through the fullscreen display pass. False when the UI shows it through the view texture id instead, the swapchain is then cleared before the UI pass
	};
}