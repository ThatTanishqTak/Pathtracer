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
	class VulkanSurface;

	class VulkanDevice
	{
	public:
		VulkanDevice();
		~VulkanDevice();

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;
		VulkanDevice(VulkanDevice&&) = delete;
		VulkanDevice& operator=(VulkanDevice&&) = delete;

		void Initialize(const VulkanInstance& instance, const VulkanSurface& surface);
		void Shutdown();

		bool IsInitialized() const { return m_Device != VK_NULL_HANDLE; }

		VkResult WaitIdle() const;

		VkDevice GetHandle() const { return m_Device; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }

		const VkPhysicalDeviceAccelerationStructurePropertiesKHR& GetAccelerationStructureProperties() const { return m_AccelerationStructureProperties; }

	private:
		void EnumeratePhysicalDevices(const VulkanInstance& instance, std::vector<VkPhysicalDevice>& devices);
		void PickPhysicalDevice(const std::vector<VkPhysicalDevice>& devices, VkSurfaceKHR surface);
		void CreateLogicalDevice();
		void QueryDeviceProperties();

		static bool IsDeviceSuitable(VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties, VkSurfaceKHR surface, uint32_t& graphicsQueueFamilyIndex);
		static uint64_t ScoreDevice(VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties);

	private:
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_GraphicsQueueFamilyIndex = UINT32_MAX;
		VkPhysicalDeviceAccelerationStructurePropertiesKHR m_AccelerationStructureProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	};
}