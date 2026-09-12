#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"

#include <iterator>

namespace Engine
{
	namespace
	{
		constexpr const char* k_DeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		bool IsDeviceExtensionSupported(VkPhysicalDevice device, const char* name)
		{
			std::vector<VkExtensionProperties> l_Available;
			if (VulkanUtilities::Enumerate(l_Available, [device](uint32_t* count, VkExtensionProperties* data) { return vkEnumerateDeviceExtensionProperties(device, nullptr, count, data); }) != VK_SUCCESS)
			{
				return false;
			}

			return VulkanUtilities::IsExtensionSupported(l_Available, name);
		}

		bool FindGraphicsQueueFamily(VkPhysicalDevice device, uint32_t& index)
		{
			uint32_t l_QueueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &l_QueueFamilyCount, nullptr);

			std::vector<VkQueueFamilyProperties> l_QueueFamilies(l_QueueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &l_QueueFamilyCount, l_QueueFamilies.data());

			for (uint32_t i = 0; i < l_QueueFamilyCount; i++)
			{
				if (l_QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					index = i;

					return true;
				}
			}

			return false;
		}

		uint64_t GetDeviceLocalMemorySize(VkPhysicalDevice device)
		{
			VkPhysicalDeviceMemoryProperties l_MemoryProperties{};
			vkGetPhysicalDeviceMemoryProperties(device, &l_MemoryProperties);

			uint64_t l_Size = 0;
			for (uint32_t i = 0; i < l_MemoryProperties.memoryHeapCount; i++)
			{
				if (l_MemoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					l_Size += l_MemoryProperties.memoryHeaps[i].size;
				}
			}

			return l_Size;
		}

		const char* DeviceTypeToString(VkPhysicalDeviceType type)
		{
			switch (type)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
				{
					return "Discrete GPU";
				}
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
				{
					return "Integrated GPU";
				}
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
				{
					return "Virtual GPU";
				}
				case VK_PHYSICAL_DEVICE_TYPE_CPU:
				{
					return "CPU";
				}
				default:
				{
					return "Other";
				}
			}
		}
	}

	VulkanDevice::VulkanDevice() = default;
	VulkanDevice::~VulkanDevice() = default;

	void VulkanDevice::Initialize(const VulkanInstance& instance)
	{
		if (m_Device != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan device is already initialized");

			return;
		}

		if (!instance.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid Vulkan instance is required to select a physical device");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN DEVICE -------");

		std::vector<VkPhysicalDevice> l_Devices;
		EnumeratePhysicalDevices(instance, l_Devices);

		if (l_Devices.empty())
		{
			Shutdown();

			return;
		}

		PickPhysicalDevice(l_Devices);
		if (m_PhysicalDevice == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		CreateLogicalDevice();
		if (m_Device == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN DEVICE INITIALIZED -------");
	}

	void VulkanDevice::Shutdown()
	{
		if (m_Device == VK_NULL_HANDLE && m_PhysicalDevice == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN DEVICE -------");

		if (m_Device != VK_NULL_HANDLE)
		{
			PT_CORE_TRACE("Destroying Logical Device");

			vkDeviceWaitIdle(m_Device);

			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;
			m_GraphicsQueue = VK_NULL_HANDLE;

			PT_CORE_TRACE("Logical Device Destroyed");
		}

		PT_CORE_TRACE("Releasing Physical Device");

		m_PhysicalDevice = VK_NULL_HANDLE;
		m_GraphicsQueueFamilyIndex = UINT32_MAX;

		PT_CORE_TRACE("Physical Device Released");

		PT_CORE_INFO("------- VULKAN DEVICE SHUTDOWN COMPLETE -------");
	}

	void VulkanDevice::EnumeratePhysicalDevices(const VulkanInstance& instance, std::vector<VkPhysicalDevice>& devices)
	{
		PT_CORE_TRACE("Enumerating Physical Devices");

		devices.clear();

		VkInstance l_Instance = instance.GetHandle();
		if (VulkanUtilities::Enumerate(devices, [l_Instance](uint32_t* count, VkPhysicalDevice* data) { return vkEnumeratePhysicalDevices(l_Instance, count, data); }) != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to enumerate physical devices");

			return;
		}

		if (devices.empty())
		{
			PT_CORE_CRITICAL("No Vulkan capable GPU was found");

			return;
		}

		PT_CORE_TRACE("Found {} GPU(s)", devices.size());
	}

	void VulkanDevice::PickPhysicalDevice(const std::vector<VkPhysicalDevice>& devices)
	{
		PT_CORE_TRACE("Selecting Suitable GPU");

		VkPhysicalDevice l_BestDevice = VK_NULL_HANDLE;
		uint32_t l_BestGraphicsQueueFamilyIndex = UINT32_MAX;
		uint64_t l_BestScore = 0;
		VkPhysicalDeviceProperties l_BestProperties{};

		for (VkPhysicalDevice l_Device : devices)
		{
			VkPhysicalDeviceProperties l_Properties{};
			vkGetPhysicalDeviceProperties(l_Device, &l_Properties);

			PT_CORE_TRACE("-------{}", l_Properties.deviceName);

			uint32_t l_GraphicsQueueFamilyIndex = UINT32_MAX;
			if (!IsDeviceSuitable(l_Device, l_Properties, l_GraphicsQueueFamilyIndex))
			{
				continue;
			}

			const uint64_t l_Score = ScoreDevice(l_Device, l_Properties);
			if (l_Score > l_BestScore)
			{
				l_BestDevice = l_Device;
				l_BestGraphicsQueueFamilyIndex = l_GraphicsQueueFamilyIndex;
				l_BestScore = l_Score;
				l_BestProperties = l_Properties;
			}
		}

		if (l_BestDevice == VK_NULL_HANDLE)
		{
			PT_CORE_CRITICAL("No suitable GPU was found");

			return;
		}

		m_PhysicalDevice = l_BestDevice;
		m_GraphicsQueueFamilyIndex = l_BestGraphicsQueueFamilyIndex;

		PT_CORE_INFO("Selected GPU: {} ({})", l_BestProperties.deviceName, DeviceTypeToString(l_BestProperties.deviceType));
		PT_CORE_TRACE("Graphics Queue Family Index: {}", m_GraphicsQueueFamilyIndex);
	}

	void VulkanDevice::CreateLogicalDevice()
	{
		PT_CORE_TRACE("Creating Logical Device");

		// Query the supported core feature chain, declared newest first so each pNext can point at the next struct
		VkPhysicalDeviceVulkan14Features l_Supported14{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
		VkPhysicalDeviceVulkan13Features l_Supported13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &l_Supported14 };
		VkPhysicalDeviceVulkan12Features l_Supported12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &l_Supported13 };
		VkPhysicalDeviceVulkan11Features l_Supported11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &l_Supported12 };
		VkPhysicalDeviceFeatures2 l_SupportedFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &l_Supported11 };

		vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &l_SupportedFeatures);

		const struct { const char* Name; VkBool32 Supported; } l_RequiredFeatures[] =
		{
			{ "descriptorIndexing", l_Supported12.descriptorIndexing },
			{ "scalarBlockLayout", l_Supported12.scalarBlockLayout },
			{ "timelineSemaphore", l_Supported12.timelineSemaphore },
			{ "bufferDeviceAddress", l_Supported12.bufferDeviceAddress },
			{ "synchronization2", l_Supported13.synchronization2 },
			{ "dynamicRendering", l_Supported13.dynamicRendering },
			{ "maintenance4", l_Supported13.maintenance4 },
			{ "maintenance5", l_Supported14.maintenance5 },
			{ "maintenance6", l_Supported14.maintenance6 },
			{ "pushDescriptor", l_Supported14.pushDescriptor },
		};

		for (const auto& l_Feature : l_RequiredFeatures)
		{
			if (l_Feature.Supported != VK_TRUE)
			{
				PT_CORE_CRITICAL("Required device feature is not supported: {}", l_Feature.Name);

				return;
			}
		}

		// Enable exactly the features that were verified above, chained the same way
		VkPhysicalDeviceVulkan14Features l_Enabled14
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
			.maintenance5 = VK_TRUE,
			.maintenance6 = VK_TRUE,
			.pushDescriptor = VK_TRUE,
		};

		VkPhysicalDeviceVulkan13Features l_Enabled13
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.pNext = &l_Enabled14,
			.synchronization2 = VK_TRUE,
			.dynamicRendering = VK_TRUE,
			.maintenance4 = VK_TRUE,
		};

		VkPhysicalDeviceVulkan12Features l_Enabled12
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &l_Enabled13,
			.descriptorIndexing = VK_TRUE,
			.scalarBlockLayout = VK_TRUE,
			.timelineSemaphore = VK_TRUE,
			.bufferDeviceAddress = VK_TRUE,
		};

		VkPhysicalDeviceFeatures2 l_EnabledFeatures
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &l_Enabled12,
		};

		const float l_QueuePriority = 1.0f;

		VkDeviceQueueCreateInfo l_QueueCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = m_GraphicsQueueFamilyIndex,
			.queueCount = 1,
			.pQueuePriorities = &l_QueuePriority,
		};

		VkDeviceCreateInfo l_DeviceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &l_EnabledFeatures,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &l_QueueCreateInfo,
			.enabledExtensionCount = static_cast<uint32_t>(std::size(k_DeviceExtensions)),
			.ppEnabledExtensionNames = k_DeviceExtensions,
		};

		const VkResult l_Result = vkCreateDevice(m_PhysicalDevice, &l_DeviceCreateInfo, nullptr, &m_Device);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkCreateDevice: {}", static_cast<int>(l_Result));

			m_Device = VK_NULL_HANDLE;

			return;
		}

		// Device level entry points, skips the per-call instance dispatch
		volkLoadDevice(m_Device);

		vkGetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &m_GraphicsQueue);

		if (m_GraphicsQueue == VK_NULL_HANDLE)
		{
			PT_CORE_CRITICAL("Failed to retrieve the graphics queue");

			vkDestroyDevice(m_Device, nullptr);
			m_Device = VK_NULL_HANDLE;

			return;
		}

		for (const char* l_Extension : k_DeviceExtensions)
		{
			PT_CORE_TRACE("Enabled Device Extension: {}", l_Extension);
		}

		PT_CORE_TRACE("Logical Device Created");
	}

	bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties, uint32_t& graphicsQueueFamilyIndex)
	{
		if (properties.apiVersion < VK_API_VERSION_1_4 || !FindGraphicsQueueFamily(device, graphicsQueueFamilyIndex) || !IsDeviceExtensionSupported(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			return false;
		}

		return true;
	}

	uint64_t VulkanDevice::ScoreDevice(VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties)
	{
		uint64_t l_Score = 0;
		switch (properties.deviceType)
		{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			{
				l_Score += 1000000;
				break;
			}
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			{
				l_Score += 100000;
				break;
			}
			case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			{
				l_Score += 10000;
				break;
			}
			case VK_PHYSICAL_DEVICE_TYPE_CPU:
			{
				l_Score += 1000;
				break;
			}
			default:
			{
				l_Score += 100;
				break;
			}
		}

		l_Score += GetDeviceLocalMemorySize(device) / (1024 * 1024);

		return l_Score;
	}
}