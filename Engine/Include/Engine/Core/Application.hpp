#pragma once

#include "Engine/Input/Input.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Engine
{
	struct ApplicationSpecification
	{
		std::string Name = "Pathtracer";

		int WindowWidth = 1920;
		int WindowHeight = 1080;

		bool WindowResizable = false;
		bool EnableUI = false;
	};

	class ApplicationClient;
	class ChildProcess;
	class FileDialog;
	struct FileDialogRequest;
	struct FileDialogResult;
	struct ProcessExit;
	struct ProcessLaunchRequest;
	struct ProcessLaunchResult;
	class Platform;
	class Renderer;
	class UILayer;
	class Window;

	class Application
	{
	public:
		Application();
		~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		void Initialize(const ApplicationSpecification& specification, std::unique_ptr<ApplicationClient> client);
		void Shutdown();

		int Run();

		void Close();

		void SetMouseCaptured(bool captured);
		bool IsMouseCaptured() const;

		bool IsInitialized() const;

		int GetWindowWidth() const;
		int GetWindowHeight() const;
		int GetFramebufferWidth() const;
		int GetFramebufferHeight() const;

		std::filesystem::path GetExecutableDirectory() const;

		bool IsUIEnabled() const;
		uint64_t GetViewTextureId() const;

		bool ShowFileDialog(const FileDialogRequest& request);
		bool IsFileDialogOpen() const;
		std::optional<FileDialogResult> PollFileDialog();

		ProcessLaunchResult LaunchProcess(const ProcessLaunchRequest& request);
		bool IsProcessRunning() const;
		std::optional<ProcessExit> PollProcess();

		const ApplicationSpecification& GetSpecification() const;

	private:
		void OnInputEvent(const InputEvent& event);
		void StopClient() noexcept;

		ApplicationSpecification m_Specification;

		std::unique_ptr<Platform> m_Platform;
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<FileDialog> m_FileDialog;
		std::unique_ptr<ChildProcess> m_ChildProcess;
		std::unique_ptr<UILayer> m_UILayer;
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<ApplicationClient> m_Client;

		InputState m_Input;

		bool m_Initialized = false;
		bool m_ClientStarted = false;
	};

	// Implemented by the client
	std::unique_ptr<Application> CreateApplication(int argumentCount, char** arguments);
}