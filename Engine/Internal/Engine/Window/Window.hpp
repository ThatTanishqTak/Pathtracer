#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <string>

struct GLFWwindow;

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
		Window() = default;
		~Window() = default;

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		// Requires an initialized Platform
		void Initialize(const WindowSpecification& specification = {});
		void Shutdown();

		void PollEvents();
		bool ShouldClose() const;
		void RequestClose();

		GLFWwindow* GetNativeWindow() const { return m_NativeWindowHandle; }
		bool IsValid() const { return m_NativeWindowHandle != nullptr; }

		// Screen coordinates
		int GetWidth() const;
		int GetHeight() const;

		// Pixels, required for the swapchain extent
		void GetFramebufferSize(int& Width, int& Height) const;

		const WindowSpecification& GetSpecification() const { return m_Specification; }

	private:
		GLFWwindow* m_NativeWindowHandle = nullptr;

		WindowSpecification m_Specification;
	};
}