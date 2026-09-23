#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"

#include <volk.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Engine
{
	class VulkanDevice;
	class VulkanMemoryAllocator;

	struct VulkanAccelerationStructureSpecification
	{
		VkAccelerationStructureTypeKHR Type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		VkBuildAccelerationStructureFlagsKHR Flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

		// What the structure is sized for: every later build uses the same geometry types and flags with at most these primitive counts, one count per geometry
		std::span<const VkAccelerationStructureGeometryKHR> Geometries;
		std::span<const uint32_t> MaxPrimitiveCounts;

		const char* DebugName = "acceleration structure";
	};

	// One bottom- or top-level acceleration structure: its device-local buffer, handle and device address. Holds neither the build inputs nor the scratch, the caller keeps both alive until the batch that recorded the build retires
	class VulkanAccelerationStructure
	{
	public:
		VulkanAccelerationStructure();
		~VulkanAccelerationStructure();

		VulkanAccelerationStructure(const VulkanAccelerationStructure&) = delete;
		VulkanAccelerationStructure& operator=(const VulkanAccelerationStructure&) = delete;
		VulkanAccelerationStructure(VulkanAccelerationStructure&&) = delete;
		VulkanAccelerationStructure& operator=(VulkanAccelerationStructure&&) = delete;

		// Queries the build sizes and creates the buffer and the handle, nothing is built yet
		VkResult Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator, const VulkanAccelerationStructureSpecification& specification);
		void Shutdown();

		bool IsInitialized() const { return m_Handle != VK_NULL_HANDLE; }

		// Records a full build, never an update. The geometries must match the ones Initialize sized for in type and count, the primitive counts may be lower. The scratch address must be aligned to minAccelerationStructureScratchOffsetAlignment and hold GetBuildScratchSize bytes
		VkResult RecordBuild(VkCommandBuffer commandBuffer, std::span<const VkAccelerationStructureGeometryKHR> geometries, std::span<const uint32_t> primitiveCounts, VkDeviceAddress scratchAddress) const;

		VkAccelerationStructureKHR GetHandle() const { return m_Handle; }
		VkDeviceAddress GetDeviceAddress() const { return m_DeviceAddress; }
		VkDeviceSize GetSize() const { return m_Buffer.GetSize(); }
		VkDeviceSize GetBuildScratchSize() const { return m_BuildScratchSize; }

	private:
		const VulkanDevice* m_Device = nullptr;

		VulkanBuffer m_Buffer;
		VkAccelerationStructureKHR m_Handle = VK_NULL_HANDLE;
		VkDeviceAddress m_DeviceAddress = 0;

		VkAccelerationStructureTypeKHR m_Type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		VkBuildAccelerationStructureFlagsKHR m_Flags = 0;
		std::vector<uint32_t> m_MaxPrimitiveCounts;
		VkDeviceSize m_BuildScratchSize = 0;

		const char* m_DebugName = "acceleration structure";
	};
}