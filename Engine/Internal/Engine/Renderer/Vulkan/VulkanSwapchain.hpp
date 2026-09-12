#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstdint>
#include <vector>

namespace Engine
{
	class Window;
	class VulkanDevice;
	class VulkanSurface;

	class VulkanSwapchain
	{
	public:
		VulkanSwapchain();
		~VulkanSwapchain();

		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
		VulkanSwapchain(VulkanSwapchain&&) = delete;
		VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

		void Initialize(const VulkanDevice& device, const VulkanSurface& surface, const Window& window);
		void Shutdown();
		void Recreate();

		bool IsInitialized() const { return m_Swapchain != VK_NULL_HANDLE; }

		void RequestRecreate() { m_NeedsRecreate = true; }
		bool NeedsRecreate() const { return m_NeedsRecreate; }
		bool IsRenderable() const;

		bool AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& imageIndex);
		bool Present(VkQueue queue, VkSemaphore renderFinishedSemaphore, uint32_t imageIndex);

		void SetVerticalSync(bool enabled);
		bool GetVerticalSync() const { return m_VerticalSync; }

		VkSwapchainKHR GetHandle() const { return m_Swapchain; }
		VkFormat GetImageFormat() const { return m_ImageFormat; }
		VkColorSpaceKHR GetColorSpace() const { return m_ColorSpace; }
		VkExtent2D GetExtent() const { return m_Extent; }
		VkPresentModeKHR GetPresentMode() const { return m_PresentMode; }
		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }
		uint64_t GetGeneration() const { return m_Generation; }
		const std::vector<VkImage>& GetImages() const { return m_Images; }
		const std::vector<VkImageView>& GetImageViews() const { return m_ImageViews; }
		VkImage GetImage(uint32_t imageIndex) const;
		VkImageView GetImageView(uint32_t imageIndex) const;

	private:
		void CreateSwapchain();
		void CreateImageViews();
		void DestroyImageViews();
		void DestroySwapchain();

		VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	private:
		const VulkanDevice* m_Device = nullptr;
		const Window* m_Window = nullptr;

		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;

		VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
		VkColorSpaceKHR m_ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkExtent2D m_Extent{};
		VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;

		std::vector<VkImage> m_Images;
		std::vector<VkImageView> m_ImageViews;

		uint64_t m_Generation = 0;

		bool m_VerticalSync = true;
		bool m_NeedsRecreate = false;
	};
}