#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/RenderScene.hpp"
#include "Engine/Renderer/Vulkan/VulkanAccelerationStructure.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine
{
	class AssetManager;
	class Window;
	class VulkanInstance;
	class VulkanSurface;
	class VulkanDevice;
	class VulkanMemoryAllocator;
	class VulkanSwapchain;
	class VulkanCommandPool;
	class VulkanComputePipeline;
	class VulkanComputePipeline;
	class VulkanGraphicsPipeline;
	class VulkanUIBackend;
	class VulkanRenderView;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		VulkanRenderer(const VulkanRenderer&) = delete;
		VulkanRenderer& operator=(const VulkanRenderer&) = delete;
		VulkanRenderer(VulkanRenderer&&) = delete;
		VulkanRenderer& operator=(VulkanRenderer&&) = delete;

		void Initialize(const Window& window, bool enableUI);
		void Shutdown();

		bool IsInitialized() const;
		RenderOutcome Render(const RenderRequest& request);

		void OnFramebufferResized();

		uint64_t GetViewTextureId() const;

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
			uint64_t ViewTimelineValue = 0; // The view batch, submitted before the acquire, zero until it was accepted
			uint64_t SubmittedTimelineValue = 0; // The present batch
			Stage AcquireStage = Stage::None;
		};

		// Each slot owns the scene records and the integrator constants its submissions read, rewritten only after WaitForFrame retired the slot's previous batches
		struct FrameResources
		{
			uint64_t SubmittedTimelineValue = 0;

			VulkanBuffer PrimitiveBuffer;
			VulkanBuffer MaterialBuffer;
			VulkanBuffer VertexBuffer; // The meshes the primitives reference, indexed through each mesh record's triangle range, and the BLAS builds' position input
			VulkanBuffer TriangleBuffer;
			VulkanBuffer IndexBuffer; // RenderScene::Indices, read only by the BLAS builds
			uint64_t UploadedRevision = 0; // The RenderScene::Revision the buffers hold

			// The slot's own acceleration structures, replaced only after WaitForFrame retired every batch that built or read them, so no retirement queue exists until Step 15 shares them between slots
			std::vector<std::unique_ptr<VulkanAccelerationStructure>> MeshStructures; // One BLAS per RenderScene::Meshes entry, in the same order
			std::vector<MeshId> MeshStructureIds; // The meshes MeshStructures were built from, a different list rebuilds them all
			const AssetManager* MeshStructureAssets = nullptr; // The manager those Ids came from
			VulkanAccelerationStructure TopLevel; // One instance per primitive record, rebuilt on every scene revision
			uint32_t TopLevelCapacity = 0; // The instance count TopLevel was sized for, it is recreated only when a scene outgrows it
			uint64_t TopLevelRevision = 0; // The RenderScene::Revision TopLevel was built from
			VulkanBuffer InstanceBuffer; // The TLAS build input, one VkAccelerationStructureInstanceKHR per primitive record
			VulkanBuffer ScratchBuffer; // Every build the slot records, the BLAS builds side by side and the TLAS after them from offset 0

			VulkanBuffer PathtraceConstantBuffer; // Uniform block for Pathtrace.slang, rewritten every path-traced frame
		};

		// Two command buffers per slot: the view batch, which traces and tone maps without a swapchain image, and the present batch, which waits for the acquire and draws the display texture or the UI into it
		static constexpr uint32_t k_CommandBuffersPerFrame = 2;
		static uint32_t GetViewCommandBufferIndex(uint32_t frameSlot) { return frameSlot * k_CommandBuffersPerFrame; }
		static uint32_t GetPresentCommandBufferIndex(uint32_t frameSlot) { return frameSlot * k_CommandBuffersPerFrame + 1; }

		void InitializeVolk();
		void ShutdownVolk();

		VkResult CreateDiagnosticResources();
		void DestroyDiagnosticResources();

		VkResult CreatePathtraceResources();
		void DestroyPathtraceResources();

		VkResult CreateToneMapResources();
		void DestroyToneMapResources();

		VkResult CreateDisplayResources();
		void DestroyDisplayResources();

		VkResult CreateUIBackend();
		void DestroyUIBackend();

		VkResult CreateRenderView();
		void DestroyRenderView();

		VkResult PrepareSceneResources(const RenderRequest& request, uint32_t frameSlot);
		VkResult UploadSceneBuffer(VulkanBuffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize minimumSize, VkBufferUsageFlags usage, const char* debugName);
		VkResult RecordAccelerationStructures(VkCommandBuffer commandBuffer, uint32_t frameSlot, const RenderRequest& request);
		void DestroySceneResources();

		VkResult PrepareRenderView(const RenderRequest& request, uint32_t frameSlot, uint32_t& sampleCount);
		VkResult RecordViewFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot, const RenderRequest& request);
		VkResult RecordPresentFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderRequest& request);

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
		std::unique_ptr<VulkanComputePipeline> m_PathtracePipeline;
		std::unique_ptr<VulkanComputePipeline> m_ToneMapPipeline;
		std::unique_ptr<VulkanGraphicsPipeline> m_DisplayPipeline; // Fullscreen triangle from the display texture into the swapchain image
		std::unique_ptr<VulkanUIBackend> m_UIBackend; // Only when the application enabled the UI
		std::unique_ptr<VulkanRenderView> m_RenderView; // The one view the window or the UI viewport shows
		std::array<FrameResources, VulkanSynchronization::k_MaxFramesInFlight> m_FrameResources;

		VkSampler m_DisplaySampler = VK_NULL_HANDLE; // Linear and clamped, for the display pass

		// The unit sphere and the unit quad as one procedural box each, indexed by RenderPrimitiveType. Built once in the first view batch and never changed, every sphere and quad instance references one of them with its own transform
		VulkanBuffer m_UnitShapeBoundsBuffer;
		std::array<VulkanAccelerationStructure, 2> m_UnitShapeStructures;
		bool m_UnitShapeStructuresBuilt = false;
		RenderScene m_RenderScene; // The packed records of the last extracted revision, the client's Scene itself is never kept

		bool m_VolkInitialized = false;
		bool m_UIEnabled = false;
		bool m_Fatal = false;
	};
}