#pragma once

#include <array>

namespace Engine
{
	struct RenderRequest
	{
		std::array<float, 4> ClearColor = { 0.05f, 0.05f, 0.05f, 1.0f };
		float GradientPhase = 0.0f;
		float Exposure = 1.0f;
	};
}