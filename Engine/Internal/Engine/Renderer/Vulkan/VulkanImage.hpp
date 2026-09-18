#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>

namespace Engine
{
	class VulkanDevice;
	class VulkanMemoryAllocator;

	struct VulkanImageSpecification
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		VkFormat Format = VK_FORMAT_UNDEFINED;
		VkImageUsageFlags Usage = 0;
		VkFormatFeatureFlags AdditionalFormatFeatures = 0;

		const char* DebugName = "image";
	};

	// A 2D single-mip color image
	class VulkanImage
	{
	public:
		VulkanImage();
		~VulkanImage();

		VulkanImage(const VulkanImage&) = delete;
		VulkanImage& operator=(const VulkanImage&) = delete;
		VulkanImage(VulkanImage&&) = delete;
		VulkanImage& operator=(VulkanImage&&) = delete;

		// Verifies the format supports the requested usages with optimal tiling before creating anything
		VkResult Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator, const VulkanImageSpecification& specification);
		void Shutdown();

		bool IsInitialized() const { return m_Image != VK_NULL_HANDLE; }

		void RecordTransition(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, bool discardContents = false);

		VkImage GetHandle() const { return m_Image; }
		VkImageView GetView() const { return m_View; }
		VkFormat GetFormat() const { return m_Specification.Format; }
		VkExtent2D GetExtent() const { return VkExtent2D{ m_Specification.Width, m_Specification.Height }; }
		VkImageLayout GetLayout() const { return m_Layout; }
		const VulkanImageSpecification& GetSpecification() const { return m_Specification; }

		static VkImageSubresourceRange GetColorRange();

	private:
		static VkFormatFeatureFlags FormatFeaturesForUsage(VkImageUsageFlags usage);

	private:
		const VulkanDevice* m_Device = nullptr;
		VmaAllocator m_Allocator = VK_NULL_HANDLE;

		VkImage m_Image = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		VkImageView m_View = VK_NULL_HANDLE;
		VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;

		VulkanImageSpecification m_Specification{};
	};
}