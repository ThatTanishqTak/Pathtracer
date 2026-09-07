#include "Engine/Core/Application.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Platform/Platform.hpp"
#include "Engine/Window/Window.hpp"

namespace Engine
{
	struct ApplicationState
	{
		ApplicationSpecification Specification;

		std::unique_ptr<Window> MainWindow;

		bool Initialized = false;
		bool Running = false;
	};

	Application::Application(const ApplicationSpecification& specification) : m_State(std::make_unique<ApplicationState>())
	{
		m_State->Specification = specification;
	}

	Application::~Application()
	{
		Shutdown();
	}

	void Application::Initialize()
	{
		if (m_State->Initialized)
		{
			return;
		}

		Log::Initialize();

		PT_CORE_INFO("------- INITIALIZING APPLICATION -------");

		Platform::Initialize();

		if (!Platform::IsInitialized())
		{
			PT_CORE_CRITICAL("Platform initialization failed, aborting startup");

			return;
		}

		WindowSpecification l_WindowSpecification;
		l_WindowSpecification.Title = m_State->Specification.Name;
		l_WindowSpecification.Width = static_cast<int>(m_State->Specification.WindowWidth);
		l_WindowSpecification.Height = static_cast<int>(m_State->Specification.WindowHeight);
		l_WindowSpecification.Resizable = m_State->Specification.WindowResizable;

		m_State->MainWindow = std::make_unique<Window>();
		m_State->MainWindow->Initialize(l_WindowSpecification);

		if (!m_State->MainWindow->IsValid())
		{
			PT_CORE_CRITICAL("Window initialization failed, aborting startup");

			m_State->MainWindow.reset();

			return;
		}

		m_State->Initialized = true;

		PT_CORE_INFO("------- APPLICATION INITIALIZED -------");
	}

	void Application::Shutdown()
	{
		if (!Log::IsInitialized() && !m_State->MainWindow && !Platform::IsInitialized())
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN APPLICATION -------");

		// Reverse order of initialization
		if (m_State->MainWindow)
		{
			m_State->MainWindow->Shutdown();
			m_State->MainWindow.reset();
		}

		Platform::Shutdown();

		m_State->Initialized = false;
		m_State->Running = false;

		PT_CORE_INFO("------- APPLICATION SHUTDOWN COMPLETE -------");

		// Last, so the lines above still reach a live logger
		Log::Shutdown();
	}

	void Application::Run()
	{
		if (!m_State->Initialized)
		{
			PT_CORE_ERROR("Run called before a successful Initialize");

			return;
		}

		PT_CORE_INFO("------- ENTERING MAIN LOOP -------");

		m_State->Running = true;

		while (m_State->Running && !m_State->MainWindow->ShouldClose())
		{
			m_State->MainWindow->PollEvents();
		}

		m_State->Running = false;

		PT_CORE_INFO("------- EXITING MAIN LOOP -------");
	}

	void Application::Close()
	{
		m_State->Running = false;

		if (m_State->MainWindow)
		{
			m_State->MainWindow->RequestClose();
		}
	}

	bool Application::IsInitialized() const
	{
		return m_State->Initialized;
	}

	unsigned int Application::GetWindowWidth() const
	{
		if (!m_State->MainWindow)
		{
			return 0;
		}

		return static_cast<unsigned int>(m_State->MainWindow->GetWidth());
	}

	unsigned int Application::GetWindowHeight() const
	{
		if (!m_State->MainWindow)
		{
			return 0;
		}

		return static_cast<unsigned int>(m_State->MainWindow->GetHeight());
	}

	const ApplicationSpecification& Application::GetSpecification() const
	{
		return m_State->Specification;
	}
}