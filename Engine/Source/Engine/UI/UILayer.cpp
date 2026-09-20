#include "Engine/UI/UILayer.hpp"

#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Window/Window.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace Engine
{
	UILayer::UILayer() = default;
	UILayer::~UILayer() = default;

	bool UILayer::Initialize(const Window& window, const std::string& layoutFileName)
	{
		if (m_Initialized)
		{
			PT_CORE_WARN("UI layer is already initialized");

			return true;
		}

		if (!window.IsInitialized())
		{
			PT_CORE_ERROR("A valid window is required to initialize the UI layer");

			return false;
		}

		PT_CORE_INFO("------- INITIALIZING UI LAYER -------");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& l_IO = ImGui::GetIO();
		l_IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		l_IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Docking inside the one native window, detached OS windows would each need a swapchain and a presentation lifetime
		l_IO.ConfigDpiScaleFonts = true; // The SDL3 backend reports the window's display scale, the fonts follow it

		// Panel layout is workspace state: it is saved next to the executable, never in a scene file and never relative to the working directory
		m_LayoutFilePath = (FileSystem::GetExecutableDirectory() / layoutFileName).string();
		l_IO.IniFilename = m_LayoutFilePath.c_str();

		ImGui::StyleColorsDark();

		if (!ImGui_ImplSDL3_InitForVulkan(window.GetNativeWindow()))
		{
			PT_CORE_ERROR("Failed to initialize the Dear ImGui SDL3 backend");

			ImGui::DestroyContext();
			m_LayoutFilePath.clear();

			return false;
		}

		m_Initialized = true;

		PT_CORE_TRACE("Dear ImGui {} with docking, layout file {}", IMGUI_VERSION, m_LayoutFilePath);
		PT_CORE_INFO("------- UI LAYER INITIALIZED -------");

		return true;
	}

	void UILayer::Shutdown()
	{
		if (!m_Initialized)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN UI LAYER -------");

		// The renderer shut the Vulkan backend down first, only the platform backend and the context remain. Destroying the context writes the layout file
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();

		m_LayoutFilePath.clear();
		m_Initialized = false;

		PT_CORE_INFO("------- UI LAYER SHUTDOWN COMPLETE -------");
	}

	void UILayer::ProcessEvent(const SDL_Event& event)
	{
		if (!m_Initialized)
		{
			return;
		}

		// The return value only says whether the UI took an interest. The engine input is updated from the same event regardless, and the client decides who owns it in BuildUI
		ImGui_ImplSDL3_ProcessEvent(&event);
	}

	void UILayer::BeginFrame()
	{
		if (!m_Initialized)
		{
			return;
		}

		// The Vulkan backend's NewFrame only asserts that the renderer created it, it is called here so the frame bracket lives in one place
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
	}

	void UILayer::EndFrame()
	{
		if (!m_Initialized)
		{
			return;
		}

		ImGui::Render();
	}
}