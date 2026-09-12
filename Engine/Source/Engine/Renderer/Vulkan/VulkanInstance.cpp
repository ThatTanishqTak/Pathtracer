#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"

#include <SDL3/SDL_vulkan.h>

#include <cstdint>
#include <cstring>

namespace Engine
{
	namespace
	{
#ifdef PT_DEBUG
		constexpr const char* k_ValidationLayerName = "VK_LAYER_KHRONOS_validation";

		bool IsExtensionEnabled(const std::vector<const char*>& enabled, const char* name)
		{
			for (const char* l_Extension : enabled)
			{
				if (std::strcmp(l_Extension, name) == 0)
				{
					return true;
				}
			}

			return false;
		}

		bool IsLayerSupported(const std::vector<VkLayerProperties>& available, const char* name)
		{
			for (const VkLayerProperties& l_Layer : available)
			{
				if (std::strcmp(l_Layer.layerName, name) == 0)
				{
					return true;
				}
			}

			return false;
		}
#endif
	}

	VulkanInstance::VulkanInstance() = default;
	VulkanInstance::~VulkanInstance() = default;

	void VulkanInstance::Initialize(const Window& window)
	{
		if (m_Instance != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan instance is already initialized");

			return;
		}

		if (!window.IsValid())
		{
			PT_CORE_CRITICAL("A valid window is required to initialize the Vulkan instance");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN INSTANCE -------");

		InitializeVolk();

		if (!m_VolkInitialized)
		{
			return;
		}

		std::vector<const char*> l_Extensions;
		GetRequiredExtensions(l_Extensions);

		if (l_Extensions.empty())
		{
			Shutdown();

			return;
		}

		std::vector<const char*> l_Layers;
		GetRequiredLayers(l_Layers);

		CreateInstance(l_Extensions, l_Layers);

		if (m_Instance == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		// Instance level entry points, device level ones are loaded after device creation
		volkLoadInstanceOnly(m_Instance);

		SetupDebugMessenger(l_Extensions);

		PT_CORE_INFO("------- VULKAN INSTANCE INITIALIZED -------");
	}

	void VulkanInstance::Shutdown()
	{
		if (!m_VolkInitialized && m_Instance == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN INSTANCE -------");

		if (m_DebugMessenger != VK_NULL_HANDLE)
		{
			PT_CORE_TRACE("Destroying Debug Messenger");

			vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
			m_DebugMessenger = VK_NULL_HANDLE;

			PT_CORE_TRACE("Debug Messenger Destroyed");
		}

		if (m_Instance != VK_NULL_HANDLE)
		{
			PT_CORE_TRACE("Destroying Vulkan Instance");

			vkDestroyInstance(m_Instance, nullptr);
			m_Instance = VK_NULL_HANDLE;

			PT_CORE_TRACE("Vulkan Instance Destroyed");
		}

		if (m_VolkInitialized)
		{
			PT_CORE_TRACE("Deinitializing Volk");

			volkFinalize();
			m_VolkInitialized = false;

			PT_CORE_TRACE("Volk Deinitialized");
		}

		PT_CORE_INFO("------- VULKAN INSTANCE SHUTDOWN COMPLETE -------");
	}

	void VulkanInstance::InitializeVolk()
	{
		PT_CORE_TRACE("Initializing Volk");

		if (volkInitialize() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to initialize volk, no Vulkan loader was found");

			return;
		}

		m_VolkInitialized = true;

		const uint32_t l_LoaderVersion = volkGetInstanceVersion();

		PT_CORE_TRACE("Vulkan loader version: {}.{}.{}", VK_API_VERSION_MAJOR(l_LoaderVersion), VK_API_VERSION_MINOR(l_LoaderVersion), VK_API_VERSION_PATCH(l_LoaderVersion));

		if (l_LoaderVersion < VK_API_VERSION_1_4)
		{
			PT_CORE_CRITICAL("Vulkan 1.4 is required, the installed loader is too old");

			volkFinalize();
			m_VolkInitialized = false;

			return;
		}

		PT_CORE_TRACE("Volk Initialized");
	}

	void VulkanInstance::GetRequiredExtensions(std::vector<const char*>& extensions)
	{
		PT_CORE_TRACE("Getting Required Extensions");

		extensions.clear();

		std::vector<VkExtensionProperties> l_Available;
		if (VulkanUtilities::Enumerate(l_Available, [](uint32_t* count, VkExtensionProperties* data) { return vkEnumerateInstanceExtensionProperties(nullptr, count, data); }) != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to enumerate instance extensions");

			return;
		}

		uint32_t l_WindowExtensionCount = 0;
		const char* const* l_WindowExtensions = SDL_Vulkan_GetInstanceExtensions(&l_WindowExtensionCount);

		if (!l_WindowExtensions || l_WindowExtensionCount == 0)
		{
			PT_CORE_CRITICAL("Failed to query SDL Vulkan instance extensions: {}", SDL_GetError());

			return;
		}

		for (uint32_t i = 0; i < l_WindowExtensionCount; i++)
		{
			if (!VulkanUtilities::IsExtensionSupported(l_Available, l_WindowExtensions[i]))
			{
				PT_CORE_CRITICAL("Required instance extension is not supported: {}", l_WindowExtensions[i]);

				extensions.clear();

				return;
			}

			extensions.push_back(l_WindowExtensions[i]);
		}

#ifdef PT_DEBUG
		if (VulkanUtilities::IsExtensionSupported(l_Available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		else
		{
			PT_CORE_WARN("{} is not available, debug messages are disabled", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
#endif

		for (const char* l_Extension : extensions)
		{
			PT_CORE_TRACE("Enabled Instance Extension: {}", l_Extension);
		}
	}

	void VulkanInstance::GetRequiredLayers(std::vector<const char*>& layers)
	{
		PT_CORE_TRACE("Getting Required Layers");

		layers.clear();

#ifdef PT_DEBUG
		std::vector<VkLayerProperties> l_Available;
		if (VulkanUtilities::Enumerate(l_Available, [](uint32_t* count, VkLayerProperties* data) { return vkEnumerateInstanceLayerProperties(count, data); }) != VK_SUCCESS)
		{
			PT_CORE_WARN("Failed to enumerate instance layers, validation is disabled");

			return;
		}

		if (!IsLayerSupported(l_Available, k_ValidationLayerName))
		{
			PT_CORE_WARN("{} is not installed, validation is disabled", k_ValidationLayerName);

			return;
		}

		layers.push_back(k_ValidationLayerName);

		PT_CORE_TRACE("Enabled instance layer: {}", k_ValidationLayerName);
#endif
	}

	void VulkanInstance::CreateInstance(const std::vector<const char*>& extensions, const std::vector<const char*>& layers)
	{
		PT_CORE_TRACE("Creating Vulkan Instance");

		VkApplicationInfo l_ApplicationInfo
		{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "Pathtracer",
			.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.pEngineName = "Engine",
			.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
			.apiVersion = VK_API_VERSION_1_4,
		};

		VkInstanceCreateInfo l_InstanceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &l_ApplicationInfo,
			.enabledLayerCount = static_cast<uint32_t>(layers.size()),
			.ppEnabledLayerNames = layers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
			.ppEnabledExtensionNames = extensions.data(),
		};

#ifdef PT_DEBUG
		// Covers vkCreateInstance and vkDestroyInstance, which the standalone messenger cannot see
		VkDebugUtilsMessengerCreateInfoEXT l_DebugMessengerCreateInfo = MakeDebugMessengerCreateInfo();
		if (IsExtensionEnabled(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			l_InstanceCreateInfo.pNext = &l_DebugMessengerCreateInfo;
		}
#endif

		const VkResult l_Result = vkCreateInstance(&l_InstanceCreateInfo, nullptr, &m_Instance);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("vkCreateInstance failed with result {}", static_cast<int>(l_Result));

			m_Instance = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Vulkan Instance Created");
	}

	void VulkanInstance::SetupDebugMessenger(const std::vector<const char*>& extensions)
	{
		(void)extensions;

#ifdef PT_DEBUG
		if (!IsExtensionEnabled(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			return;
		}

		if (!vkCreateDebugUtilsMessengerEXT)
		{
			PT_CORE_WARN("vkCreateDebugUtilsMessengerEXT was not loaded");

			return;
		}

		PT_CORE_TRACE("Setting Up Debug Messenger");

		const VkDebugUtilsMessengerCreateInfoEXT l_DebugMessengerCreateInfo = MakeDebugMessengerCreateInfo();

		const VkResult l_Result = vkCreateDebugUtilsMessengerEXT(m_Instance, &l_DebugMessengerCreateInfo, nullptr, &m_DebugMessenger);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("vkCreateDebugUtilsMessengerEXT failed with result {}", static_cast<int>(l_Result));

			m_DebugMessenger = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Debug Messenger Setup Complete");
#endif
	}

	VkDebugUtilsMessengerCreateInfoEXT VulkanInstance::MakeDebugMessengerCreateInfo()
	{
		return VkDebugUtilsMessengerCreateInfoEXT
		{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = &DebugCallback,
			.pUserData = nullptr,
		};
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
	{
		(void)userData;
		(void)types;
		
		const char* l_Message = (callbackData && callbackData->pMessage) ? callbackData->pMessage : "<null>";
		if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		{
			PT_CORE_ERROR("[VULKAN]: {}", l_Message);
		}
		else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			PT_CORE_WARN("[VULKAN]: {}", l_Message);
		}
		else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		{
			PT_CORE_INFO("[VULKAN]: {}", l_Message);
		}
		else
		{
			PT_CORE_TRACE("[VULKAN]: {}", l_Message);
		}

		return VK_FALSE;
	}
}