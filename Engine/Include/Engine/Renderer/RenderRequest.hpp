#pragma once

#include "Engine/Renderer/RenderView.hpp"

namespace Engine
{
	class Scene;

	struct RenderRequest
	{
		RenderView View; // The one view this frame renders, its output fills the window
		const Scene* ActiveScene = nullptr; // Owned by the client and alive for the call, the renderer re-extracts when the scene's radiance revision changes. Null renders an empty scene
		float Exposure = 1.0f; // Linear scene-light scale applied before the tone map curve, 1.0 is neutral. Display only, it never restarts the accumulation
	};
}