#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanMemoryAllocator::VulkanMemoryAllocator() = default;
	VulkanMemoryAllocator::~VulkanMemoryAllocator() = default;
	
	void VulkanMemoryAllocator::Initialize(const VulkanInstance& instance, const VulkanDevice& device)
	{
		if (m_VulkanMemoryAllocator != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan allocator is already initialized");

			return;
		}

		if (!instance.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid Vulkan instance is required to create the allocator");

			return;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid logical device is required to create the allocator");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN MEMORY ALLOCATOR -------");

		CreateAllocator(instance, device);

		PT_CORE_INFO("------- VULKAN MEMORY ALLOCATOR INITIALIZED -------");
	}

	void VulkanMemoryAllocator::Shutdown()
	{
		if (m_VulkanMemoryAllocator == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN ALLOCATOR -------");

		PT_CORE_TRACE("Destroying Allocator");

		vmaDestroyAllocator(m_VulkanMemoryAllocator);
		m_VulkanMemoryAllocator = VK_NULL_HANDLE;

		PT_CORE_TRACE("Allocator Destroyed");

		PT_CORE_INFO("------- VULKAN ALLOCATOR SHUTDOWN COMPLETE -------");
	}

	void VulkanMemoryAllocator::CreateAllocator(const VulkanInstance& instance, const VulkanDevice& device)
	{
		PT_CORE_TRACE("Creating Allocator");

		VmaVulkanFunctions l_Functions
		{
			.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
			.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
		};

		VmaAllocatorCreateInfo l_AllocatorCreateInfo
		{
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT,
			.physicalDevice = device.GetPhysicalDevice(),
			.device = device.GetHandle(),
			.pVulkanFunctions = &l_Functions,
			.instance = instance.GetHandle(),
			.vulkanApiVersion = VK_API_VERSION_1_4,
		};

		const VkResult l_Result = vmaCreateAllocator(&l_AllocatorCreateInfo, &m_VulkanMemoryAllocator);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vmaCreateAllocator: {}", static_cast<int>(l_Result));

			m_VulkanMemoryAllocator = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Allocator Created");
	}
}