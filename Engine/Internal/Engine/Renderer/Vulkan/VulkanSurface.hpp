#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

namespace Engine
{
	class VulkanInstance;
	class Window;

	class VulkanSurface
	{
	public:
		VulkanSurface();
		~VulkanSurface();

		VulkanSurface(const VulkanSurface&) = delete;
		VulkanSurface& operator=(const VulkanSurface&) = delete;
		VulkanSurface(VulkanSurface&&) = delete;
		VulkanSurface& operator=(VulkanSurface&&) = delete;

		void Initialize(const VulkanInstance& instance, const Window& window);
		void Shutdown();

		bool IsInitialized() const { return m_Surface != VK_NULL_HANDLE; }

		VkSurfaceKHR GetHandle() const { return m_Surface; }

	private:
		void CreateSurface(const Window& window);

	private:
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkInstance m_Instance = VK_NULL_HANDLE;
	};
}