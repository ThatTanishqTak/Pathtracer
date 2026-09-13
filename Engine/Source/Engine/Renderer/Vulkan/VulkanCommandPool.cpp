#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanCommandPool::VulkanCommandPool() = default;
	VulkanCommandPool::~VulkanCommandPool() = default;

	void VulkanCommandPool::Initialize(const VulkanDevice& device, uint32_t commandBufferCount)
	{
		if (m_CommandPool != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan command pool is already initialized");

			return;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid logical device is required to create a command pool");

			return;
		}

		if (commandBufferCount == 0)
		{
			PT_CORE_CRITICAL("At least one command buffer is required");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN COMMAND POOL -------");

		m_Device = &device;

		CreateCommandPool();
		if (m_CommandPool == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		AllocateCommandBuffers(commandBufferCount);
		if (m_CommandBuffers.empty())
		{
			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN COMMAND POOL INITIALIZED -------");
	}

	void VulkanCommandPool::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN COMMAND POOL -------");

		if (m_Device->IsInitialized())
		{
			vkDeviceWaitIdle(m_Device->GetHandle());
		}

		// Destroying the pool frees every buffer allocated from it
		m_CommandBuffers.clear();
		DestroyCommandPool();

		m_Device = nullptr;

		PT_CORE_INFO("------- VULKAN COMMAND POOL SHUTDOWN COMPLETE -------");
	}

	bool VulkanCommandPool::Begin(uint32_t index)
	{
		VkCommandBuffer l_CommandBuffer = GetCommandBuffer(index);
		if (l_CommandBuffer == VK_NULL_HANDLE)
		{
			PT_CORE_ERROR("Command buffer index {} is out of range for {} command buffer(s)", index, m_CommandBuffers.size());

			return false;
		}

		VkResult l_Result = vkResetCommandBuffer(l_CommandBuffer, 0);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkResetCommandBuffer: {}", VulkanUtilities::ResultToString(l_Result));

			return false;
		}

		VkCommandBufferBeginInfo l_BeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		l_Result = vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkBeginCommandBuffer: {}", VulkanUtilities::ResultToString(l_Result));

			return false;
		}

		return true;
	}

	bool VulkanCommandPool::End(uint32_t index)
	{
		VkCommandBuffer l_CommandBuffer = GetCommandBuffer(index);
		if (l_CommandBuffer == VK_NULL_HANDLE)
		{
			PT_CORE_ERROR("Command buffer index {} is out of range for {} command buffer(s)", index, m_CommandBuffers.size());

			return false;
		}

		const VkResult l_Result = vkEndCommandBuffer(l_CommandBuffer);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkEndCommandBuffer: {}", VulkanUtilities::ResultToString(l_Result));

			return false;
		}

		return true;
	}

	VkCommandBuffer VulkanCommandPool::GetCommandBuffer(uint32_t index) const
	{
		if (index >= m_CommandBuffers.size())
		{
			return VK_NULL_HANDLE;
		}

		return m_CommandBuffers[index];
	}

	void VulkanCommandPool::CreateCommandPool()
	{
		PT_CORE_TRACE("Creating Command Pool");

		VkCommandPoolCreateInfo l_CommandPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = m_Device->GetGraphicsQueueFamilyIndex(),
		};

		const VkResult l_Result = vkCreateCommandPool(m_Device->GetHandle(), &l_CommandPoolCreateInfo, nullptr, &m_CommandPool);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkCreateCommandPool: {}", VulkanUtilities::ResultToString(l_Result));

			m_CommandPool = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Command Pool Created");
	}

	void VulkanCommandPool::AllocateCommandBuffers(uint32_t commandBufferCount)
	{
		PT_CORE_TRACE("Allocating Command Buffers");

		m_CommandBuffers.resize(commandBufferCount, VK_NULL_HANDLE);

		VkCommandBufferAllocateInfo l_AllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = m_CommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = commandBufferCount,
		};

		const VkResult l_Result = vkAllocateCommandBuffers(m_Device->GetHandle(), &l_AllocateInfo, m_CommandBuffers.data());
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkAllocateCommandBuffers: {}", VulkanUtilities::ResultToString(l_Result));

			m_CommandBuffers.clear();

			return;
		}

		PT_CORE_TRACE("Command Buffers Allocated: {}", m_CommandBuffers.size());
	}

	void VulkanCommandPool::DestroyCommandPool()
	{
		if (m_CommandPool == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_TRACE("Destroying Command Pool");

		vkDestroyCommandPool(m_Device->GetHandle(), m_CommandPool, nullptr);
		m_CommandPool = VK_NULL_HANDLE;

		PT_CORE_TRACE("Command Pool Destroyed");
	}
}