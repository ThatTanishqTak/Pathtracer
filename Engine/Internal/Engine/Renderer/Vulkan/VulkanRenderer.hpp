#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>

namespace Engine
{
	class Window;
	class VulkanInstance;
	class VulkanSurface;
	class VulkanDevice;
	class VulkanMemoryAllocator;
	class VulkanSwapchain;
	class VulkanCommandPool;
	class VulkanBuffer;
	class VulkanImage;
	class VulkanComputePipeline;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		VulkanRenderer(const VulkanRenderer&) = delete;
		VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		VulkanRenderer(VulkanRenderer&&) = delete;
		VulkanRenderer& operator=(VulkanRenderer&&) = delete;

		void Initialize(const Window& window);
		void Shutdown();

		bool IsInitialized() const;
		RenderOutcome Render(const RenderRequest& request);

		void OnFramebufferResized();

	private:
		// Describes one Render() call, the slot and generation are captured before Submit advances the frame count
		struct FrameRecord
		{
			enum class Stage : uint8_t
			{
				None, // No swapchain image is held
				Acquired, // An image and its acquire semaphore/fence are outstanding, nothing has consumed them yet
				Submitted, // GPU work waiting on the acquire was accepted by the queue
				Presented, // The presentation request was enqueued
			};

			uint32_t FrameSlot = 0;
			uint32_t ImageIndex = 0;
			uint64_t SwapchainGeneration = 0;
			uint64_t SubmittedTimelineValue = 0;
			Stage AcquireStage = Stage::None;
		};

		struct FrameResources
		{
			std::unique_ptr<VulkanBuffer> GradientParameters;
			uint64_t SubmittedTimelineValue = 0;
		};

		void InitializeVolk();
		void ShutdownVolk();

		VkResult CreateGradientResources();
		VkResult CreateGradientImage();
		void DestroyGradientResources();

		VkResult CreateToneMapResources();
		void DestroyToneMapResources();

		VkResult RecordFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint32_t imageIndex, const RenderRequest& request);

		RenderOutcome FailFrame(const FrameRecord& frame, const char* stage, VkResult result);

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;
		std::unique_ptr<VulkanSurface> m_VulkanSurface;
		std::unique_ptr<VulkanDevice> m_VulkanDevice;
		std::unique_ptr<VulkanMemoryAllocator> m_VulkanMemoryAllocator;
		std::unique_ptr<VulkanSwapchain> m_VulkanSwapchain;
		std::unique_ptr<VulkanSynchronization> m_VulkanSynchronization;
		std::unique_ptr<VulkanCommandPool> m_VulkanCommandPool;
		std::unique_ptr<VulkanComputePipeline> m_GradientPipeline;
		std::unique_ptr<VulkanImage> m_GradientImage;
		std::unique_ptr<VulkanComputePipeline> m_ToneMapPipeline;
		std::array<FrameResources, VulkanSynchronization::k_MaxFramesInFlight> m_FrameResources;

		bool m_VolkInitialized = false;
		bool m_Fatal = false;
	};
}