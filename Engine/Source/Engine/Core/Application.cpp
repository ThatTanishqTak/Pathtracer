#include "Engine/Core/Application.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Platform.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Renderer/Renderer.hpp"

namespace Engine
{
	Application::Application() = default;
	Application::~Application() = default;

	void Application::Initialize(const ApplicationSpecification& specification)
	{
		if (m_Initialized)
		{
			return;
		}

		m_Specification = specification;

		PT_CORE_INFO("------- INITIALIZING APPLICATION -------");

		Platform::Initialize();

		if (!Platform::IsInitialized())
		{
			PT_CORE_CRITICAL("Failed to initialize platform, aborting startup");

			return;
		}

		WindowSpecification l_WindowSpecification;
		l_WindowSpecification.Title = m_Specification.Name;
		l_WindowSpecification.Width = m_Specification.WindowWidth;
		l_WindowSpecification.Height = m_Specification.WindowHeight;
		l_WindowSpecification.Resizable = m_Specification.WindowResizable;

		m_Window = std::make_unique<Window>();
		m_Window->Initialize(l_WindowSpecification);

		if (!m_Window->IsValid())
		{
			PT_CORE_CRITICAL("Failed to initialize window, aborting startup");

			m_Window.reset();
			Platform::Shutdown();

			return;
		}

		m_Renderer = std::make_unique<Renderer>();
		m_Renderer->Initialize(*m_Window);

		if (!m_Renderer->IsInitialized())
		{
			PT_CORE_CRITICAL("Failed to initialize renderer, aborting startup");

			m_Renderer->Shutdown();
			m_Renderer.reset();
			
			m_Window->Shutdown();
			m_Window.reset();

			Platform::Shutdown();

			return;
		}

		m_Initialized = true;

		PT_CORE_INFO("------- APPLICATION INITIALIZED -------");
	}

	void Application::Shutdown()
	{
		if (!m_Window && !Platform::IsInitialized())
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN APPLICATION -------");

		if (m_Renderer)
		{
			m_Renderer->Shutdown();
			m_Renderer.reset();
		}

		if (m_Window)
		{
			m_Window->Shutdown();
			m_Window.reset();
		}

		Platform::Shutdown();

		m_Initialized = false;
		m_Running = false;

		PT_CORE_INFO("------- APPLICATION SHUTDOWN COMPLETE -------");
	}

	void Application::Run()
	{
		if (!m_Initialized)
		{
			PT_CORE_ERROR("Run called before a successful Initialize");

			return;
		}

		PT_CORE_INFO("------- ENTERING MAIN LOOP -------");

		m_Running = true;

		while (m_Running && !m_Window->ShouldClose())
		{
			m_Window->PollEvents();

			m_Renderer->Render();
		}

		m_Running = false;

		PT_CORE_INFO("------- EXITING MAIN LOOP -------");
	}

	void Application::Close()
	{
		m_Running = false;

		if (m_Window)
		{
			m_Window->RequestClose();
		}
	}

	bool Application::IsInitialized() const
	{
		return m_Initialized;
	}

	unsigned int Application::GetWindowWidth() const
	{
		if (!m_Window)
		{
			return 0;
		}

		return static_cast<unsigned int>(m_Window->GetWidth());
	}

	unsigned int Application::GetWindowHeight() const
	{
		if (!m_Window)
		{
			return 0;
		}

		return static_cast<unsigned int>(m_Window->GetHeight());
	}

	const ApplicationSpecification& Application::GetSpecification() const
	{
		return m_Specification;
	}
}