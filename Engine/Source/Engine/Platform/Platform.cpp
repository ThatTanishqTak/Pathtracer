#include "Engine/Platform/Platform.hpp"

#include "Engine/Core/Log.hpp"

#include <GLFW/glfw3.h>

namespace Engine
{
	bool Platform::s_Initialized = false;

	namespace
	{
		void GLFWErrorCallback(int errorCode, const char* description)
		{
			PT_CORE_ERROR("GLFW error ({}): {}", errorCode, description ? description : "<null>");
		}
	}

	void Platform::Initialize()
	{
		if (s_Initialized)
		{
			return;
		}

		PT_CORE_INFO("------- INITIALIZING PLATFORM -------");

		// Installed before glfwInit to catch initialization errors
		glfwSetErrorCallback(&GLFWErrorCallback);

		if (!glfwInit())
		{
			PT_CORE_CRITICAL("Failed to initialize GLFW");

			return;
		}

		int l_Major = 0;
		int l_Minor = 0;
		int l_Revision = 0;
		glfwGetVersion(&l_Major, &l_Minor, &l_Revision);

		PT_CORE_TRACE("GLFW {}.{}.{}", l_Major, l_Minor, l_Revision);

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

		glfwTerminate();
		glfwSetErrorCallback(nullptr);

		s_Initialized = false;

		PT_CORE_INFO("------- PLATFORM SHUTDOWN COMPLETE -------");
	}
}