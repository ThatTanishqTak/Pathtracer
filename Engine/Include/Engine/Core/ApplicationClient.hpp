#pragma once

#include "Engine/Core/FrameTime.hpp"
#include "Engine/Input/Input.hpp"
#include "Engine/Renderer/RenderRequest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Engine
{
	class Application;

	enum class FileDialogKind : uint8_t
	{
		Open,
		Save,
	};

	struct FileDialogRequest
	{
		FileDialogKind Kind = FileDialogKind::Open;

		std::string FilterName;
		std::string FilterPattern;

		std::filesystem::path DefaultLocation;
	};

	struct FileDialogResult
	{
		FileDialogKind Kind = FileDialogKind::Open;

		bool Accepted = false;
		std::filesystem::path Path;

		std::string Error;
	};

	struct ProcessLaunchRequest
	{
		std::filesystem::path Executable;

		std::vector<std::filesystem::path> Arguments;
	};

	struct ProcessLaunchResult
	{
		bool Started = false;
		std::string Error;
	};

	struct ProcessExit
	{
		int ExitCode = 0;
	};

	class ApplicationServices
	{
	public:
		explicit ApplicationServices(Application& application);

		void RequestClose();

		void SetMouseCaptured(bool captured);
		bool IsMouseCaptured() const;

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

	private:
		Application* m_Application = nullptr;
	};

	class ApplicationClient
	{
	public:
		virtual ~ApplicationClient() = default;

		virtual void OnStart(ApplicationServices& services) = 0;
		virtual void OnStop() noexcept = 0;
		virtual void OnEvent(const InputEvent& event) = 0;
		virtual void BuildUI() {}
		virtual void Update(const FrameTime& time, const InputState& input) = 0;
		virtual RenderRequest GetRenderRequest() const = 0;
		virtual bool OnCloseRequested() { return true; }
	};
}