#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace Engine
{
	class VulkanMemoryAllocator;

	enum class BufferMemory : uint8_t
	{
		DeviceLocal, // GPU only, filled through a transfer or a shader
		HostUpload, // Persistently mapped, the CPU writes it through Upload()
	};

	struct VulkanBufferSpecification
	{
		VkDeviceSize Size = 0;
		VkBufferUsageFlags Usage = 0;
		BufferMemory Memory = BufferMemory::DeviceLocal;
		const char* DebugName = "buffer";
	};

	// One VMA allocation and its VkBuffer
	class VulkanBuffer
	{
	public:
		VulkanBuffer();
		~VulkanBuffer();

		VulkanBuffer(const VulkanBuffer&) = delete;
		VulkanBuffer& operator=(const VulkanBuffer&) = delete;
		VulkanBuffer(VulkanBuffer&&) = delete;
		VulkanBuffer& operator=(VulkanBuffer&&) = delete;

		VkResult Initialize(const VulkanMemoryAllocator& allocator, const VulkanBufferSpecification& specification);
		void Shutdown();

		bool IsInitialized() const { return m_Buffer != VK_NULL_HANDLE; }

		VkResult Upload(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

		VkBuffer GetHandle() const { return m_Buffer; }
		VkDeviceSize GetSize() const { return m_Specification.Size; }
		const VulkanBufferSpecification& GetSpecification() const { return m_Specification; }
		void* GetMappedPointer() const { return m_MappedPointer; }
		bool IsHostCoherent() const { return m_HostCoherent; }

	private:
		VmaAllocator m_Allocator = VK_NULL_HANDLE;

		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		void* m_MappedPointer = nullptr;
		bool m_HostCoherent = false;

		VulkanBufferSpecification m_Specification{};
	};
}