#include "Engine/Window/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Platform.hpp"

#include <SDL3/SDL.h>

namespace Engine
{
	Window::Window() = default;
	Window::~Window() = default;

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
		m_ShouldClose = false;

		if (m_Specification.Width <= 0 || m_Specification.Height <= 0)
		{
			PT_CORE_ERROR("Invalid window size {}x{}", m_Specification.Width, m_Specification.Height);

			return;
		}

		SDL_WindowFlags l_Flags = SDL_WINDOW_VULKAN;
		if (m_Specification.Resizable)
		{
			l_Flags |= SDL_WINDOW_RESIZABLE;
		}

		m_NativeWindowHandle = SDL_CreateWindow(m_Specification.Title.c_str(), m_Specification.Width, m_Specification.Height, l_Flags);

		if (!m_NativeWindowHandle)
		{
			PT_CORE_ERROR("Failed to create native window handle: {}", SDL_GetError());

			return;
		}

		// SDL may grant a different size than requested
		SDL_GetWindowSize(m_NativeWindowHandle, &m_Specification.Width, &m_Specification.Height);

		PT_CORE_TRACE("Window Title: {}", m_Specification.Title);
		PT_CORE_TRACE("Window Resolution: {}x{}", m_Specification.Width, m_Specification.Height);

		PT_CORE_INFO("------- WINDOW INITIALIZED -------");
	}

	void Window::Shutdown()
	{
		if (!m_NativeWindowHandle)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN WINDOW -------");

		SDL_DestroyWindow(m_NativeWindowHandle);

		m_NativeWindowHandle = nullptr;
		m_ShouldClose = false;

		PT_CORE_INFO("------- WINDOW SHUTDOWN COMPLETE -------");
	}

	void Window::HandleEvent(const SDL_Event& event)
	{
		switch (event.type)
		{
			case SDL_EVENT_QUIT:
			{
				m_ShouldClose = true;
				break;
			}
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			{
				if (m_NativeWindowHandle && event.window.windowID == SDL_GetWindowID(m_NativeWindowHandle))
				{
					m_ShouldClose = true;
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}

	void Window::WaitEvents()
	{
		SDL_Event l_Event;

		// Blocks until at least one event arrives
		if (!SDL_WaitEvent(&l_Event))
		{
			PT_CORE_ERROR("SDL_WaitEvent failed: {}", SDL_GetError());

			return;
		}

		HandleEvent(l_Event);

		// Drain whatever else queued up while blocked
		while (SDL_PollEvent(&l_Event))
		{
			HandleEvent(l_Event);
		}
	}

	bool Window::ShouldClose() const
	{
		if (!m_NativeWindowHandle)
		{
			return true;
		}

		return m_ShouldClose;
	}

	void Window::RequestClose()
	{
		if (m_NativeWindowHandle)
		{
			m_ShouldClose = true;
		}
	}

	int Window::GetWidth() const
	{
		if (!m_NativeWindowHandle)
		{
			return 0;
		}

		int l_Width = 0;
		int l_Height = 0;
		SDL_GetWindowSize(m_NativeWindowHandle, &l_Width, &l_Height);

		return l_Width;
	}

	int Window::GetHeight() const
	{
		if (!m_NativeWindowHandle)
		{
			return 0;
		}

		int l_Width = 0;
		int l_Height = 0;
		SDL_GetWindowSize(m_NativeWindowHandle, &l_Width, &l_Height);

		return l_Height;
	}

	void Window::GetFramebufferSize(int& Width, int& Height) const
	{
		Width = 0;
		Height = 0;

		if (m_NativeWindowHandle)
		{
			SDL_GetWindowSizeInPixels(m_NativeWindowHandle, &Width, &Height);
		}
	}
}