#include "Engine/Platform/Platform.hpp"

#include "Engine/Core/Log.hpp"

#include <SDL3/SDL.h>

namespace Engine
{
	bool Platform::s_Initialized = false;

	namespace
	{
		void SDLLogOutput(void* userData, int category, SDL_LogPriority priority, const char* message)
		{
			(void)userData;
			(void)category;

			const char* l_Message = message ? message : "<null>";

			switch (priority)
			{
				case SDL_LOG_PRIORITY_TRACE:
				case SDL_LOG_PRIORITY_VERBOSE:
				{
					PT_CORE_TRACE("[SDL]: {}", l_Message);
					break;
				}
				case SDL_LOG_PRIORITY_DEBUG:
				{
					PT_CORE_TRACE("[SDL]: {}", l_Message);
					break;
				}
				case SDL_LOG_PRIORITY_INFO:
				{
					PT_CORE_INFO("[SDL]: {}", l_Message);
					break;
				}
				case SDL_LOG_PRIORITY_WARN:
				{
					PT_CORE_WARN("[SDL]: {}", l_Message);
					break;
				}
				case SDL_LOG_PRIORITY_ERROR:
				{
					PT_CORE_ERROR("[SDL]: {}", l_Message);
					break;
				}
				case SDL_LOG_PRIORITY_CRITICAL:
				{
					PT_CORE_CRITICAL("[SDL]: {}", l_Message);
					break;
				}
				default:
				{
					PT_CORE_INFO("[SDL]: {}", l_Message);
					break;
				}
			}
		}
	}

	Platform::Platform() = default;
	Platform::~Platform() = default;

	void Platform::Initialize()
	{
		if (s_Initialized)
		{
			return;
		}

		PT_CORE_INFO("------- INITIALIZING PLATFORM -------");

		// Installed before SDL_Init so initialization failures are captured
		SDL_SetLogOutputFunction(&SDLLogOutput, nullptr);

		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			PT_CORE_CRITICAL("Failed to initialize SDL: {}", SDL_GetError());

			return;
		}

		const int l_Version = SDL_GetVersion();

		PT_CORE_TRACE("[SDL]: {}.{}.{}", SDL_VERSIONNUM_MAJOR(l_Version), SDL_VERSIONNUM_MINOR(l_Version), SDL_VERSIONNUM_MICRO(l_Version));

		s_Initialized = true;

		PT_CORE_INFO("------- PLATFORM INITIALIZED -------");
	}

	void Platform::Shutdown()
	{
		if (!s_Initialized)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN PLATFORM -------");

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);

		s_Initialized = false;

		PT_CORE_INFO("------- PLATFORM SHUTDOWN COMPLETE -------");
	}
}