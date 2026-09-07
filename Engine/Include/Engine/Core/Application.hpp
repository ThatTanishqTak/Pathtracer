#pragma once

#include <memory>
#include <string>

namespace Engine
{
	// Defined in Source/Core/Application.cpp
	struct ApplicationState;

	struct ApplicationSpecification
	{
		std::string Name = "Pathtracer";

		unsigned int WindowWidth = 1920;
		unsigned int WindowHeight = 1080;

		bool WindowResizable = false;
	};

	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification = {});
		~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		void Initialize();
		void Shutdown();

		void Run();

		void Close();

		bool IsInitialized() const;

		unsigned int GetWindowWidth() const;
		unsigned int GetWindowHeight() const;

		const ApplicationSpecification& GetSpecification() const;

	private:
		std::unique_ptr<ApplicationState> m_State;
	};

	// Implemented by the client, called by the engine entry point
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments);
}