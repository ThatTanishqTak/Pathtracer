#include "Engine/Renderer/Vulkan/VulkanRenderView.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	namespace
	{
		// Both images are linear float, the shaders declare rgba32f so the writes never rely on write-without-format
		constexpr VkFormat k_ViewImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		constexpr VkImageUsageFlags k_AccumulationImageUsage = VK_IMAGE_USAGE_STORAGE_BIT;
		constexpr VkImageUsageFlags k_OutputImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	VulkanRenderView::VulkanRenderView() = default;
	VulkanRenderView::~VulkanRenderView() = default;

	VkResult VulkanRenderView::Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator)
	{
		if (m_Device != nullptr)
		{
			PT_CORE_WARN("Vulkan render view is already initialized");

			return VK_SUCCESS;
		}

		if (!device.IsInitialized() || !allocator.IsInitialized())
		{
			PT_CORE_ERROR("A valid device and allocator are required to create a render view");

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN RENDER VIEW -------");

		m_Device = &device;
		m_Allocator = &allocator;
		m_Width = 0;
		m_Height = 0;
		m_AccumulatedSamples = 0;
		m_Key = RenderViewKey{};

		PT_CORE_INFO("------- VULKAN RENDER VIEW INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanRenderView::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN RENDER VIEW -------");

		DestroyImages();

		m_AccumulatedSamples = 0;
		m_Key = RenderViewKey{};
		m_Allocator = nullptr;
		m_Device = nullptr;

		PT_CORE_INFO("------- VULKAN RENDER VIEW SHUTDOWN COMPLETE -------");
	}

	bool VulkanRenderView::NeedsResize(uint32_t width, uint32_t height) const
	{
		return !HasImages() || m_Width != width || m_Height != height;
	}

	VkResult VulkanRenderView::Resize(uint32_t width, uint32_t height)
	{
		if (m_Device == nullptr)
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (width == 0 || height == 0)
		{
			PT_CORE_ERROR("A render view needs a non-zero extent, {}x{} was requested", width, height);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// The caller retired every submission that used the old images, so they can go before the new ones exist
		DestroyImages();

		const VulkanImageSpecification l_AccumulationSpecification
		{
			.Width = width,
			.Height = height,
			.Format = k_ViewImageFormat,
			.Usage = k_AccumulationImageUsage,
			.DebugName = "view accumulation",
		};

		VkResult l_Result = m_AccumulationImage.Initialize(*m_Device, *m_Allocator, l_AccumulationSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyImages();

			return l_Result;
		}

		const VulkanImageSpecification l_OutputSpecification
		{
			.Width = width,
			.Height = height,
			.Format = k_ViewImageFormat,
			.Usage = k_OutputImageUsage,
			.DebugName = "view output",
		};

		l_Result = m_OutputImage.Initialize(*m_Device, *m_Allocator, l_OutputSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyImages();

			return l_Result;
		}

		m_Width = width;
		m_Height = height;

		// Fresh images hold nothing, the next dispatch starts the sum over
		m_AccumulatedSamples = 0;

		PT_CORE_TRACE("Render view resized to {}x{}", width, height);

		return VK_SUCCESS;
	}

	bool VulkanRenderView::Invalidate(const RenderViewKey& key)
	{
		if (key == m_Key)
		{
			return false;
		}

		m_Key = key;
		m_AccumulatedSamples = 0;

		return true;
	}

	void VulkanRenderView::CommitSamples(uint32_t sampleCount)
	{
		m_AccumulatedSamples += sampleCount;
	}

	void VulkanRenderView::DestroyImages()
	{
		m_OutputImage.Shutdown();
		m_AccumulationImage.Shutdown();

		m_Width = 0;
		m_Height = 0;
	}
}