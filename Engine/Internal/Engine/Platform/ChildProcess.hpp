#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Core/ApplicationClient.hpp"

#include <optional>
#include <string>

struct SDL_Process;

namespace Engine
{
	// One launched process, tracked until it exits or the owner shuts down. Shutdown releases the handle and leaves the process running
	class ChildProcess
	{
	public:
		ChildProcess();
		~ChildProcess();

		ChildProcess(const ChildProcess&) = delete;
		ChildProcess& operator=(const ChildProcess&) = delete;

		ProcessLaunchResult Launch(const ProcessLaunchRequest& request);
		bool IsRunning() const { return m_Process != nullptr; }

		std::optional<ProcessExit> Poll();

		void Shutdown();

	private:
		SDL_Process* m_Process = nullptr;
		std::string m_Name;
	};
}