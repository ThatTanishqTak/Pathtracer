#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <vector>

namespace Engine
{
	class VulkanInstance
	{
	public:
		VulkanInstance();
		~VulkanInstance();

		VulkanInstance(const VulkanInstance&) = delete;
		VulkanInstance& operator=(const VulkanInstance&) = delete;
		VulkanInstance(VulkanInstance&&) = delete;
		VulkanInstance& operator=(VulkanInstance&&) = delete;

		void Initialize();
		void Shutdown();

		bool IsInitialized() const { return m_Instance != VK_NULL_HANDLE; }

		VkInstance GetHandle() const { return m_Instance; }

	private:
		void InitializeVolk();
		void GetRequiredExtensions(std::vector<const char*>& extensions);
		void GetRequiredLayers(std::vector<const char*>& layers);
		void CreateInstance(const std::vector<const char*>& extensions, const std::vector<const char*>& layers);
		void SetupDebugMessenger(const std::vector<const char*>& extensions);

		static VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo();

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);

	private:
		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

		bool m_VolkInitialized = false;
	};
}