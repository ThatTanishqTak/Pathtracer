#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstdint>
#include <vector>

namespace Engine
{
	class VulkanInstance;

	class VulkanDevice
	{
	public:
		VulkanDevice();
		~VulkanDevice();

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;
		VulkanDevice(VulkanDevice&&) = delete;
		VulkanDevice& operator=(VulkanDevice&&) = delete;

		void Initialize(const VulkanInstance& instance);
		void Shutdown();

		bool IsInitialized() const { return m_Device != VK_NULL_HANDLE; }

		VkDevice GetHandle() const { return m_Device; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }

	private:
		void EnumeratePhysicalDevices(const VulkanInstance& instance, std::vector<VkPhysicalDevice>& devices);
		void PickPhysicalDevice(const std::vector<VkPhysicalDevice>& devices);
		void CreateLogicalDevice();

		static bool IsDeviceSuitable(VkPhysicalDevice device, uint32_t& graphicsQueueFamilyIndex);
		static uint64_t ScoreDevice(VkPhysicalDevice device);

	private:
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_GraphicsQueueFamilyIndex = UINT32_MAX;
	};
}