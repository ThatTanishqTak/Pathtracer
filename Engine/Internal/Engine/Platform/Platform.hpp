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

		void Initialize();
		void Shutdown();

		bool IsInitialized() const { return m_Initialized; }

	private:
		bool m_Initialized = false;
	};
}