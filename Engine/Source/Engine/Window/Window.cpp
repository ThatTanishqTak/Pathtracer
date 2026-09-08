#include "Engine/Window/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Platform.hpp"

#include <GLFW/glfw3.h>

namespace Engine
{
	void Window::Initialize(const WindowSpecification& specification)
	{
		if (m_NativeWindowHandle)
		{
			PT_CORE_WARN("Window is already initialized");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING WINDOW -------");

		if (!Platform::IsInitialized())
		{
			PT_CORE_ERROR("Platform must be initialized before creating a window");

			return;
		}

		m_Specification = specification;

		if (m_Specification.Width <= 0 || m_Specification.Height <= 0)
		{
			PT_CORE_ERROR("Invalid window size {}x{}", m_Specification.Width, m_Specification.Height);

			return;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, m_Specification.Resizable ? GLFW_TRUE : GLFW_FALSE);

		m_NativeWindowHandle = glfwCreateWindow(m_Specification.Width, m_Specification.Height, m_Specification.Title.c_str(), nullptr, nullptr);

		if (!m_NativeWindowHandle)
		{
			PT_CORE_ERROR("Failed to create native window handle");

			return;
		}

		// GLFW may grant a different size than requested
		glfwGetWindowSize(m_NativeWindowHandle, &m_Specification.Width, &m_Specification.Height);

		PT_CORE_TRACE("Created window \"{}\" ({}x{})", m_Specification.Title, m_Specification.Width, m_Specification.Height);

		PT_CORE_INFO("------- WINDOW INITIALIZED -------");
	}

	void Window::Shutdown()
	{
		if (!m_NativeWindowHandle)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN WINDOW -------");

		glfwDestroyWindow(m_NativeWindowHandle);

		m_NativeWindowHandle = nullptr;

		PT_CORE_INFO("------- WINDOW SHUTDOWN COMPLETE -------");
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	bool Window::ShouldClose() const
	{
		if (!m_NativeWindowHandle)
		{
			return true;
		}

		return glfwWindowShouldClose(m_NativeWindowHandle) != 0;
	}

	void Window::RequestClose()
	{
		if (m_NativeWindowHandle)
		{
			glfwSetWindowShouldClose(m_NativeWindowHandle, GLFW_TRUE);
		}
	}

	void Window::GetFramebufferSize(int& Width, int& Height) const
	{
		Width = 0;
		Height = 0;

		if (m_NativeWindowHandle)
		{
			glfwGetFramebufferSize(m_NativeWindowHandle, &Width, &Height);
		}
	}
}