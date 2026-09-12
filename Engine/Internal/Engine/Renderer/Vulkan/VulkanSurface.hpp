#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <Vulkan/vulkan.hpp>

namespace Engine
{
	class VulkanSurface
	{
	public:
		VulkanSurface();
		~VulkanSurface();

		VulkanSurface(const VulkanSurface&) = delete;
		VulkanSurface& operator=(const VulkanSurface&) = delete;
		VulkanSurface(VulkanSurface&&) = delete;
		VulkanSurface& operator=(VulkanSurface&&) = delete;

		void Initialize();
		void Shutdown();

		VkSurfaceKHR GetHandle() const { return m_Surface; }

	private:
		void CreateSurface();

	private:
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
	};
}