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

	// One primary command buffer per frame in flight, all allocated from a single resettable pool
	class VulkanCommandPool
	{
	public:
		VulkanCommandPool();
		~VulkanCommandPool();

		VulkanCommandPool(const VulkanCommandPool&) = delete;
		VulkanCommandPool& operator=(const VulkanCommandPool&) = delete;
		VulkanCommandPool(VulkanCommandPool&&) = delete;
		VulkanCommandPool& operator=(VulkanCommandPool&&) = delete;

		void Initialize(const VulkanDevice& device, uint32_t commandBufferCount);
		void Shutdown();

		bool IsInitialized() const { return m_CommandPool != VK_NULL_HANDLE; }

		// Resets the buffer and puts it in the recording state, the caller must have waited for its previous submission
		bool Begin(uint32_t index);
		bool End(uint32_t index);

		VkCommandPool GetHandle() const { return m_CommandPool; }
		VkCommandBuffer GetCommandBuffer(uint32_t index) const;
		uint32_t GetCommandBufferCount() const { return static_cast<uint32_t>(m_CommandBuffers.size()); }

	private:
		void CreateCommandPool();
		void AllocateCommandBuffers(uint32_t commandBufferCount);
		void DestroyCommandPool();

	private:
		const VulkanDevice* m_Device = nullptr;

		VkCommandPool m_CommandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> m_CommandBuffers;
	};
}