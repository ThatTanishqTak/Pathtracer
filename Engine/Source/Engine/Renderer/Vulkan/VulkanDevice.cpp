#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"

#include <cstring>
#include <iterator>

namespace Engine
{
	namespace
	{
		constexpr const char* k_DeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		bool IsExtensionSupported(const std::vector<VkExtensionProperties>& available, const char* name)
		{
			for (const VkExtensionProperties& l_Extension : available)
			{
				if (std::strcmp(l_Extension.extensionName, name) == 0)
				{
					return true;
				}
			}

			return false;
		}

		bool IsDeviceExtensionSupported(VkPhysicalDevice device, const char* name)
		{
			std::vector<VkExtensionProperties> l_Available;
			uint32_t l_AvailableCount = 0;
			VkResult l_EnumerateResult = VK_INCOMPLETE;

			// The set can change between the count and fill calls, VK_INCOMPLETE means retry
			do
			{
				l_EnumerateResult = vkEnumerateDeviceExtensionProperties(device, nullptr, &l_AvailableCount, nullptr);

				if (l_EnumerateResult != VK_SUCCESS)
				{
					break;
				}

				l_Available.resize(l_AvailableCount);
				l_EnumerateResult = vkEnumerateDeviceExtensionProperties(device, nullptr, &l_AvailableCount, l_Available.data());
			}
			while (l_EnumerateResult == VK_INCOMPLETE);

			if (l_EnumerateResult != VK_SUCCESS)
			{
				return false;
			}

			// The fill call wrote back the count it actually delivered
			l_Available.resize(l_AvailableCount);

			return IsExtensionSupported(l_Available, name);
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
		if (m_PhysicalDevice == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN DEVICE -------");

		if (m_Device == VK_NULL_HANDLE && m_PhysicalDevice == VK_NULL_HANDLE)
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

		uint32_t l_DeviceCount = 0;
		VkResult l_EnumerateResult = VK_INCOMPLETE;

		// The set can change between the count and fill calls, VK_INCOMPLETE means retry
		do
		{
			l_EnumerateResult = vkEnumeratePhysicalDevices(instance.GetHandle(), &l_DeviceCount, nullptr);
			if (l_EnumerateResult != VK_SUCCESS)
			{
				break;
			}

			devices.resize(l_DeviceCount);
			l_EnumerateResult = vkEnumeratePhysicalDevices(instance.GetHandle(), &l_DeviceCount, devices.data());
		}
		while (l_EnumerateResult == VK_INCOMPLETE);

		if (l_EnumerateResult != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to enumerate physical devices");

			devices.clear();

			return;
		}

		// The fill call wrote back the count it actually delivered
		devices.resize(l_DeviceCount);

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

		for (VkPhysicalDevice l_Device : devices)
		{
			VkPhysicalDeviceProperties l_Properties{};
			vkGetPhysicalDeviceProperties(l_Device, &l_Properties);

			PT_CORE_TRACE("-------{}", l_Properties.deviceName);

			uint32_t l_GraphicsQueueFamilyIndex = UINT32_MAX;
			if (!IsDeviceSuitable(l_Device, l_GraphicsQueueFamilyIndex))
			{
				continue;
			}

			const uint64_t l_Score = ScoreDevice(l_Device);
			if (l_Score > l_BestScore)
			{
				l_BestDevice = l_Device;
				l_BestGraphicsQueueFamilyIndex = l_GraphicsQueueFamilyIndex;
				l_BestScore = l_Score;
			}
		}

		if (l_BestDevice == VK_NULL_HANDLE)
		{
			PT_CORE_CRITICAL("No suitable GPU was found");

			return;
		}

		m_PhysicalDevice = l_BestDevice;
		m_GraphicsQueueFamilyIndex = l_BestGraphicsQueueFamilyIndex;

		VkPhysicalDeviceProperties l_Properties{};
		vkGetPhysicalDeviceProperties(m_PhysicalDevice, &l_Properties);

		PT_CORE_INFO("Selected GPU: {} ({})", l_Properties.deviceName, DeviceTypeToString(l_Properties.deviceType));
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

	bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device, uint32_t& graphicsQueueFamilyIndex)
	{
		VkPhysicalDeviceProperties l_Properties{};
		vkGetPhysicalDeviceProperties(device, &l_Properties);

		if (l_Properties.apiVersion < VK_API_VERSION_1_4 || !FindGraphicsQueueFamily(device, graphicsQueueFamilyIndex) || !IsDeviceExtensionSupported(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			return false;
		}

		return true;
	}

	uint64_t VulkanDevice::ScoreDevice(VkPhysicalDevice device)
	{
		VkPhysicalDeviceProperties l_Properties{};
		vkGetPhysicalDeviceProperties(device, &l_Properties);

		uint64_t l_Score = 0;
		switch (l_Properties.deviceType)
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