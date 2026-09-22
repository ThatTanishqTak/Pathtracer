#include "Engine/Platform/ChildProcess.hpp"

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace Engine
{
	namespace
	{
		std::string ToUtf8(const std::filesystem::path& path)
		{
			const std::u8string l_Text = path.u8string();

			return std::string(reinterpret_cast<const char*>(l_Text.data()), l_Text.size());
		}
	}

	ChildProcess::ChildProcess() = default;
	ChildProcess::~ChildProcess() = default;

	ProcessLaunchResult ChildProcess::Launch(const ProcessLaunchRequest& request)
	{
		ProcessLaunchResult l_Result;

		if (IsRunning())
		{
			l_Result.Error = "a launched process is still running";

			PT_CORE_WARN("{} is still running, the new launch request is ignored", m_Name);

			return l_Result;
		}

		// The executable first, every argument as its own entry, then the terminator. SDL quotes them for the platform, nothing goes through a shell
		std::vector<std::string> l_Strings;
		l_Strings.reserve(request.Arguments.size() + 1);
		l_Strings.push_back(ToUtf8(request.Executable));

		for (const std::filesystem::path& l_Argument : request.Arguments)
		{
			l_Strings.push_back(ToUtf8(l_Argument));
		}

		std::vector<const char*> l_Arguments;
		l_Arguments.reserve(l_Strings.size() + 1);

		for (const std::string& l_String : l_Strings)
		{
			l_Arguments.push_back(l_String.c_str());
		}

		l_Arguments.push_back(nullptr);

		// No pipes: the process gets no standard input and inherits the standard output, so its log lands in the same console
		m_Process = SDL_CreateProcess(l_Arguments.data(), false);
		if (m_Process == nullptr)
		{
			l_Result.Error = SDL_GetError();
			if (l_Result.Error.empty())
			{
				l_Result.Error = "unknown error";
			}

			PT_CORE_ERROR("Failed to start {}: {}", l_Strings.front(), l_Result.Error);

			return l_Result;
		}

		m_Name = request.Executable.filename().string();
		l_Result.Started = true;

		PT_CORE_INFO("Started {} with {} argument(s)", l_Strings.front(), request.Arguments.size());

		return l_Result;
	}

	std::optional<ProcessExit> ChildProcess::Poll()
	{
		if (m_Process == nullptr)
		{
			return std::nullopt;
		}

		// Never blocks, false means the process is still running
		int l_ExitCode = 0;
		if (!SDL_WaitProcess(m_Process, false, &l_ExitCode))
		{
			return std::nullopt;
		}

		SDL_DestroyProcess(m_Process);
		m_Process = nullptr;

		PT_CORE_TRACE("{} exited with code {}", m_Name, l_ExitCode);

		return ProcessExit{ .ExitCode = l_ExitCode };
	}

	void ChildProcess::Shutdown()
	{
		if (m_Process == nullptr)
		{
			return;
		}

		// Destroying the object only drops the handle, the process keeps running; SDL_KillProcess would be the other policy
		PT_CORE_INFO("{} is still running and is left running", m_Name);

		SDL_DestroyProcess(m_Process);
		m_Process = nullptr;
	}
}