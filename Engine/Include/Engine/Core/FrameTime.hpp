#pragma once

#include <cstdint>

namespace Engine
{
	// Timing for one loop iteration, measured with a monotonic clock
	struct FrameTime
	{
		// Simulation step, clamped so a pause, breakpoint or minimized window cannot produce one huge jump
		float DeltaSeconds = 0.0f;

		// Real time since the previous iteration, never clamped, for statistics only
		float ElapsedSeconds = 0.0f;

		// Real time since Run started
		double TotalSeconds = 0.0;

		// Number of completed loop iterations before this one
		uint64_t FrameIndex = 0;
	};
}