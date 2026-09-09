#pragma once

#include <memory>
#include <string>

namespace Engine
{
	// Defined in Application.cpp
	struct ApplicationState;

	struct ApplicationSpecification
	{
		std::string Name = "Pathtracer";

		unsigned int WindowWidth = 1920;
		unsigned int WindowHeight = 1080;

		bool WindowResizable = false;
	};

	class Renderer;

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

		unsigned int GetWindowWidth() const;
		unsigned int GetWindowHeight() const;

		const ApplicationSpecification& GetSpecification() const;

	private:
		std::unique_ptr<ApplicationState> m_State;
		std::unique_ptr<Renderer> m_Renderer;
	};

	// Implemented by the client
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments);
}