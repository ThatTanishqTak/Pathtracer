#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstdint>
#include <vector>

namespace Engine
{
	class VulkanDevice;
	class VulkanSwapchain;

	class VulkanSynchronization
	{
	public:
		static constexpr uint64_t k_MaxFramesInFlight = 2;

		VulkanSynchronization();
		~VulkanSynchronization();

		VulkanSynchronization(const VulkanSynchronization&) = delete;
		VulkanSynchronization& operator=(const VulkanSynchronization&) = delete;
		VulkanSynchronization(VulkanSynchronization&&) = delete;
		VulkanSynchronization& operator=(VulkanSynchronization&&) = delete;

		void Initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain);
		void Shutdown();

		bool IsInitialized() const { return m_TimelineSemaphore != VK_NULL_HANDLE; }

		bool WaitForFrame();
		bool Submit(VkQueue queue, VkCommandBuffer commandBuffer, uint32_t imageIndex);
		void WaitForAllFrames();
		void RecoverAbandonedAcquire();

		uint32_t GetFrameIndex() const { return static_cast<uint32_t>(m_SubmittedFrameCount % k_MaxFramesInFlight); }

		VkSemaphore GetImageAvailableSemaphore() const;
		VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const;
		VkSemaphore GetTimelineSemaphore() const { return m_TimelineSemaphore; }
		uint64_t GetSubmittedFrameCount() const { return m_SubmittedFrameCount; }

	private:
		void CreateTimelineSemaphore();
		void CreateFrameSemaphores();
		void CreateImageSemaphores(uint32_t swapchainImageCount);
		void RehookBinarySemaphores();
		void DestroyImageSemaphores();
		void DestroyFrameSemaphores();
		void DestroyTimelineSemaphore();

		bool WaitForTimelineValue(uint64_t value);

	private:
		const VulkanDevice* m_Device = nullptr;
		const VulkanSwapchain* m_Swapchain = nullptr;

		VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
		uint64_t m_SubmittedFrameCount = 0;
		uint64_t m_SwapchainGeneration = 0;

		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	};
}