#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <string>

union SDL_Event;

namespace Engine
{
	class Window;

	// The Dear ImGui context and its SDL3 platform backend. Application owns one when the specification enables the UI, the Vulkan side is the renderer's VulkanUIBackend. Panel content stays in the client's BuildUI
	class UILayer
	{
	public:
		UILayer();
		~UILayer();

		UILayer(const UILayer&) = delete;
		UILayer& operator=(const UILayer&) = delete;
		UILayer(UILayer&&) = delete;
		UILayer& operator=(UILayer&&) = delete;

		// Creates the context with docking inside the one native window and hooks the SDL3 backend to it. The layout file is workspace state and lives next to the executable like every other piece of shipped content
		bool Initialize(const Window& window, const std::string& layoutFileName);
		void Shutdown(); // After the renderer shut the Vulkan backend down

		bool IsInitialized() const { return m_Initialized; }

		// Every raw SDL event, before the window translates it, so the UI and the engine input are built from the same stream
		void ProcessEvent(const SDL_Event& event);

		// Bracket the client's BuildUI: NewFrame on both backends and the context, then ImGui::Render, which leaves the draw data for the renderer's UI pass
		void BeginFrame();
		void EndFrame();

	private:
		std::string m_LayoutFilePath; // io.IniFilename points into this string, so it lives as long as the context
		bool m_Initialized = false;
	};
}