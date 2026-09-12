#pragma once

#include <memory>
#include <string>

namespace Engine
{
	struct ApplicationSpecification
	{
		std::string Name = "Pathtracer";

		int WindowWidth = 1920;
		int WindowHeight = 1080;

		bool WindowResizable = false;
	};

	class Renderer;
	class Window;

	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		void Initialize(const ApplicationSpecification& specification = {});
		void Shutdown();

		void Run();

		void Close();

		bool IsInitialized() const;

		int GetWindowWidth() const;
		int GetWindowHeight() const;

		const ApplicationSpecification& GetSpecification() const;

	private:
		ApplicationSpecification m_Specification;

		std::unique_ptr<Window> m_Window;
		std::unique_ptr<Renderer> m_Renderer;

		bool m_Initialized = false;
	};

	// Implemented by the client
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments);
}