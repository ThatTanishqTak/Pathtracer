#include "Engine/Core/Application.hpp"

#include "Engine/Core/ApplicationClient.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Platform/ChildProcess.hpp"
#include "Engine/Platform/FileDialog.hpp"
#include "Engine/Platform/Platform.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/UI/UILayer.hpp"
#include "Engine/Core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace Engine
{
	namespace
	{
		constexpr float k_MaxDeltaSeconds = 0.1f;
	}

	ApplicationServices::ApplicationServices(Application& application) : m_Application(&application)
	{

	}

	void ApplicationServices::RequestClose()
	{
		m_Application->Close();
	}

	void ApplicationServices::SetMouseCaptured(bool captured)
	{
		m_Application->SetMouseCaptured(captured);
	}

	bool ApplicationServices::IsMouseCaptured() const
	{
		return m_Application->IsMouseCaptured();
	}

	int ApplicationServices::GetWindowWidth() const
	{
		return m_Application->GetWindowWidth();
	}

	int ApplicationServices::GetWindowHeight() const
	{
		return m_Application->GetWindowHeight();
	}

	int ApplicationServices::GetFramebufferWidth() const
	{
		return m_Application->GetFramebufferWidth();
	}

	int ApplicationServices::GetFramebufferHeight() const
	{
		return m_Application->GetFramebufferHeight();
	}

	std::filesystem::path ApplicationServices::GetExecutableDirectory() const
	{
		return m_Application->GetExecutableDirectory();
	}

	bool ApplicationServices::IsUIEnabled() const
	{
		return m_Application->IsUIEnabled();
	}

	uint64_t ApplicationServices::GetViewTextureId() const
	{
		return m_Application->GetViewTextureId();
	}

	bool ApplicationServices::ShowFileDialog(const FileDialogRequest& request)
	{
		return m_Application->ShowFileDialog(request);
	}

	bool ApplicationServices::IsFileDialogOpen() const
	{
		return m_Application->IsFileDialogOpen();
	}

	std::optional<FileDialogResult> ApplicationServices::PollFileDialog()
	{
		return m_Application->PollFileDialog();
	}

	ProcessLaunchResult ApplicationServices::LaunchProcess(const ProcessLaunchRequest& request)
	{
		return m_Application->LaunchProcess(request);
	}

	bool ApplicationServices::IsProcessRunning() const
	{
		return m_Application->IsProcessRunning();
	}

	std::optional<ProcessExit> ApplicationServices::PollProcess()
	{
		return m_Application->PollProcess();
	}

	Application::Application() = default;
	Application::~Application() = default;

	void Application::Initialize(const ApplicationSpecification& specification, std::unique_ptr<ApplicationClient> client)
	{
		if (m_Initialized)
		{
			return;
		}

		m_Specification = specification;

		PT_CORE_INFO("------- INITIALIZING APPLICATION -------");

		if (!client)
		{
			PT_CORE_CRITICAL("No application client was supplied, aborting startup");

			return;
		}

		m_Client = std::move(client);

		m_Platform = std::make_unique<Platform>();
		m_Platform->Initialize();
		if (!m_Platform->IsInitialized())
		{
			PT_CORE_CRITICAL("Failed to initialize platform, aborting startup");

			Shutdown();

			return;
		}

		WindowSpecification l_WindowSpecification;
		l_WindowSpecification.Title = m_Specification.Name;
		l_WindowSpecification.Width = m_Specification.WindowWidth;
		l_WindowSpecification.Height = m_Specification.WindowHeight;
		l_WindowSpecification.Resizable = m_Specification.WindowResizable;

		m_Window = std::make_unique<Window>();
		m_Window->Initialize(l_WindowSpecification);
		if (!m_Window->IsInitialized())
		{
			PT_CORE_CRITICAL("Failed to initialize window, aborting startup");

			Shutdown();

			return;
		}

		m_Window->SetEventCallback([this](const InputEvent& event) { OnInputEvent(event); });
		m_FileDialog = std::make_unique<FileDialog>();
		m_ChildProcess = std::make_unique<ChildProcess>();
		if (m_Specification.EnableUI)
		{
			m_UILayer = std::make_unique<UILayer>();
			if (!m_UILayer->Initialize(*m_Window, m_Specification.Name + ".layout.ini"))
			{
				PT_CORE_CRITICAL("Failed to initialize the UI layer, aborting startup");

				Shutdown();

				return;
			}

			m_Window->SetRawEventCallback([this](const SDL_Event& event) { m_UILayer->ProcessEvent(event); });
		}

		m_Renderer = std::make_unique<Renderer>();
		m_Renderer->Initialize(*m_Window, m_UILayer != nullptr);
		if (!m_Renderer->IsInitialized())
		{
			PT_CORE_CRITICAL("Failed to initialize renderer, aborting startup");

			Shutdown();

			return;
		}

		m_Initialized = true;

		PT_CORE_INFO("------- APPLICATION INITIALIZED -------");
	}

	void Application::Shutdown()
	{
		if (!m_Renderer && !m_UILayer && !m_FileDialog && !m_ChildProcess && !m_Window && !m_Platform && !m_Client)
		{
			m_Initialized = false;

			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN APPLICATION -------");

		StopClient();

		if (m_Renderer)
		{
			m_Renderer->Shutdown();
			m_Renderer.reset();
		}

		if (m_UILayer)
		{
			m_UILayer->Shutdown();
			m_UILayer.reset();
		}

		if (m_FileDialog)
		{
			m_FileDialog->Shutdown();
			m_FileDialog.reset();
		}

		if (m_ChildProcess)
		{
			m_ChildProcess->Shutdown();
			m_ChildProcess.reset();
		}

		if (m_Window)
		{
			m_Window->Shutdown();
			m_Window.reset();
		}

		if (m_Platform)
		{
			m_Platform->Shutdown();
			m_Platform.reset();
		}

		m_Client.reset();

		m_Input = InputState{};
		m_Initialized = false;

		PT_CORE_INFO("------- APPLICATION SHUTDOWN COMPLETE -------");
	}

	int Application::Run()
	{
		if (!m_Initialized)
		{
			PT_CORE_ERROR("Run called before a successful Initialize");

			return 1;
		}

		PT_CORE_INFO("------- ENTERING MAIN LOOP -------");

		int l_ExitCode = 0;

		m_Input = InputState{};
		m_Input.HasFocus = m_Window->HasFocus();
		m_Input.MouseCaptured = m_Window->IsRelativeMouseMode();

		ApplicationServices l_Services(*this);

		m_ClientStarted = true;
		m_Client->OnStart(l_Services);

		using Clock = std::chrono::steady_clock;

		const Clock::time_point l_StartTime = Clock::now();
		Clock::time_point l_PreviousTime = l_StartTime;

		FrameTime l_FrameTime;

		while (m_ClientStarted)
		{
			// 1. Edges, deltas and wheel movement only live for one iteration
			m_Input.ResetTransient();

			// 2. Poll, notify the client through the window callback, keep close and resize tracking
			if (m_Window->IsMinimized())
			{
				// Nothing can be presented while minimized, sleep on the event queue instead of spinning
				m_Window->WaitEvents();

				// Anything accumulated while minimized is dropped by the reset at the top of the next iteration, so restoring the window cannot produce one big jump
				l_PreviousTime = Clock::now();

				continue;
			}

			m_Window->PollEvents();

			if (m_Window->ConsumeFramebufferResized())
			{
				m_Renderer->OnFramebufferResized();
			}

			// 3. Close before starting new GPU work, unless the client holds the request back for an unsaved-changes prompt. A held request is cleared so the next close gesture asks again
			if (m_Window->ShouldClose())
			{
				if (m_Client->OnCloseRequested())
				{
					break;
				}

				m_Window->CancelClose();
			}

			// 4. Monotonic time, the simulation step is clamped but the real elapsed time is kept for statistics
			const Clock::time_point l_Now = Clock::now();
			const float l_Elapsed = std::chrono::duration<float>(l_Now - l_PreviousTime).count();
			l_PreviousTime = l_Now;

			l_FrameTime.ElapsedSeconds = l_Elapsed;
			l_FrameTime.DeltaSeconds = std::min(l_Elapsed, k_MaxDeltaSeconds);
			l_FrameTime.TotalSeconds = std::chrono::duration<double>(l_Now - l_StartTime).count();

			// 5. The UI frame: the client lays out its panels, reads the edits and decides who owns the input before Update acts on it. Nothing happens here for a client without the UI
			if (m_UILayer)
			{
				m_UILayer->BeginFrame();
				m_Client->BuildUI();
				m_UILayer->EndFrame();
			}

			// 6. Client update and its render request
			m_Client->Update(l_FrameTime, m_Input);

			const RenderRequest l_Request = m_Client->GetRenderRequest();

			// 7. Render and react to the outcome, the UI draw data from step 5 is composed into the same frame
			const RenderOutcome l_Outcome = m_Renderer->Render(l_Request);

			if (l_Outcome == RenderOutcome::Fatal)
			{
				PT_CORE_CRITICAL("Renderer reported a fatal error, leaving the main loop");

				l_ExitCode = 1;

				break;
			}

			if (l_Outcome == RenderOutcome::Skipped)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			++l_FrameTime.FrameIndex;
		}

		StopClient();

		PT_CORE_INFO("------- EXITING MAIN LOOP -------");

		return l_ExitCode;
	}

	void Application::OnInputEvent(const InputEvent& event)
	{
		switch (event.Type)
		{
			case InputEventType::KeyPressed:
			{
				const size_t l_Index = InputState::Index(event.KeyCode);

				if (!event.Repeat && !m_Input.KeysDown[l_Index])
				{
					m_Input.KeysPressed[l_Index] = true;
				}

				m_Input.KeysDown[l_Index] = true;
				break;
			}
			case InputEventType::KeyReleased:
			{
				const size_t l_Index = InputState::Index(event.KeyCode);

				m_Input.KeysDown[l_Index] = false;
				m_Input.KeysReleased[l_Index] = true;
				break;
			}
			case InputEventType::MouseButtonPressed:
			{
				const size_t l_Index = InputState::Index(event.Button);

				if (!m_Input.MouseButtonsDown[l_Index])
				{
					m_Input.MouseButtonsPressed[l_Index] = true;
				}

				m_Input.MouseButtonsDown[l_Index] = true;
				m_Input.MouseX = event.X;
				m_Input.MouseY = event.Y;
				break;
			}
			case InputEventType::MouseButtonReleased:
			{
				const size_t l_Index = InputState::Index(event.Button);

				m_Input.MouseButtonsDown[l_Index] = false;
				m_Input.MouseButtonsReleased[l_Index] = true;
				m_Input.MouseX = event.X;
				m_Input.MouseY = event.Y;
				break;
			}
			case InputEventType::MouseMoved:
			{
				m_Input.MouseX = event.X;
				m_Input.MouseY = event.Y;
				m_Input.MouseDeltaX += event.DeltaX;
				m_Input.MouseDeltaY += event.DeltaY;
				break;
			}
			case InputEventType::MouseWheel:
			{
				m_Input.WheelX += event.X;
				m_Input.WheelY += event.Y;
				break;
			}
			case InputEventType::FocusGained:
			{
				m_Input.HasFocus = true;
				break;
			}
			case InputEventType::FocusLost:
			{
				m_Input.HasFocus = false;
				m_Input.ClearHeld();

				SetMouseCaptured(false);
				break;
			}
			case InputEventType::WindowResized:
			{
				break;
			}
		}

		if (m_ClientStarted)
		{
			m_Client->OnEvent(event);
		}
	}

	void Application::StopClient() noexcept
	{
		if (!m_ClientStarted)
		{
			return;
		}

		m_ClientStarted = false;

		if (m_Client)
		{
			m_Client->OnStop();
		}

		SetMouseCaptured(false);
	}

	void Application::Close()
	{
		if (m_Window)
		{
			m_Window->RequestClose();
		}
	}

	void Application::SetMouseCaptured(bool captured)
	{
		if (!m_Window)
		{
			return;
		}

		if (m_Input.MouseCaptured == captured)
		{
			return;
		}

		if (m_Window->SetRelativeMouseMode(captured))
		{
			m_Input.MouseCaptured = captured;
		}
	}

	bool Application::IsMouseCaptured() const
	{
		return m_Input.MouseCaptured;
	}

	bool Application::IsInitialized() const
	{
		return m_Initialized;
	}

	int Application::GetWindowWidth() const
	{
		if (!m_Window)
		{
			return 0;
		}

		return m_Window->GetWidth();
	}

	int Application::GetWindowHeight() const
	{
		if (!m_Window)
		{
			return 0;
		}

		return m_Window->GetHeight();
	}

	int Application::GetFramebufferWidth() const
	{
		int l_Width = 0;
		int l_Height = 0;
		if (m_Window)
		{
			m_Window->GetFramebufferSize(l_Width, l_Height);
		}

		return l_Width;
	}

	int Application::GetFramebufferHeight() const
	{
		int l_Width = 0;
		int l_Height = 0;
		if (m_Window)
		{
			m_Window->GetFramebufferSize(l_Width, l_Height);
		}

		return l_Height;
	}

	std::filesystem::path Application::GetExecutableDirectory() const
	{
		return FileSystem::GetExecutableDirectory();
	}

	bool Application::IsUIEnabled() const
	{
		return m_UILayer != nullptr && m_UILayer->IsInitialized();
	}

	uint64_t Application::GetViewTextureId() const
	{
		if (!m_Renderer)
		{
			return 0;
		}

		return m_Renderer->GetViewTextureId();
	}

	bool Application::ShowFileDialog(const FileDialogRequest& request)
	{
		if (!m_FileDialog || !m_Window)
		{
			return false;
		}

		return m_FileDialog->Show(request, m_Window->GetNativeWindow());
	}

	bool Application::IsFileDialogOpen() const
	{
		return m_FileDialog != nullptr && m_FileDialog->IsOpen();
	}

	std::optional<FileDialogResult> Application::PollFileDialog()
	{
		if (!m_FileDialog)
		{
			return std::nullopt;
		}

		return m_FileDialog->Poll();
	}

	ProcessLaunchResult Application::LaunchProcess(const ProcessLaunchRequest& request)
	{
		if (!m_ChildProcess)
		{
			ProcessLaunchResult l_Result;
			l_Result.Error = "the application is not initialized";

			return l_Result;
		}

		return m_ChildProcess->Launch(request);
	}

	bool Application::IsProcessRunning() const
	{
		return m_ChildProcess != nullptr && m_ChildProcess->IsRunning();
	}

	std::optional<ProcessExit> Application::PollProcess()
	{
		if (!m_ChildProcess)
		{
			return std::nullopt;
		}

		return m_ChildProcess->Poll();
	}

	const ApplicationSpecification& Application::GetSpecification() const
	{
		return m_Specification;
	}
}