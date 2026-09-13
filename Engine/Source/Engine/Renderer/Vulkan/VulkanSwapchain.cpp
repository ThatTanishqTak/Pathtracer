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
		constexpr VkImageUsageFlags k_ImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
		{
			for (const VkSurfaceFormatKHR& l_Format : formats)
			{
				if (l_Format.format == k_PreferredFormat && l_Format.colorSpace == k_PreferredColorSpace)
				{
					return l_Format;
				}
			}

			return formats[0];
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

		CreateSwapchain();
		if (m_Swapchain == VK_NULL_HANDLE)
		{
			if (m_NeedsRecreate)
			{
				PT_CORE_INFO("------- VULKAN SWAPCHAIN INITIALIZED (CREATION DEFERRED) -------");

				return;
			}

			Shutdown();

			return;
		}

		CreateImageViews();
		if (m_ImageViews.empty())
		{
			Shutdown();

			return;
		}

		m_NeedsRecreate = false;

		PT_CORE_INFO("------- VULKAN SWAPCHAIN INITIALIZED -------");
	}

	void VulkanSwapchain::Shutdown()
	{
		if (m_Swapchain == VK_NULL_HANDLE && m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SWAPCHAIN -------");

		if (m_Device != nullptr && m_Device->IsInitialized())
		{
			vkDeviceWaitIdle(m_Device->GetHandle());
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

	void VulkanSwapchain::Recreate()
	{
		if (m_Device == nullptr || m_Window == nullptr || m_Surface == VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Cannot recreate a swapchain that was never initialized");

			return;
		}

		int l_Width = 0;
		int l_Height = 0;
		m_Window->GetFramebufferSize(l_Width, l_Height);

		if (l_Width <= 0 || l_Height <= 0)
		{
			m_NeedsRecreate = true;

			return;
		}

		PT_CORE_TRACE("Recreating Swapchain");

		vkDeviceWaitIdle(m_Device->GetHandle());

		DestroyImageViews();

		CreateSwapchain();
		if (m_Swapchain == VK_NULL_HANDLE)
		{
			m_NeedsRecreate = true;

			return;
		}

		CreateImageViews();
		if (m_ImageViews.empty())
		{
			m_NeedsRecreate = true;

			return;
		}

		m_NeedsRecreate = false;

		PT_CORE_TRACE("Swapchain Recreated");
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

	bool VulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& imageIndex)
	{
		imageIndex = 0;

		if (m_Device == nullptr)
		{
			return false;
		}

		if (m_NeedsRecreate || m_Swapchain == VK_NULL_HANDLE)
		{
			Recreate();

			return false;
		}

		if (!IsRenderable())
		{
			m_NeedsRecreate = true;

			return false;
		}

		const VkResult l_Result = vkAcquireNextImageKHR(m_Device->GetHandle(), m_Swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

		if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			Recreate();

			return false;
		}

		if (l_Result == VK_SUBOPTIMAL_KHR)
		{
			m_NeedsRecreate = true;

			return true;
		}

		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkAcquireNextImageKHR: {}", static_cast<int>(l_Result));

			return false;
		}

		return true;
	}

	bool VulkanSwapchain::Present(VkQueue queue, VkSemaphore renderFinishedSemaphore, uint32_t imageIndex)
	{
		if (m_Swapchain == VK_NULL_HANDLE)
		{
			return false;
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

		if (l_Result == VK_SUBOPTIMAL_KHR)
		{
			m_NeedsRecreate = true;

			return true;
		}

		if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_NeedsRecreate = true;

			return false;
		}

		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkQueuePresentKHR: {}", static_cast<int>(l_Result));

			return false;
		}

		return true;
	}

	void VulkanSwapchain::CreateSwapchain()
	{
		PT_CORE_TRACE("Creating Swapchain");

		VkPhysicalDevice l_PhysicalDevice = m_Device->GetPhysicalDevice();

		VkSurfaceCapabilitiesKHR l_Capabilities{};
		const VkResult l_CapabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(l_PhysicalDevice, m_Surface, &l_Capabilities);
		if (l_CapabilitiesResult != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {}", static_cast<int>(l_CapabilitiesResult));

			return;
		}

		if ((l_Capabilities.supportedUsageFlags & k_ImageUsage) != k_ImageUsage)
		{
			PT_CORE_CRITICAL("The surface does not support the required swapchain image usage");

			return;
		}

		VkSurfaceKHR l_Surface = m_Surface;

		std::vector<VkSurfaceFormatKHR> l_Formats;
		if (VulkanUtilities::Enumerate(l_Formats, [l_PhysicalDevice, l_Surface](uint32_t* count, VkSurfaceFormatKHR* data) { return vkGetPhysicalDeviceSurfaceFormatsKHR(l_PhysicalDevice, l_Surface, count, data); }) != VK_SUCCESS || l_Formats.empty())
		{
			PT_CORE_CRITICAL("Failed to enumerate surface formats");

			return;
		}

		std::vector<VkPresentModeKHR> l_PresentModes;
		if (VulkanUtilities::Enumerate(l_PresentModes, [l_PhysicalDevice, l_Surface](uint32_t* count, VkPresentModeKHR* data) { return vkGetPhysicalDeviceSurfacePresentModesKHR(l_PhysicalDevice, l_Surface, count, data); }) != VK_SUCCESS || l_PresentModes.empty())
		{
			PT_CORE_CRITICAL("Failed to enumerate surface present modes");

			return;
		}

		const VkSurfaceFormatKHR l_SurfaceFormat = ChooseSurfaceFormat(l_Formats);
		const VkPresentModeKHR l_PresentMode = ChoosePresentMode(l_PresentModes, m_VerticalSync);
		const VkExtent2D l_Extent = ChooseExtent(l_Capabilities);

		if (l_Extent.width == 0 || l_Extent.height == 0)
		{
			PT_CORE_TRACE("Swapchain extent is zero, deferring creation");

			m_NeedsRecreate = true;

			return;
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
			PT_CORE_CRITICAL("Failed vkCreateSwapchainKHR: {}", static_cast<int>(l_Result));

			DestroySwapchain();
			m_Images.clear();

			return;
		}

		if (l_OldSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_Device->GetHandle(), l_OldSwapchain, nullptr);
		}

		m_Swapchain = l_Swapchain;
		m_ImageFormat = l_SurfaceFormat.format;
		m_ColorSpace = l_SurfaceFormat.colorSpace;
		m_Extent = l_Extent;
		m_PresentMode = l_PresentMode;

		VkDevice l_Device = m_Device->GetHandle();
		VkSwapchainKHR l_Handle = m_Swapchain;

		m_Images.clear();
		if (VulkanUtilities::Enumerate(m_Images, [l_Device, l_Handle](uint32_t* count, VkImage* data) { return vkGetSwapchainImagesKHR(l_Device, l_Handle, count, data); }) != VK_SUCCESS || m_Images.empty())
		{
			PT_CORE_CRITICAL("Failed to retrieve swapchain images");

			DestroySwapchain();

			return;
		}

		m_Generation++;

		PT_CORE_TRACE("Swapchain Resolution: {}x{}", m_Extent.width, m_Extent.height);
		PT_CORE_TRACE("Swapchain Images: {}", m_Images.size());
		PT_CORE_TRACE("Swapchain Present Mode: {}", PresentModeToString(m_PresentMode));

		PT_CORE_TRACE("Swapchain Created");
	}

	void VulkanSwapchain::CreateImageViews()
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
				PT_CORE_CRITICAL("Failed vkCreateImageView for swapchain image {}: {}", i, static_cast<int>(l_Result));

				DestroyImageViews();

				return;
			}
		}

		PT_CORE_TRACE("Swapchain Image Views Created");
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