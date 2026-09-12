#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <memory>

namespace Engine
{
	class Window;
	class VulkanInstance;
	class VulkanSurface;
	class VulkanDevice;
	class VulkanMemoryAllocator;
	class VulkanSwapchain;
	class VulkanSynchronization;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		void Initialize(const Window& window);
		void Shutdown();

		bool IsInitialized() const;

		void OnFramebufferResized();

	private:
		void InitializeVolk();
		void ShutdownVolk();

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;
		std::unique_ptr<VulkanDevice> m_VulkanDevice;
		std::unique_ptr<VulkanSurface> m_VulkanSurface;
		std::unique_ptr<VulkanMemoryAllocator> m_VulkanMemoryAllocator;
		std::unique_ptr<VulkanSwapchain> m_VulkanSwapchain;
		std::unique_ptr<VulkanSynchronization> m_VulkanSynchronization;

		bool m_VolkInitialized = false;
	};
}