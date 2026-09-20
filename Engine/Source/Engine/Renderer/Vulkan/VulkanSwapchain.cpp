#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Window/Window.hpp"
#include "Engine/Core/Log.hpp"

#include <algorithm>

namespace Engine
{
	namespace
	{
		constexpr VkFormat k_PreferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
		constexpr VkColorSpaceKHR k_PreferredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

		// The display and UI passes render into the images as colour attachments. The storage usage stays: it is what keeps ChooseSurfaceFormat on a UNORM format, so the sRGB encode the tone map already applied is never applied a second time by the hardware
		constexpr VkImageUsageFlags k_ImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		bool SupportsStorageImage(VkPhysicalDevice physicalDevice, VkFormat format)
		{
			VkFormatProperties l_FormatProperties{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &l_FormatProperties);

			return (l_FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
		}

		// Storage writes rule out sRGB formats, so the shader encodes sRGB itself and the format must be a storage-capable one in the sRGB nonlinear color space
		bool ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, const std::vector<VkSurfaceFormatKHR>& formats, VkSurfaceFormatKHR& surfaceFormat)
		{
			for (const VkSurfaceFormatKHR& l_Format : formats)
			{
				if (l_Format.format == k_PreferredFormat && l_Format.colorSpace == k_PreferredColorSpace && SupportsStorageImage(physicalDevice, l_Format.format))
				{
					surfaceFormat = l_Format;

					return true;
				}
			}

			// Any other storage-capable format in the same color space keeps the shader's sRGB encode correct
			for (const VkSurfaceFormatKHR& l_Format : formats)
			{
				if (l_Format.colorSpace == k_PreferredColorSpace && SupportsStorageImage(physicalDevice, l_Format.format))
				{
					surfaceFormat = l_Format;

					return true;
				}
			}

			return false;
		}

		VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes, bool verticalSync)
		{
			if (verticalSync)
			{
				return VK_PRESENT_MODE_FIFO_KHR;
			}

			for (VkPresentModeKHR l_PresentMode : presentModes)
			{
				if (l_PresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					return l_PresentMode;
				}
			}

			for (VkPresentModeKHR l_PresentMode : presentModes)
			{
				if (l_PresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
				{
					return l_PresentMode;
				}
			}

			return VK_PRESENT_MODE_FIFO_KHR;
		}

		VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
		{
			constexpr VkCompositeAlphaFlagBitsKHR k_Candidates[] =
			{
				VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
				VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
				VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
			};

			for (VkCompositeAlphaFlagBitsKHR l_Candidate : k_Candidates)
			{
				if (supported & l_Candidate)
				{
					return l_Candidate;
				}
			}

			return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		}

		const char* PresentModeToString(VkPresentModeKHR presentMode)
		{
			switch (presentMode)
			{
				case VK_PRESENT_MODE_IMMEDIATE_KHR:
				{
					return "Immediate";
				}
				case VK_PRESENT_MODE_MAILBOX_KHR:
				{
					return "Mailbox";
				}
				case VK_PRESENT_MODE_FIFO_KHR:
				{
					return "FIFO";
				}
				case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
				{
					return "FIFO Relaxed";
				}
				default:
				{
					return "Other";
				}
			}
		}
	}

	VulkanSwapchain::VulkanSwapchain() = default;
	VulkanSwapchain::~VulkanSwapchain() = default;

	void VulkanSwapchain::Initialize(const VulkanDevice& device, const VulkanSurface& surface, const Window& window)
	{
		if (m_Device != nullptr)
		{
			PT_CORE_WARN("Vulkan swapchain is already initialized");

			return;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid logical device is required to create a swapchain");

			return;
		}

		if (!surface.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid surface is required to create a swapchain");

			return;
		}

		if (!window.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid window is required to create a swapchain");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN SWAPCHAIN -------");

		m_Device = &device;
		m_Surface = surface.GetHandle();
		m_Window = &window;

		const SwapchainResult l_Result = Build();
		switch (l_Result.Status)
		{
			case SwapchainStatus::Success:
			{
				PT_CORE_INFO("------- VULKAN SWAPCHAIN INITIALIZED -------");
				break;
			}
			case SwapchainStatus::Deferred:
			{
				// Zero-sized framebuffer at startup, the renderer recreates once the window has a size
				PT_CORE_INFO("------- VULKAN SWAPCHAIN INITIALIZED (CREATION DEFERRED) -------");

				break;
			}
			default:
			{
				PT_CORE_CRITICAL("Swapchain creation failed: {}", VulkanUtilities::ResultToString(l_Result.Error));

				Shutdown();
				break;
			}
		}
	}

	void VulkanSwapchain::Shutdown()
	{
		if (m_Swapchain == VK_NULL_HANDLE && m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SWAPCHAIN -------");

		// Presentation retirement policy: a full device wait before the swapchain and its views are destroyed
		if (m_Device != nullptr && m_Device->IsInitialized())
		{
			m_Device->WaitIdle();
		}

		DestroyImageViews();
		DestroySwapchain();

		m_Images.clear();
		m_ImageFormat = VK_FORMAT_UNDEFINED;
		m_ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		m_Extent = VkExtent2D{};
		m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		m_NeedsRecreate = false;

		m_Surface = VK_NULL_HANDLE;
		m_Device = nullptr;
		m_Window = nullptr;

		PT_CORE_INFO("------- VULKAN SWAPCHAIN SHUTDOWN COMPLETE -------");
	}

	SwapchainResult VulkanSwapchain::Recreate()
	{
		if (m_Device == nullptr || m_Window == nullptr || m_Surface == VK_NULL_HANDLE)
		{
			PT_CORE_ERROR("Cannot recreate a swapchain that was never initialized");

			return SwapchainResult{ SwapchainStatus::Failed, VK_ERROR_INITIALIZATION_FAILED };
		}

		int l_Width = 0;
		int l_Height = 0;
		m_Window->GetFramebufferSize(l_Width, l_Height);

		if (l_Width <= 0 || l_Height <= 0)
		{
			m_NeedsRecreate = true;

			return SwapchainResult{ SwapchainStatus::Deferred };
		}

		PT_CORE_TRACE("Recreating Swapchain");

		const VkResult l_WaitResult = m_Device->WaitIdle();
		if (l_WaitResult != VK_SUCCESS)
		{
			m_NeedsRecreate = true;

			return SwapchainResult{ SwapchainStatus::Failed, l_WaitResult };
		}

		DestroyImageViews();

		const SwapchainResult l_Result = Build();
		if (l_Result.Status == SwapchainStatus::Success)
		{
			PT_CORE_TRACE("Swapchain Recreated");
		}

		return l_Result;
	}

	bool VulkanSwapchain::IsRenderable() const
	{
		if (m_Swapchain == VK_NULL_HANDLE || m_Window == nullptr)
		{
			return false;
		}

		int l_Width = 0;
		int l_Height = 0;
		m_Window->GetFramebufferSize(l_Width, l_Height);

		return l_Width > 0 && l_Height > 0;
	}

	VkImage VulkanSwapchain::GetImage(uint32_t imageIndex) const
	{
		if (imageIndex >= m_Images.size())
		{
			return VK_NULL_HANDLE;
		}

		return m_Images[imageIndex];
	}

	VkImageView VulkanSwapchain::GetImageView(uint32_t imageIndex) const
	{
		if (imageIndex >= m_ImageViews.size())
		{
			return VK_NULL_HANDLE;
		}

		return m_ImageViews[imageIndex];
	}

	void VulkanSwapchain::SetVerticalSync(bool enabled)
	{
		if (m_VerticalSync == enabled)
		{
			return;
		}

		m_VerticalSync = enabled;
		m_NeedsRecreate = true;
	}

	SwapchainResult VulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore, VkFence acquireFence, uint32_t& imageIndex)
	{
		imageIndex = 0;

		if (m_Device == nullptr)
		{
			return SwapchainResult{ SwapchainStatus::Failed, VK_ERROR_INITIALIZATION_FAILED };
		}

		// Recreation is the renderer's job before it acquires, so it can retire outstanding acquires first
		if (m_NeedsRecreate || m_Swapchain == VK_NULL_HANDLE)
		{
			return SwapchainResult{ SwapchainStatus::OutOfDate };
		}

		if (!IsRenderable())
		{
			m_NeedsRecreate = true;

			return SwapchainResult{ SwapchainStatus::Deferred };
		}

		const VkResult l_Result = vkAcquireNextImageKHR(m_Device->GetHandle(), m_Swapchain, UINT64_MAX, imageAvailableSemaphore, acquireFence, &imageIndex);

		switch (l_Result)
		{
			case VK_SUCCESS:
			{
				return SwapchainResult{ SwapchainStatus::Success };
			}
			case VK_SUBOPTIMAL_KHR:
			{
				// The image is acquired and the semaphore/fence will signal, only the next frame needs a replacement
				m_NeedsRecreate = true;

				return SwapchainResult{ SwapchainStatus::Suboptimal };
			}
			case VK_ERROR_OUT_OF_DATE_KHR:
			{
				// Nothing was acquired and neither the semaphore nor the fence is signaled
				m_NeedsRecreate = true;

				return SwapchainResult{ SwapchainStatus::OutOfDate };
			}
			default:
			{
				PT_CORE_ERROR("Failed vkAcquireNextImageKHR: {}", VulkanUtilities::ResultToString(l_Result));

				return SwapchainResult{ SwapchainStatus::Failed, l_Result };
			}
		}
	}

	SwapchainResult VulkanSwapchain::Present(VkQueue queue, VkSemaphore renderFinishedSemaphore, uint32_t imageIndex)
	{
		if (m_Swapchain == VK_NULL_HANDLE)
		{
			return SwapchainResult{ SwapchainStatus::Failed, VK_ERROR_INITIALIZATION_FAILED };
		}

		VkPresentInfoKHR l_PresentInfo
		{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = renderFinishedSemaphore != VK_NULL_HANDLE ? 1u : 0u,
			.pWaitSemaphores = renderFinishedSemaphore != VK_NULL_HANDLE ? &renderFinishedSemaphore : nullptr,
			.swapchainCount = 1,
			.pSwapchains = &m_Swapchain,
			.pImageIndices = &imageIndex,
		};

		const VkResult l_Result = vkQueuePresentKHR(queue, &l_PresentInfo);

		switch (l_Result)
		{
			case VK_SUCCESS:
			{
				return SwapchainResult{ SwapchainStatus::Success };
			}
			case VK_SUBOPTIMAL_KHR:
			{
				m_NeedsRecreate = true;

				return SwapchainResult{ SwapchainStatus::Suboptimal };
			}
			case VK_ERROR_OUT_OF_DATE_KHR:
			{
				// The request was rejected but its semaphore wait is still enqueued, so no synchronization object is left dangling
				m_NeedsRecreate = true;

				return SwapchainResult{ SwapchainStatus::OutOfDate };
			}
			default:
			{
				PT_CORE_ERROR("Failed vkQueuePresentKHR: {}", VulkanUtilities::ResultToString(l_Result));

				return SwapchainResult{ SwapchainStatus::Failed, l_Result };
			}
		}
	}

	SwapchainResult VulkanSwapchain::Build()
	{
		const SwapchainResult l_CreateResult = CreateSwapchain();
		if (l_CreateResult.Status != SwapchainStatus::Success)
		{
			m_NeedsRecreate = true;

			return l_CreateResult;
		}

		const VkResult l_ViewResult = CreateImageViews();
		if (l_ViewResult != VK_SUCCESS)
		{
			DestroyImageViews();
			DestroySwapchain();
			m_Images.clear();
			m_NeedsRecreate = true;

			return SwapchainResult{ SwapchainStatus::Failed, l_ViewResult };
		}

		m_Generation++;
		m_NeedsRecreate = false;

		PT_CORE_TRACE("Swapchain Generation: {}", m_Generation);

		return SwapchainResult{ SwapchainStatus::Success };
	}

	SwapchainResult VulkanSwapchain::CreateSwapchain()
	{
		PT_CORE_TRACE("Creating Swapchain");

		VkPhysicalDevice l_PhysicalDevice = m_Device->GetPhysicalDevice();

		VkSurfaceCapabilitiesKHR l_Capabilities{};
		const VkResult l_CapabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(l_PhysicalDevice, m_Surface, &l_Capabilities);
		if (l_CapabilitiesResult != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {}", VulkanUtilities::ResultToString(l_CapabilitiesResult));

			return SwapchainResult{ SwapchainStatus::Failed, l_CapabilitiesResult };
		}

		if ((l_Capabilities.supportedUsageFlags & k_ImageUsage) != k_ImageUsage)
		{
			PT_CORE_CRITICAL("The surface does not support storage and color attachment swapchain images");

			return SwapchainResult{ SwapchainStatus::Failed, VK_ERROR_FEATURE_NOT_PRESENT };
		}

		VkSurfaceKHR l_Surface = m_Surface;

		std::vector<VkSurfaceFormatKHR> l_Formats;
		const VkResult l_FormatsResult = VulkanUtilities::Enumerate(l_Formats, [l_PhysicalDevice, l_Surface](uint32_t* count, VkSurfaceFormatKHR* data) { return vkGetPhysicalDeviceSurfaceFormatsKHR(l_PhysicalDevice, l_Surface, count, data); });
		if (l_FormatsResult != VK_SUCCESS || l_Formats.empty())
		{
			PT_CORE_CRITICAL("Failed to enumerate surface formats: {}", VulkanUtilities::ResultToString(l_FormatsResult));

			return SwapchainResult{ SwapchainStatus::Failed, l_FormatsResult != VK_SUCCESS ? l_FormatsResult : VK_ERROR_INITIALIZATION_FAILED };
		}

		std::vector<VkPresentModeKHR> l_PresentModes;
		const VkResult l_PresentModesResult = VulkanUtilities::Enumerate(l_PresentModes, [l_PhysicalDevice, l_Surface](uint32_t* count, VkPresentModeKHR* data) { return vkGetPhysicalDeviceSurfacePresentModesKHR(l_PhysicalDevice, l_Surface, count, data); });
		if (l_PresentModesResult != VK_SUCCESS || l_PresentModes.empty())
		{
			PT_CORE_CRITICAL("Failed to enumerate surface present modes: {}", VulkanUtilities::ResultToString(l_PresentModesResult));

			return SwapchainResult{ SwapchainStatus::Failed, l_PresentModesResult != VK_SUCCESS ? l_PresentModesResult : VK_ERROR_INITIALIZATION_FAILED };
		}

		VkSurfaceFormatKHR l_SurfaceFormat{};
		if (!ChooseSurfaceFormat(l_PhysicalDevice, l_Formats, l_SurfaceFormat))
		{
			PT_CORE_CRITICAL("No surface format supports storage image writes in the sRGB nonlinear color space");

			return SwapchainResult{ SwapchainStatus::Failed, VK_ERROR_FORMAT_NOT_SUPPORTED };
		}

		// Remembered before the extent check: the display and UI pipelines are built against the format at initialization, which may happen while creation is deferred, and the same surface yields the same choice on every attempt
		m_ImageFormat = l_SurfaceFormat.format;
		m_ColorSpace = l_SurfaceFormat.colorSpace;

		const VkPresentModeKHR l_PresentMode = ChoosePresentMode(l_PresentModes, m_VerticalSync);
		const VkExtent2D l_Extent = ChooseExtent(l_Capabilities);

		if (l_Extent.width == 0 || l_Extent.height == 0)
		{
			// The previous swapchain, if any, stays alive but unusable until a later attempt replaces it
			PT_CORE_TRACE("Swapchain extent is zero, deferring creation");

			return SwapchainResult{ SwapchainStatus::Deferred };
		}

		uint32_t l_ImageCount = l_Capabilities.minImageCount + 1;
		if (l_Capabilities.maxImageCount > 0 && l_ImageCount > l_Capabilities.maxImageCount)
		{
			l_ImageCount = l_Capabilities.maxImageCount;
		}

		VkSwapchainKHR l_OldSwapchain = m_Swapchain;

		VkSwapchainCreateInfoKHR l_SwapchainCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = m_Surface,
			.minImageCount = l_ImageCount,
			.imageFormat = l_SurfaceFormat.format,
			.imageColorSpace = l_SurfaceFormat.colorSpace,
			.imageExtent = l_Extent,
			.imageArrayLayers = 1,
			.imageUsage = k_ImageUsage,
			.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.preTransform = l_Capabilities.currentTransform,
			.compositeAlpha = ChooseCompositeAlpha(l_Capabilities.supportedCompositeAlpha),
			.presentMode = l_PresentMode,
			.clipped = VK_TRUE,
			.oldSwapchain = l_OldSwapchain,
		};

		VkSwapchainKHR l_Swapchain = VK_NULL_HANDLE;
		const VkResult l_Result = vkCreateSwapchainKHR(m_Device->GetHandle(), &l_SwapchainCreateInfo, nullptr, &l_Swapchain);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkCreateSwapchainKHR: {}", VulkanUtilities::ResultToString(l_Result));

			DestroySwapchain();
			m_Images.clear();

			return SwapchainResult{ SwapchainStatus::Failed, l_Result };
		}

		if (l_OldSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device->GetHandle(), l_OldSwapchain, nullptr);
		}

		m_Swapchain = l_Swapchain;
		m_Extent = l_Extent;
		m_PresentMode = l_PresentMode;

		VkDevice l_Device = m_Device->GetHandle();
		VkSwapchainKHR l_Handle = m_Swapchain;

		m_Images.clear();
		const VkResult l_ImagesResult = VulkanUtilities::Enumerate(m_Images, [l_Device, l_Handle](uint32_t* count, VkImage* data) { return vkGetSwapchainImagesKHR(l_Device, l_Handle, count, data); });
		if (l_ImagesResult != VK_SUCCESS || m_Images.empty())
		{
			PT_CORE_CRITICAL("Failed to retrieve swapchain images: {}", VulkanUtilities::ResultToString(l_ImagesResult));

			DestroySwapchain();
			m_Images.clear();

			return SwapchainResult{ SwapchainStatus::Failed, l_ImagesResult != VK_SUCCESS ? l_ImagesResult : VK_ERROR_INITIALIZATION_FAILED };
		}

		PT_CORE_TRACE("Swapchain Resolution: {}x{}", m_Extent.width, m_Extent.height);
		PT_CORE_TRACE("Swapchain Format: {}", static_cast<int>(m_ImageFormat));
		PT_CORE_TRACE("Swapchain Images: {}", m_Images.size());
		PT_CORE_TRACE("Swapchain Present Mode: {}", PresentModeToString(m_PresentMode));

		PT_CORE_TRACE("Swapchain Created");

		return SwapchainResult{ SwapchainStatus::Success };
	}

	VkResult VulkanSwapchain::CreateImageViews()
	{
		PT_CORE_TRACE("Creating Swapchain Image Views");

		m_ImageViews.resize(m_Images.size(), VK_NULL_HANDLE);

		for (size_t i = 0; i < m_Images.size(); i++)
		{
			VkImageViewCreateInfo l_ImageViewCreateInfo
			{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = m_Images[i],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = m_ImageFormat,
				.subresourceRange =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			const VkResult l_Result = vkCreateImageView(m_Device->GetHandle(), &l_ImageViewCreateInfo, nullptr, &m_ImageViews[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateImageView for swapchain image {}: {}", i, VulkanUtilities::ResultToString(l_Result));

				DestroyImageViews();

				return l_Result;
			}
		}

		PT_CORE_TRACE("Swapchain Image Views Created");

		return VK_SUCCESS;
	}

	void VulkanSwapchain::DestroyImageViews()
	{
		if (m_ImageViews.empty())
		{
			return;
		}

		PT_CORE_TRACE("Destroying Swapchain Image Views");

		for (VkImageView l_ImageView : m_ImageViews)
		{
			if (l_ImageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_Device->GetHandle(), l_ImageView, nullptr);
			}
		}

		m_ImageViews.clear();

		PT_CORE_TRACE("Swapchain Image Views Destroyed");
	}

	void VulkanSwapchain::DestroySwapchain()
	{
		if (m_Swapchain == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_TRACE("Destroying Swapchain");

		vkDestroySwapchainKHR(m_Device->GetHandle(), m_Swapchain, nullptr);
		m_Swapchain = VK_NULL_HANDLE;

		PT_CORE_TRACE("Swapchain Destroyed");
	}

	VkExtent2D VulkanSwapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
	{
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}

		int l_Width = 0;
		int l_Height = 0;
		m_Window->GetFramebufferSize(l_Width, l_Height);

		// A zero-size framebuffer must defer creation, clamping would silently raise it to minImageExtent
		if (l_Width <= 0 || l_Height <= 0)
		{
			return VkExtent2D{ 0, 0 };
		}

		VkExtent2D l_Extent
		{
			.width = static_cast<uint32_t>(l_Width),
			.height = static_cast<uint32_t>(l_Height),
		};

		l_Extent.width = std::clamp(l_Extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		l_Extent.height = std::clamp(l_Extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return l_Extent;
	}
}