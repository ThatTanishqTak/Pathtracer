#pragma once

#include <memory>

namespace Engine
{
	class Window;

	class Application
	{
	public:
		Application();
		~Application();

		void Initialize();
		void Shutdown();

		void Run();

	private:
		std::unique_ptr<Window> m_Window;
	};
}