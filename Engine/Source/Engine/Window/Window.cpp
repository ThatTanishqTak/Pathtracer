#include "Engine/Window/Window.hpp"

#include <GLFW/glfw3.h>

#include <iostream>

namespace Engine
{
	void Window::Initialize()
	{
		if (!glfwInit())
		{
			std::cerr << "Failed to initialize GLFW" << std::endl;
			return;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		m_NativeWindowHandle = glfwCreateWindow(1920, 1080, "Pathtracer", nullptr, nullptr);
		if (!m_NativeWindowHandle)
		{
			std::cerr << "Failed to initialize native window handle" << std::endl;
			glfwTerminate();

			return;
		}
	}

	void Window::Shutdown()
	{
		glfwDestroyWindow(m_NativeWindowHandle);
		glfwTerminate();

		m_NativeWindowHandle = nullptr;
	}

	void Window::PollEvent()
	{
		glfwPollEvents();
	}

	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(m_NativeWindowHandle);
	}
}