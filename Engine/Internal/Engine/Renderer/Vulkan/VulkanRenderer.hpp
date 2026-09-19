#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/RenderScene.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
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

		// Each slot owns the scene records its submissions read, rewritten only after WaitForFrame retired the slot's previous submission
		struct FrameResources
		{
			uint64_t SubmittedTimelineValue = 0;

			VulkanBuffer PrimitiveBuffer;
			VulkanBuffer MaterialBuffer;
			uint64_t UploadedRevision = 0; // The RenderScene::Revision the buffers hold
		};

		void InitializeVolk();
		void ShutdownVolk();

		VkResult CreateDiagnosticResources();
		VkResult CreateHdrImage();
		void DestroyDiagnosticResources();

		VkResult CreateToneMapResources();
		void DestroyToneMapResources();

		VkResult PrepareSceneResources(const RenderRequest& request, uint32_t frameSlot);
		VkResult UploadSceneBuffer(VulkanBuffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize minimumSize, const char* debugName);
		void DestroySceneResources();

		VkResult RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameSlot, const RenderRequest& request);

		RenderOutcome FailFrame(const FrameRecord& frame, const char* stage, VkResult result);

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;
		std::unique_ptr<VulkanSurface> m_VulkanSurface;
		std::unique_ptr<VulkanDevice> m_VulkanDevice;
		std::unique_ptr<VulkanMemoryAllocator> m_VulkanMemoryAllocator;
		std::unique_ptr<VulkanSwapchain> m_VulkanSwapchain;
		std::unique_ptr<VulkanSynchronization> m_VulkanSynchronization;
		std::unique_ptr<VulkanCommandPool> m_VulkanCommandPool;
		std::unique_ptr<VulkanComputePipeline> m_DiagnosticPipeline;
		std::unique_ptr<VulkanImage> m_HdrImage;
		std::unique_ptr<VulkanComputePipeline> m_ToneMapPipeline;
		std::array<FrameResources, VulkanSynchronization::k_MaxFramesInFlight> m_FrameResources;
		RenderScene m_RenderScene; // The packed records of the last extracted revision, the client's Scene itself is never kept

		bool m_VolkInitialized = false;
		bool m_Fatal = false;
	};
}