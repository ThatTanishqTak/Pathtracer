#include "Engine/Renderer/Vulkan/VulkanAccelerationStructure.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanAccelerationStructure::VulkanAccelerationStructure() = default;

	VulkanAccelerationStructure::~VulkanAccelerationStructure()
	{
		Shutdown();
	}

	VkResult VulkanAccelerationStructure::Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator, const VulkanAccelerationStructureSpecification& specification)
	{
		if (m_Handle != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Acceleration structure '{}' is already initialized", m_DebugName);

			return VK_SUCCESS;
		}

		if (!device.IsInitialized() || !allocator.IsInitialized())
		{
			PT_CORE_ERROR("A valid device and allocator are required to create acceleration structure '{}'", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.Geometries.empty() || specification.Geometries.size() != specification.MaxPrimitiveCounts.size())
		{
			PT_CORE_ERROR("Acceleration structure '{}' needs at least one geometry and one primitive count per geometry", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN ACCELERATION STRUCTURE -------");

		m_Device = &device;
		m_Type = specification.Type;
		m_Flags = specification.Flags;
		m_MaxPrimitiveCounts.assign(specification.MaxPrimitiveCounts.begin(), specification.MaxPrimitiveCounts.end());
		m_DebugName = specification.DebugName;

		// 1. The sizes depend on the geometry types, the flags and the counts, never on the data addresses, which may still be anything here
		const VkAccelerationStructureBuildGeometryInfoKHR l_BuildInfo
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = m_Type,
			.flags = m_Flags,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.geometryCount = static_cast<uint32_t>(specification.Geometries.size()),
			.pGeometries = specification.Geometries.data(),
		};

		VkAccelerationStructureBuildSizesInfoKHR l_Sizes{ .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(device.GetHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &l_BuildInfo, m_MaxPrimitiveCounts.data(), &l_Sizes);

		if (l_Sizes.accelerationStructureSize == 0 || l_Sizes.buildScratchSize == 0)
		{
			PT_CORE_ERROR("Acceleration structure '{}' reported a zero size ({} bytes, {} scratch bytes)", m_DebugName, l_Sizes.accelerationStructureSize, l_Sizes.buildScratchSize);

			Shutdown();

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		m_BuildScratchSize = l_Sizes.buildScratchSize;

		// 2. The structure lives in its own device-local buffer at offset 0, which meets the 256 byte offset rule by construction
		const VulkanBufferSpecification l_BufferSpecification
		{
			.Size = l_Sizes.accelerationStructureSize,
			.Usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.Memory = BufferMemory::DeviceLocal,
			.DebugName = m_DebugName,
		};

		VkResult l_Result = m_Buffer.Initialize(allocator, l_BufferSpecification);
		if (l_Result != VK_SUCCESS)
		{
			Shutdown();

			return l_Result;
		}

		const VkAccelerationStructureCreateInfoKHR l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = m_Buffer.GetHandle(),
			.offset = 0,
			.size = l_Sizes.accelerationStructureSize,
			.type = m_Type,
		};

		l_Result = vkCreateAccelerationStructureKHR(device.GetHandle(), &l_CreateInfo, nullptr, &m_Handle);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateAccelerationStructureKHR for '{}': {}", m_DebugName, VulkanUtilities::ResultToString(l_Result));

			m_Handle = VK_NULL_HANDLE;
			Shutdown();

			return l_Result;
		}

		// 3. The address a TLAS instance names this structure by
		const VkAccelerationStructureDeviceAddressInfoKHR l_AddressInfo
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
			.accelerationStructure = m_Handle,
		};

		m_DeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device.GetHandle(), &l_AddressInfo);

		PT_CORE_TRACE("Acceleration Structure '{}' Created: {} bytes, {} scratch bytes, {}", m_DebugName, l_Sizes.accelerationStructureSize, m_BuildScratchSize, m_Type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? "top level" : "bottom level");
		PT_CORE_INFO("------- VULKAN ACCELERATION STRUCTURE INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanAccelerationStructure::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN ACCELERATION STRUCTURE -------");

		// The handle goes before the buffer it lives in
		if (m_Handle != VK_NULL_HANDLE)
		{
			vkDestroyAccelerationStructureKHR(m_Device->GetHandle(), m_Handle, nullptr);
			m_Handle = VK_NULL_HANDLE;
		}

		m_Buffer.Shutdown();

		PT_CORE_TRACE("Acceleration Structure '{}' Destroyed", m_DebugName);

		m_Device = nullptr;
		m_DeviceAddress = 0;
		m_Flags = 0;
		m_MaxPrimitiveCounts.clear();
		m_BuildScratchSize = 0;

		PT_CORE_INFO("------- VULKAN ACCELERATION STRUCTURE SHUTDOWN COMPLETE -------");
	}

	VkResult VulkanAccelerationStructure::RecordBuild(VkCommandBuffer commandBuffer, std::span<const VkAccelerationStructureGeometryKHR> geometries, std::span<const uint32_t> primitiveCounts, VkDeviceAddress scratchAddress) const
	{
		if (m_Handle == VK_NULL_HANDLE)
		{
			PT_CORE_ERROR("Acceleration structure '{}' is not initialized, nothing to build", m_DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (geometries.size() != m_MaxPrimitiveCounts.size() || primitiveCounts.size() != m_MaxPrimitiveCounts.size())
		{
			PT_CORE_ERROR("Acceleration structure '{}' was sized for {} geometries, the build names {} geometries and {} counts", m_DebugName, m_MaxPrimitiveCounts.size(), geometries.size(), primitiveCounts.size());

			return VK_ERROR_UNKNOWN;
		}

		const VkDeviceSize l_ScratchAlignment = m_Device->GetAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;
		if (scratchAddress == 0 || (l_ScratchAlignment > 0 && scratchAddress % l_ScratchAlignment != 0))
		{
			PT_CORE_ERROR("Acceleration structure '{}' was given scratch address {:#x}, it must be non-zero and aligned to {}", m_DebugName, scratchAddress, l_ScratchAlignment);

			return VK_ERROR_UNKNOWN;
		}

		// A structure is only as large as the counts it was sized for, a bigger build would write past its buffer
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> l_Ranges(geometries.size());
		for (size_t i_Geometry = 0; i_Geometry < geometries.size(); ++i_Geometry)
		{
			if (primitiveCounts[i_Geometry] > m_MaxPrimitiveCounts[i_Geometry])
			{
				PT_CORE_ERROR("Acceleration structure '{}' geometry {} builds {} primitives, it was sized for {}", m_DebugName, i_Geometry, primitiveCounts[i_Geometry], m_MaxPrimitiveCounts[i_Geometry]);

				return VK_ERROR_UNKNOWN;
			}

			// The geometry's data addresses already point at its first primitive, so every offset is zero
			l_Ranges[i_Geometry] = VkAccelerationStructureBuildRangeInfoKHR{ .primitiveCount = primitiveCounts[i_Geometry] };
		}

		const VkAccelerationStructureBuildGeometryInfoKHR l_BuildInfo
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = m_Type,
			.flags = m_Flags,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.dstAccelerationStructure = m_Handle,
			.geometryCount = static_cast<uint32_t>(geometries.size()),
			.pGeometries = geometries.data(),
			.scratchData = {.deviceAddress = scratchAddress },
		};

		const VkAccelerationStructureBuildRangeInfoKHR* l_RangePointer = l_Ranges.data();
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &l_BuildInfo, &l_RangePointer);

		return VK_SUCCESS;
	}
}