#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

namespace Engine
{
	// Owns the process-global windowing backend lifetime
	class Platform
	{
	public:
		Platform();
		~Platform();

		static void Initialize();
		static void Shutdown();

		static bool IsInitialized() { return s_Initialized; }

	private:
		static bool s_Initialized;
	};
}