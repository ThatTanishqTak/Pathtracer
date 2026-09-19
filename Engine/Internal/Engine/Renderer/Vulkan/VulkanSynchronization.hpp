#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <array>
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

		VkResult WaitForFrame();
		VkResult SubmitCompute(VkQueue queue, VkCommandBuffer commandBuffer, uint64_t& submittedValue);
		VkResult Submit(VkQueue queue, VkCommandBuffer commandBuffer, uint32_t imageIndex, uint64_t& submittedValue);
		VkResult WaitForAllFrames();

		void MarkAcquirePending();

		VkResult WaitForPendingAcquires();

		uint32_t GetFrameIndex() const { return static_cast<uint32_t>(m_SubmittedFrameCount % k_MaxFramesInFlight); }

		VkSemaphore GetImageAvailableSemaphore() const;
		VkFence GetAcquireFence() const;
		VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const;
		VkSemaphore GetTimelineSemaphore() const { return m_TimelineSemaphore; }
		uint64_t GetSubmittedFrameCount() const { return m_SubmittedFrameCount; }
		uint64_t GetSubmittedTimelineValue() const { return m_TimelineValue; }

	private:
		VkResult CreateTimelineSemaphore();
		VkResult CreateFrameSemaphores();
		VkResult CreateAcquireFences();
		VkResult CreateImageSemaphores(uint32_t swapchainImageCount);
		VkResult RehookBinarySemaphores();
		void DestroyImageSemaphores();
		void DestroyFrameSemaphores();
		void DestroyAcquireFences();
		void DestroyTimelineSemaphore();

		VkResult WaitForAcquire(uint32_t frameSlot);
		VkResult WaitForTimelineValue(uint64_t value);

	private:
		const VulkanDevice* m_Device = nullptr;
		const VulkanSwapchain* m_Swapchain = nullptr;

		VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
		uint64_t m_TimelineValue = 0; // The last value a batch signals, every batch takes the next one
		uint64_t m_SubmittedFrameCount = 0; // Present batches, the frame slot follows this
		uint64_t m_SwapchainGeneration = 0;

		std::array<uint64_t, k_MaxFramesInFlight> m_SlotTimelineValues{}; // The last value signalled by a batch recorded on each slot, zero until the slot submits

		std::vector<VkSemaphore> m_ImageAvailableSemaphores;
		std::vector<VkFence> m_AcquireFences;
		std::vector<bool> m_AcquireFencePending;
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	};
}