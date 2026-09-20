#include "Engine/Renderer/Vulkan/VulkanRenderView.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanUIBackend.hpp"
#include "Engine/Core/Log.hpp"

#include <utility>

namespace Engine
{
	namespace
	{
		// Both view images are linear float, the shaders declare rgba32f so the writes never rely on write-without-format
		constexpr VkFormat k_ViewImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		constexpr VkImageUsageFlags k_AccumulationImageUsage = VK_IMAGE_USAGE_STORAGE_BIT;
		constexpr VkImageUsageFlags k_OutputImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		// The display texture is UNORM and holds sRGB-encoded values, ToneMap.slang declares rgba8 for the storage write and the display pass and the UI sample it
		constexpr VkFormat k_DisplayImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
		constexpr VkImageUsageFlags k_DisplayImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		// The layout the display texture is registered and sampled in, the view batch leaves it there every frame
		constexpr VkImageLayout k_DisplayImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	VulkanRenderView::VulkanRenderView() = default;
	VulkanRenderView::~VulkanRenderView() = default;

	VkResult VulkanRenderView::Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator, const VulkanUIBackend* uiBackend)
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
		m_UIBackend = uiBackend;
		m_DisplayImage = std::make_unique<VulkanImage>();
		m_DisplayTextureId = 0;
		m_RetiredDisplayTextureId = 0;
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

		// The caller waited for every frame, so the retired display texture has no user left either
		DestroyImages();
		ReleaseRetiredDisplayImage();

		m_DisplayImage.reset();
		m_AccumulatedSamples = 0;
		m_Key = RenderViewKey{};
		m_UIBackend = nullptr;
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

		// The caller retired every submission that used the old images, so the accumulation and output go before the new ones exist. A display texture the UI registered is retired instead: BuildUI ran before this frame's Render and its draw list may name the old id, so the old texture stays alive until the next Resize has waited for every frame again
		ReleaseRetiredDisplayImage();
		RetireDisplayImage();
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

		const VulkanImageSpecification l_DisplaySpecification
		{
			.Width = width,
			.Height = height,
			.Format = k_DisplayImageFormat,
			.Usage = k_DisplayImageUsage,
			.DebugName = "view display",
		};

		l_Result = m_DisplayImage->Initialize(*m_Device, *m_Allocator, l_DisplaySpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyImages();

			return l_Result;
		}

		// The UI draws the display texture through this id, registered in the layout the view batch leaves it in
		if (m_UIBackend != nullptr)
		{
			m_DisplayTextureId = m_UIBackend->RegisterTexture(m_DisplayImage->GetView(), k_DisplayImageLayout);
			if (m_DisplayTextureId == 0)
			{
				DestroyImages();

				return VK_ERROR_OUT_OF_POOL_MEMORY;
			}
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
		// The caller waited for every frame, or the images were never used, so the id goes with the image
		if (m_DisplayTextureId != 0 && m_UIBackend != nullptr)
		{
			m_UIBackend->UnregisterTexture(m_DisplayTextureId);
		}

		m_DisplayTextureId = 0;

		if (m_DisplayImage)
		{
			m_DisplayImage->Shutdown();
		}

		m_OutputImage.Shutdown();
		m_AccumulationImage.Shutdown();

		m_Width = 0;
		m_Height = 0;
	}

	void VulkanRenderView::RetireDisplayImage()
	{
		// Without a registered id only GPU batches referenced the texture, and the caller retired those, so it is destroyed with the other images
		if (m_DisplayTextureId == 0)
		{
			return;
		}

		m_RetiredDisplayImage = std::move(m_DisplayImage);
		m_RetiredDisplayTextureId = m_DisplayTextureId;

		m_DisplayImage = std::make_unique<VulkanImage>();
		m_DisplayTextureId = 0;
	}

	void VulkanRenderView::ReleaseRetiredDisplayImage()
	{
		if (m_RetiredDisplayTextureId != 0 && m_UIBackend != nullptr)
		{
			m_UIBackend->UnregisterTexture(m_RetiredDisplayTextureId);
		}

		m_RetiredDisplayTextureId = 0;

		if (m_RetiredDisplayImage)
		{
			m_RetiredDisplayImage->Shutdown();
			m_RetiredDisplayImage.reset();
		}
	}
}