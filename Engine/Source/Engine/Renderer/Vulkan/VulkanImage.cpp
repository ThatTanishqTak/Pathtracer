#include "Engine/Renderer/Vulkan/VulkanImage.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanImage::VulkanImage() = default;
	VulkanImage::~VulkanImage() = default;

	VkResult VulkanImage::Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator, const VulkanImageSpecification& specification)
	{
		if (m_Image != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan image '{}' is already initialized", m_Specification.DebugName);

			return VK_SUCCESS;
		}

		if (!device.IsInitialized() || !allocator.IsInitialized())
		{
			PT_CORE_ERROR("A valid device and allocator are required to create image '{}'", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.Width == 0 || specification.Height == 0 || specification.Format == VK_FORMAT_UNDEFINED || specification.Usage == 0)
		{
			PT_CORE_ERROR("Image '{}' needs a non-zero extent, a format and at least one usage flag", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN IMAGE -------");

		// Query support for every requested use before creating anything, a format that lacks a feature would fail late and unclearly
		VkFormatProperties l_FormatProperties{};
		vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), specification.Format, &l_FormatProperties);

		const VkFormatFeatureFlags l_RequiredFeatures = FormatFeaturesForUsage(specification.Usage) | specification.AdditionalFormatFeatures;
		if ((l_FormatProperties.optimalTilingFeatures & l_RequiredFeatures) != l_RequiredFeatures)
		{
			PT_CORE_ERROR("Image '{}': format {} does not support the requested usages with optimal tiling (required {:#x}, supported {:#x})", specification.DebugName, static_cast<int>(specification.Format), l_RequiredFeatures, l_FormatProperties.optimalTilingFeatures);

			return VK_ERROR_FORMAT_NOT_SUPPORTED;
		}

		m_Device = &device;
		m_Allocator = allocator.GetHandle();
		m_Specification = specification;

		VkImageCreateInfo l_ImageCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = specification.Format,
			.extent = VkExtent3D{ specification.Width, specification.Height, 1 },
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = specification.Usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		// Render-target sized images get their own allocation, which keeps resize churn from fragmenting the shared blocks
		VmaAllocationCreateInfo l_AllocationCreateInfo
		{
			.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO,
		};

		VkResult l_Result = vmaCreateImage(m_Allocator, &l_ImageCreateInfo, &l_AllocationCreateInfo, &m_Image, &m_Allocation, nullptr);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vmaCreateImage for '{}' ({}x{}): {}", specification.DebugName, specification.Width, specification.Height, VulkanUtilities::ResultToString(l_Result));

			m_Image = VK_NULL_HANDLE;
			m_Allocation = VK_NULL_HANDLE;
			m_Allocator = VK_NULL_HANDLE;
			m_Device = nullptr;

			return l_Result;
		}

		VkImageViewCreateInfo l_ViewCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = m_Image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = specification.Format,
			.subresourceRange = GetColorRange(),
		};

		l_Result = vkCreateImageView(m_Device->GetHandle(), &l_ViewCreateInfo, nullptr, &m_View);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateImageView for '{}': {}", specification.DebugName, VulkanUtilities::ResultToString(l_Result));

			m_View = VK_NULL_HANDLE;

			Shutdown();

			return l_Result;
		}

		m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;

		PT_CORE_TRACE("Image '{}' Created: {}x{}", specification.DebugName, specification.Width, specification.Height);
		PT_CORE_INFO("------- VULKAN IMAGE INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanImage::Shutdown()
	{
		if (m_Image == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN IMAGE -------");

		// The caller has retired every GPU user, the renderer does that with the swapchain's device wait on recreate and at shutdown
		if (m_View != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_Device->GetHandle(), m_View, nullptr);
			m_View = VK_NULL_HANDLE;
		}

		vmaDestroyImage(m_Allocator, m_Image, m_Allocation);

		PT_CORE_TRACE("Image '{}' Destroyed", m_Specification.DebugName);

		m_Image = VK_NULL_HANDLE;
		m_Allocation = VK_NULL_HANDLE;
		m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		m_Allocator = VK_NULL_HANDLE;
		m_Device = nullptr;
		m_Specification = VulkanImageSpecification{};

		PT_CORE_INFO("------- VULKAN IMAGE SHUTDOWN COMPLETE -------");
	}

	void VulkanImage::RecordTransition(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, bool discardContents)
	{
		if (m_Image == VK_NULL_HANDLE)
		{
			return;
		}

		VkImageMemoryBarrier2 l_Barrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = srcStageMask,
			.srcAccessMask = srcAccessMask,
			.dstStageMask = dstStageMask,
			.dstAccessMask = dstAccessMask,
			.oldLayout = discardContents ? VK_IMAGE_LAYOUT_UNDEFINED : m_Layout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_Image,
			.subresourceRange = GetColorRange(),
		};

		VkDependencyInfo l_Dependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_Barrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_Dependency);

		m_Layout = newLayout;
	}

	VkImageSubresourceRange VulkanImage::GetColorRange()
	{
		return VkImageSubresourceRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
	}

	VkFormatFeatureFlags VulkanImage::FormatFeaturesForUsage(VkImageUsageFlags usage)
	{
		VkFormatFeatureFlags l_Features = 0;

		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
		{
			l_Features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		}

		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
		{
			l_Features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		}

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			l_Features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
		}

		if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		{
			l_Features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
		}

		if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		{
			l_Features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		}

		return l_Features;
	}
}