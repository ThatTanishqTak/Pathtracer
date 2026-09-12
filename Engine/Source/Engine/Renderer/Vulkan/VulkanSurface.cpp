#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"

#include <SDL3/SDL_vulkan.h>

namespace Engine
{
	VulkanSurface::VulkanSurface() = default;
	VulkanSurface::~VulkanSurface() = default;

	void VulkanSurface::Initialize(const VulkanInstance& instance, const Window& window)
	{
		if (m_Surface != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan surface is already initialized");

			return;
		}

		if (!instance.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid Vulkan instance is required to create a surface");

			return;
		}

		if (!window.IsValid())
		{
			PT_CORE_CRITICAL("A valid window is required to create a surface");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN SURFACE -------");

		m_Instance = instance.GetHandle();

		CreateSurface(window);

		if (m_Surface == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN SURFACE INITIALIZED -------");
	}

	void VulkanSurface::Shutdown()
	{
		if (m_Surface == VK_NULL_HANDLE && m_Instance == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SURFACE -------");

		if (m_Surface != VK_NULL_HANDLE)
		{
			PT_CORE_TRACE("Destroying Surface");

			SDL_Vulkan_DestroySurface(m_Instance, m_Surface, nullptr);
			m_Surface = VK_NULL_HANDLE;

			PT_CORE_TRACE("Surface Destroyed");
		}

		m_Instance = VK_NULL_HANDLE;

		PT_CORE_INFO("------- VULKAN SURFACE SHUTDOWN COMPLETE -------");
	}

	void VulkanSurface::CreateSurface(const Window& window)
	{
		PT_CORE_TRACE("Creating Surface");

		if (!SDL_Vulkan_CreateSurface(window.GetNativeWindow(), m_Instance, nullptr, &m_Surface))
		{
			PT_CORE_CRITICAL("Failed to create Vulkan surface: {}", SDL_GetError());

			m_Surface = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Surface Created");
	}
}