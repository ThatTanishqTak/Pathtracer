#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk/volk.h>
#include <vk_mem_alloc.h>

namespace Engine
{
	class VulkanInstance;
	class VulkanDevice;

	class VulkanMemoryAllocator
	{
	public:
		VulkanMemoryAllocator();
		~VulkanMemoryAllocator();

		VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
		VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;
		VulkanMemoryAllocator(VulkanMemoryAllocator&&) = delete;
		VulkanMemoryAllocator& operator=(VulkanMemoryAllocator&&) = delete;

		void Initialize(const VulkanInstance& instance, const VulkanDevice& device);
		void Shutdown();

		bool IsInitialized() const { return m_VulkanMemoryAllocator != VK_NULL_HANDLE; }

		VmaAllocator GetHandle() const { return m_VulkanMemoryAllocator; }

	private:
		void CreateAllocator(const VulkanInstance& instance, const VulkanDevice& device);

	private:
		VmaAllocator m_VulkanMemoryAllocator = VK_NULL_HANDLE;
	};
}