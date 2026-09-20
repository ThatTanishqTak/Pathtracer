#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstdint>

namespace Engine
{
	class VulkanInstance;
	class VulkanDevice;

	// The Dear ImGui Vulkan backend behind one owner: it draws the UI layer's draw data into the swapchain image inside the renderer's dynamic rendering pass, and hands out the descriptor sets the UI uses as texture ids
	class VulkanUIBackend
	{
	public:
		VulkanUIBackend();
		~VulkanUIBackend();

		VulkanUIBackend(const VulkanUIBackend&) = delete;
		VulkanUIBackend& operator=(const VulkanUIBackend&) = delete;
		VulkanUIBackend(VulkanUIBackend&&) = delete;
		VulkanUIBackend& operator=(VulkanUIBackend&&) = delete;

		// Needs the Dear ImGui context the UI layer created and the swapchain format its pipeline renders into, which the swapchain knows even while its creation is deferred
		VkResult Initialize(const VulkanInstance& instance, const VulkanDevice& device, VkFormat colorFormat);
		void Shutdown(); // The caller retired every frame, the backend waits for the device once more before its objects go

		bool IsInitialized() const { return m_Initialized; }

		// Records the draw data ImGui::Render produced this frame, inside the dynamic rendering the caller began on the swapchain image. Records nothing without draw data
		void Record(VkCommandBuffer commandBuffer) const;

		// A sampled-image descriptor the UI draws through ImGui::Image. The view must stay valid and in the given layout for as long as a draw list can name the id. Zero on failure
		uint64_t RegisterTexture(VkImageView view, VkImageLayout layout) const;
		void UnregisterTexture(uint64_t textureId) const; // Only after every submission that drew the id was retired

		VkFormat GetColorFormat() const { return m_ColorFormat; }

	private:
		const VulkanDevice* m_Device = nullptr;
		VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
		bool m_Initialized = false;
	};
}