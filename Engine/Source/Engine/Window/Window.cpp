#include "Engine/Window/Window.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Platform.hpp"

#include <SDL3/SDL.h>

#include <utility>

namespace Engine
{
	namespace
	{
		// SDL scancodes are USB HID usage IDs, the same numbering Key uses, so only the range needs checking
		Key TranslateScancode(SDL_Scancode scancode)
		{
			const auto l_Value = static_cast<uint32_t>(scancode);

			if (l_Value == 0 || l_Value >= static_cast<uint32_t>(Key::Count))
			{
				return Key::Unknown;
			}

			return static_cast<Key>(l_Value);
		}

		MouseButton TranslateMouseButton(uint8_t button)
		{
			switch (button)
			{
				case SDL_BUTTON_LEFT:
				{
					return MouseButton::Left;
				}
				case SDL_BUTTON_RIGHT:
				{
					return MouseButton::Right;
				}
				case SDL_BUTTON_MIDDLE:
				{
					return MouseButton::Middle;
				}
				case SDL_BUTTON_X1:
				{
					return MouseButton::Extra1;
				}
				case SDL_BUTTON_X2:
				{
					return MouseButton::Extra2;
				}
				default:
				{
					return MouseButton::Unknown;
				}
			}
		}
	}

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

		m_Specification = specification;
		m_ShouldClose = false;
		m_FramebufferResized = false;

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

		// SDL may grant a different size than requested, the specification keeps the requested values
		int l_Width = 0;
		int l_Height = 0;
		SDL_GetWindowSize(m_NativeWindowHandle, &l_Width, &l_Height);

		PT_CORE_TRACE("Window Title: {}", m_Specification.Title);
		PT_CORE_TRACE("Window Resolution: {}x{}", l_Width, l_Height);

		PT_CORE_INFO("------- WINDOW INITIALIZED -------");
	}

	void Window::Shutdown()
	{
		if (!m_NativeWindowHandle)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN WINDOW -------");

		// Leaves the cursor usable if the client never released capture
		SDL_SetWindowRelativeMouseMode(m_NativeWindowHandle, false);

		SDL_DestroyWindow(m_NativeWindowHandle);

		m_NativeWindowHandle = nullptr;
		m_ShouldClose = false;
		m_FramebufferResized = false;
		m_EventCallback = nullptr;

		PT_CORE_INFO("------- WINDOW SHUTDOWN COMPLETE -------");
	}

	void Window::SetEventCallback(EventCallback callback)
	{
		m_EventCallback = std::move(callback);
	}

	void Window::Dispatch(const InputEvent& event)
	{
		if (m_EventCallback)
		{
			m_EventCallback(event);
		}
	}

	void Window::HandleEvent(const SDL_Event& event)
	{
		// Events carrying a window ID are dropped unless they belong to this window
		const auto l_IsThisWindow = [this](SDL_WindowID windowID)
		{
			return m_NativeWindowHandle && windowID == SDL_GetWindowID(m_NativeWindowHandle);
		};

		switch (event.type)
		{
			case SDL_EVENT_QUIT:
			{
				m_ShouldClose = true;
				break;
			}
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			{
				if (l_IsThisWindow(event.window.windowID))
				{
					m_ShouldClose = true;
				}
				break;
			}
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			{
				if (l_IsThisWindow(event.window.windowID))
				{
					m_FramebufferResized = true;

					InputEvent l_Event;
					l_Event.Type = InputEventType::WindowResized;
					l_Event.X = static_cast<float>(event.window.data1);
					l_Event.Y = static_cast<float>(event.window.data2);

					Dispatch(l_Event);
				}
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			{
				if (l_IsThisWindow(event.window.windowID))
				{
					InputEvent l_Event;
					l_Event.Type = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ? InputEventType::FocusGained : InputEventType::FocusLost;

					Dispatch(l_Event);
				}
				break;
			}
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
			{
				if (l_IsThisWindow(event.key.windowID))
				{
					InputEvent l_Event;
					l_Event.Type = event.key.down ? InputEventType::KeyPressed : InputEventType::KeyReleased;
					l_Event.KeyCode = TranslateScancode(event.key.scancode);
					l_Event.Repeat = event.key.repeat;

					Dispatch(l_Event);
				}
				break;
			}
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				if (l_IsThisWindow(event.button.windowID))
				{
					InputEvent l_Event;
					l_Event.Type = event.button.down ? InputEventType::MouseButtonPressed : InputEventType::MouseButtonReleased;
					l_Event.Button = TranslateMouseButton(event.button.button);
					l_Event.X = event.button.x;
					l_Event.Y = event.button.y;

					Dispatch(l_Event);
				}
				break;
			}
			case SDL_EVENT_MOUSE_MOTION:
			{
				if (l_IsThisWindow(event.motion.windowID))
				{
					InputEvent l_Event;
					l_Event.Type = InputEventType::MouseMoved;
					l_Event.X = event.motion.x;
					l_Event.Y = event.motion.y;
					l_Event.DeltaX = event.motion.xrel;
					l_Event.DeltaY = event.motion.yrel;

					Dispatch(l_Event);
				}
				break;
			}
			case SDL_EVENT_MOUSE_WHEEL:
			{
				if (l_IsThisWindow(event.wheel.windowID))
				{
					// SDL already applies the flipped direction to x and y, so nothing is negated here
					InputEvent l_Event;
					l_Event.Type = InputEventType::MouseWheel;
					l_Event.X = event.wheel.x;
					l_Event.Y = event.wheel.y;

					Dispatch(l_Event);
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}

	void Window::PollEvents()
	{
		SDL_Event l_Event;

		// Drain the queue without blocking so the render loop runs every frame
		while (SDL_PollEvent(&l_Event))
		{
			HandleEvent(l_Event);
		}
	}

	void Window::WaitEvents()
	{
		SDL_Event l_Event;

		if (SDL_WaitEvent(&l_Event))
		{
			HandleEvent(l_Event);
		}

		PollEvents();
	}

	bool Window::IsMinimized() const
	{
		if (!m_NativeWindowHandle)
		{
			return false;
		}

		return (SDL_GetWindowFlags(m_NativeWindowHandle) & SDL_WINDOW_MINIMIZED) != 0;
	}

	bool Window::HasFocus() const
	{
		if (!m_NativeWindowHandle)
		{
			return false;
		}

		return (SDL_GetWindowFlags(m_NativeWindowHandle) & SDL_WINDOW_INPUT_FOCUS) != 0;
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

	bool Window::SetRelativeMouseMode(bool enabled)
	{
		if (!m_NativeWindowHandle)
		{
			return false;
		}

		if (!SDL_SetWindowRelativeMouseMode(m_NativeWindowHandle, enabled))
		{
			PT_CORE_WARN("Failed to {} relative mouse mode: {}", enabled ? "enable" : "disable", SDL_GetError());

			return false;
		}

		return true;
	}

	bool Window::IsRelativeMouseMode() const
	{
		if (!m_NativeWindowHandle)
		{
			return false;
		}

		return SDL_GetWindowRelativeMouseMode(m_NativeWindowHandle);
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

	void Window::GetFramebufferSize(int& width, int& height) const
	{
		width = 0;
		height = 0;

		if (m_NativeWindowHandle)
		{
			SDL_GetWindowSizeInPixels(m_NativeWindowHandle, &width, &height);
		}
	}

	bool Window::ConsumeFramebufferResized()
	{
		const bool l_Resized = m_FramebufferResized;
		m_FramebufferResized = false;

		return l_Resized;
	}
}