#pragma once

struct GLFWwindow;

namespace Engine
{
	class Window
	{
	public:
		void Initialize();
		void Shutdown();

		void PollEvent();
		bool ShouldClose() const;

		GLFWwindow* GetNativeWindow() { return m_NativeWindowHandle; }

	private:
		GLFWwindow* m_NativeWindowHandle = nullptr;
	};
}