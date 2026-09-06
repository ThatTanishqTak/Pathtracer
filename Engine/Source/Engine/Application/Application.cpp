#include "Engine/Core/Application.hpp"

#include "Engine/Window/Window.hpp"

#include <iostream>

namespace Engine
{
	Application::Application() = default;
	Application::~Application() = default;

	void Application::Initialize()
	{
		m_Window = std::make_unique<Window>();
		m_Window->Initialize();

		std::cout << "Application Initialized" << std::endl;
	}

	void Application::Shutdown()
	{
		m_Window->Shutdown();

		std::cout << "Application Shutdown" << std::endl;
	}

	void Application::Run()
	{
		while (!m_Window->ShouldClose())
		{
			m_Window->PollEvent();
		}
	}
}