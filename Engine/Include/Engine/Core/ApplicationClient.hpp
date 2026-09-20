#pragma once

#include "Engine/Core/FrameTime.hpp"
#include "Engine/Input/Input.hpp"
#include "Engine/Renderer/RenderRequest.hpp"

#include <cstdint>
#include <filesystem>

namespace Engine
{
	class Application;

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
	};
}