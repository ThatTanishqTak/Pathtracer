#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

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
		Window();
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		void Initialize(const WindowSpecification& specification = {});
		void Shutdown();

		void PollEvents();
		bool ShouldClose() const;
		void RequestClose();

		SDL_Window* GetNativeWindow() const { return m_NativeWindowHandle; }
		bool IsValid() const { return m_NativeWindowHandle != nullptr; }

		int GetWidth() const;
		int GetHeight() const;

		void GetFramebufferSize(int& width, int& height) const;
		bool ConsumeFramebufferResized();
		
		const WindowSpecification& GetSpecification() const { return m_Specification; }

	private:
		void HandleEvent(const SDL_Event& event);

		SDL_Window* m_NativeWindowHandle = nullptr;

		bool m_ShouldClose = false;
		bool m_FramebufferResized = false;

		WindowSpecification m_Specification;
	};
}