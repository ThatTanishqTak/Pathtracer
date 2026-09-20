#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Input/Input.hpp"

#include <functional>
#include <string>

struct SDL_Window;
union SDL_Event;

namespace Engine
{
	struct WindowSpecification
	{
		std::string Title = "Pathtracer";

		int Width = 1920;
		int Height = 1080;

		bool Resizable = false;
	};

	class Window
	{
	public:
		using EventCallback = std::function<void(const InputEvent&)>;
		using RawEventCallback = std::function<void(const SDL_Event&)>;

		Window();
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		void Initialize(const WindowSpecification& specification = {});
		void Shutdown();

		// Receives every translated input event, close and resize are still tracked by the window itself
		void SetEventCallback(EventCallback callback);

		// Internal hook for the UI layer: receives every raw SDL event before it is translated, including the ones the translation drops. The engine input keeps being built from the same events
		void SetRawEventCallback(RawEventCallback callback);

		void PollEvents();
		void WaitEvents();
		bool ShouldClose() const;
		bool IsMinimized() const;
		bool HasFocus() const;
		void RequestClose();

		// Relative mouse mode hides the cursor and reports motion as deltas only
		bool SetRelativeMouseMode(bool enabled);
		bool IsRelativeMouseMode() const;

		SDL_Window* GetNativeWindow() const { return m_NativeWindowHandle; }
		bool IsInitialized() const { return m_NativeWindowHandle != nullptr; }

		int GetWidth() const;
		int GetHeight() const;

		void GetFramebufferSize(int& width, int& height) const;
		bool ConsumeFramebufferResized();
		
		const WindowSpecification& GetSpecification() const { return m_Specification; }

	private:
		void HandleEvent(const SDL_Event& event);
		void Dispatch(const InputEvent& event);

		SDL_Window* m_NativeWindowHandle = nullptr;

		bool m_ShouldClose = false;
		bool m_FramebufferResized = false;

		EventCallback m_EventCallback;
		RawEventCallback m_RawEventCallback;

		WindowSpecification m_Specification;
	};
}