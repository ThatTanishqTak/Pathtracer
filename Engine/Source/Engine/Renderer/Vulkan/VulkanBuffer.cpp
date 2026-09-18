#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"

#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

#include <cstddef>
#include <cstring>

namespace Engine
{
	VulkanBuffer::VulkanBuffer() = default;
	VulkanBuffer::~VulkanBuffer() = default;

	VkResult VulkanBuffer::Initialize(const VulkanMemoryAllocator& allocator, const VulkanBufferSpecification& specification)
	{
		if (m_Buffer != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan buffer '{}' is already initialized", m_Specification.DebugName);

			return VK_SUCCESS;
		}

		if (!allocator.IsInitialized())
		{
			PT_CORE_ERROR("A valid allocator is required to create buffer '{}'", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.Size == 0 || specification.Usage == 0)
		{
			PT_CORE_ERROR("Buffer '{}' needs a non-zero size and at least one usage flag", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN BUFFER -------");

		m_Allocator = allocator.GetHandle();
		m_Specification = specification;

		VkBufferCreateInfo l_BufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = specification.Size,
			.usage = specification.Usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		VmaAllocationCreateInfo l_AllocationCreateInfo
		{
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		if (specification.Memory == BufferMemory::HostUpload)
		{
			// Sequential write access lets VMA pick host-visible memory, the persistent mapping avoids a map/unmap pair per upload
			l_AllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}

		VmaAllocationInfo l_AllocationInfo{};
		const VkResult l_Result = vmaCreateBuffer(m_Allocator, &l_BufferCreateInfo, &l_AllocationCreateInfo, &m_Buffer, &m_Allocation, &l_AllocationInfo);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vmaCreateBuffer for '{}' ({} bytes): {}", specification.DebugName, specification.Size, VulkanUtilities::ResultToString(l_Result));

			m_Buffer = VK_NULL_HANDLE;
			m_Allocation = VK_NULL_HANDLE;
			m_Allocator = VK_NULL_HANDLE;

			return l_Result;
		}

		if (specification.Memory == BufferMemory::HostUpload)
		{
			m_MappedPointer = l_AllocationInfo.pMappedData;

			VkMemoryPropertyFlags l_MemoryProperties = 0;
			vmaGetAllocationMemoryProperties(m_Allocator, m_Allocation, &l_MemoryProperties);
			m_HostCoherent = (l_MemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

			if (m_MappedPointer == nullptr)
			{
				PT_CORE_ERROR("Buffer '{}' requested a persistent mapping but none was provided", specification.DebugName);

				Shutdown();

				return VK_ERROR_MEMORY_MAP_FAILED;
			}
		}

		PT_CORE_TRACE("Buffer '{}' Created: {} bytes, {}", specification.DebugName, specification.Size, specification.Memory == BufferMemory::HostUpload ? (m_HostCoherent ? "host upload, coherent" : "host upload, flushed") : "device local");
		PT_CORE_INFO("------- VULKAN BUFFER INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanBuffer::Shutdown()
	{
		if (m_Buffer == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN BUFFER -------");

		// The persistent mapping is released together with the allocation
		vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);

		PT_CORE_TRACE("Buffer '{}' Destroyed", m_Specification.DebugName);

		m_Buffer = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_MappedPointer = nullptr;
		m_HostCoherent = false;
		m_Allocator = VK_NULL_HANDLE;
		m_Specification = VulkanBufferSpecification{};

		PT_CORE_INFO("------- VULKAN BUFFER SHUTDOWN COMPLETE -------");
	}

	VkResult VulkanBuffer::Upload(const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		if (m_MappedPointer == nullptr)
		{
			PT_CORE_ERROR("Buffer '{}' is not host mapped, Upload requires BufferMemory::HostUpload", m_Specification.DebugName);

			return VK_ERROR_MEMORY_MAP_FAILED;
		}

		if (data == nullptr || size == 0 || offset > m_Specification.Size || size > m_Specification.Size - offset)
		{
			PT_CORE_ERROR("Upload of {} bytes at offset {} does not fit buffer '{}' of {} bytes", size, offset, m_Specification.DebugName, m_Specification.Size);

			return VK_ERROR_UNKNOWN;
		}

		std::memcpy(static_cast<std::byte*>(m_MappedPointer) + offset, data, static_cast<size_t>(size));

		if (m_HostCoherent)
		{
			return VK_SUCCESS;
		}

		// Non-coherent memory needs an explicit flush before the GPU can observe the write, VMA rounds the range to nonCoherentAtomSize
		const VkResult l_Result = vmaFlushAllocation(m_Allocator, m_Allocation, offset, size);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vmaFlushAllocation for '{}': {}", m_Specification.DebugName, VulkanUtilities::ResultToString(l_Result));
		}

		return l_Result;
	}
}