#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanImage.hpp"
#include "Engine/Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Engine/Renderer/Vulkan/VulkanComputePipeline.hpp"
#include "Engine/Renderer/Vulkan/VulkanGraphicsPipeline.hpp"
#include "Engine/Renderer/Vulkan/VulkanUIBackend.hpp"
#include "Engine/Renderer/Vulkan/VulkanRenderView.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Scene/Camera.hpp"
#include "Engine/Scene/Scene.hpp"

#include <volk.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{
	namespace
	{
		constexpr uint32_t k_DiagnosticWorkgroupSize = 8;
		constexpr const char* k_DiagnosticShaderFile = "Diagnostic.slang.spv";
		constexpr const char* k_DiagnosticEntryPoint = "computeMain";

		constexpr uint32_t k_PathtraceWorkgroupSize = 8;
		constexpr const char* k_PathtraceShaderFile = "Pathtrace.slang.spv";
		constexpr const char* k_PathtraceEntryPoint = "computeMain";

		constexpr uint32_t k_ToneMapWorkgroupSize = 8;
		constexpr const char* k_ToneMapShaderFile = "ToneMap.slang.spv";
		constexpr const char* k_ToneMapEntryPoint = "computeMain";

		constexpr const char* k_DisplayShaderFile = "Display.slang.spv";
		constexpr const char* k_DisplayVertexEntryPoint = "vertexMain";
		constexpr const char* k_DisplayFragmentEntryPoint = "fragmentMain";

		// What the UI draws over when no view fills the window, already display-encoded like everything in the swapchain
		constexpr std::array<float, 4> k_UIClearColor{ 0.06f, 0.06f, 0.07f, 1.0f };

		constexpr VkBufferUsageFlags k_SceneBufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		constexpr VkBufferUsageFlags k_ConstantBufferUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		constexpr uint64_t k_MaxAccumulatedSamples = UINT32_MAX;

		struct CameraFrameBlock
		{
			std::array<float, 4> Origin{};
			std::array<float, 4> Forward{};
			std::array<float, 4> RightScaled{};
			std::array<float, 4> UpScaled{};
		};

		static_assert(sizeof(CameraFrameBlock) == 64, "CameraFrameBlock must match the 64 byte block in Modules/Camera.slang");
		static_assert(offsetof(CameraFrameBlock, Origin) == 0, "Origin must sit at offset 0");
		static_assert(offsetof(CameraFrameBlock, Forward) == 16, "Forward must sit at offset 16");
		static_assert(offsetof(CameraFrameBlock, RightScaled) == 32, "RightScaled must sit at offset 32");
		static_assert(offsetof(CameraFrameBlock, UpScaled) == 48, "UpScaled must sit at offset 48");

		// Must match DiagnosticParameters in Diagnostic.slang, std430 push constant layout: CameraFrameBlock at 0, uint2 at 64, then five uints from 72, padded to the block's 16 byte alignment
		struct DiagnosticParameters
		{
			CameraFrameBlock Camera{};
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Mode = 0;
			uint32_t PrimitiveCount = 0;
			uint32_t MaterialCount = 0;
			uint32_t Padding0 = 0;
			uint32_t Padding1 = 0;
			uint32_t Padding2 = 0;
		};

		static_assert(sizeof(DiagnosticParameters) == 96, "DiagnosticParameters must match the 96 byte push constant block in Diagnostic.slang");
		static_assert(offsetof(DiagnosticParameters, Camera) == 0, "Camera must sit at std430 offset 0");
		static_assert(offsetof(DiagnosticParameters, Width) == 64, "Extent.x must sit at std430 offset 64");
		static_assert(offsetof(DiagnosticParameters, Height) == 68, "Extent.y must sit at std430 offset 68");
		static_assert(offsetof(DiagnosticParameters, Mode) == 72, "Mode must sit at std430 offset 72");
		static_assert(offsetof(DiagnosticParameters, PrimitiveCount) == 76, "PrimitiveCount must sit at std430 offset 76");
		static_assert(offsetof(DiagnosticParameters, MaterialCount) == 80, "MaterialCount must sit at std430 offset 80");
		static_assert(offsetof(DiagnosticParameters, Padding0) == 84, "Padding0 must sit at std430 offset 84");

		// Must match PathtraceParameters in Pathtrace.slang, std140 uniform layout: CameraFrameBlock at 0, uint2 at 64, uints at 72 and 76, float4 at 80, uints at 96, 100, 104 and 108
		struct PathtraceParameters
		{
			CameraFrameBlock Camera{};
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t PrimitiveCount = 0;
			uint32_t MaterialCount = 0;
			std::array<float, 4> EnvironmentRadiance{};
			uint32_t SampleIndex = 0;
			uint32_t SamplesPerFrame = 1;
			uint32_t MaxBounces = 0;
			uint32_t Seed = 0;
		};

		static_assert(sizeof(PathtraceParameters) == 112, "PathtraceParameters must match the 112 byte uniform block in Pathtrace.slang");
		static_assert(offsetof(PathtraceParameters, Camera) == 0, "Camera must sit at std140 offset 0");
		static_assert(offsetof(PathtraceParameters, Width) == 64, "Extent.x must sit at std140 offset 64");
		static_assert(offsetof(PathtraceParameters, Height) == 68, "Extent.y must sit at std140 offset 68");
		static_assert(offsetof(PathtraceParameters, PrimitiveCount) == 72, "PrimitiveCount must sit at std140 offset 72");
		static_assert(offsetof(PathtraceParameters, MaterialCount) == 76, "MaterialCount must sit at std140 offset 76");
		static_assert(offsetof(PathtraceParameters, EnvironmentRadiance) == 80, "EnvironmentRadiance must sit at std140 offset 80");
		static_assert(offsetof(PathtraceParameters, SampleIndex) == 96, "SampleIndex must sit at std140 offset 96");
		static_assert(offsetof(PathtraceParameters, SamplesPerFrame) == 100, "SamplesPerFrame must sit at std140 offset 100");
		static_assert(offsetof(PathtraceParameters, MaxBounces) == 104, "MaxBounces must sit at std140 offset 104");
		static_assert(offsetof(PathtraceParameters, Seed) == 108, "Seed must sit at std140 offset 108");

		std::array<float, 4> ToFloat4(const Math::Vector3& value)
		{
			return { value.x, value.y, value.z, 0.0f };
		}

		CameraFrameBlock ToCameraBlock(const CameraRayFrame& frame)
		{
			return CameraFrameBlock
			{
				.Origin = ToFloat4(frame.Origin),
				.Forward = ToFloat4(frame.Forward),
				.RightScaled = ToFloat4(frame.RightScaled),
				.UpScaled = ToFloat4(frame.UpScaled),
			};
		}

		// The view's extent replaces whatever the client left in the camera, so the ray frame's aspect ratio follows the view
		CameraRayFrame GetViewRayFrame(const RenderView& view, VkExtent2D extent)
		{
			Camera l_Camera = view.ActiveCamera;
			l_Camera.ViewWidth = extent.width;
			l_Camera.ViewHeight = extent.height;

			return GetCameraRayFrame(l_Camera);
		}

		// Must match ToneMapParameters in ToneMap.slang, std430 push constant layout: uint2 at 0, float at 8, one float of padding to 16
		struct ToneMapParameters
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			float Exposure = 1.0f;
			float Padding0 = 0.0f;
		};

		static_assert(sizeof(ToneMapParameters) == 16, "ToneMapParameters must match the 16 byte push constant block in ToneMap.slang");
		static_assert(offsetof(ToneMapParameters, Width) == 0, "Extent.x must sit at std430 offset 0");
		static_assert(offsetof(ToneMapParameters, Height) == 4, "Extent.y must sit at std430 offset 4");
		static_assert(offsetof(ToneMapParameters, Exposure) == 8, "Exposure must sit at std430 offset 8");
		static_assert(offsetof(ToneMapParameters, Padding0) == 12, "Padding0 must sit at std430 offset 12");

		constexpr uint32_t DivideRoundingUp(uint32_t value, uint32_t divisor)
		{
			return (value + divisor - 1) / divisor;
		}
	}

	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() = default;

	void VulkanRenderer::Initialize(const Window& window, bool enableUI)
	{
		if (IsInitialized())
		{
			PT_CORE_WARN("Vulkan renderer is already initialized");

			return;
		}

		if (m_VolkInitialized)
		{
			Shutdown();
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN RENDERER -------");

		m_UIEnabled = enableUI;

		InitializeVolk();
		if (!m_VolkInitialized)
		{
			return;
		}

		m_VulkanInstance = std::make_unique<VulkanInstance>();
		m_VulkanInstance->Initialize(window);
		if (!m_VulkanInstance->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanSurface = std::make_unique<VulkanSurface>();
		m_VulkanSurface->Initialize(*m_VulkanInstance, window);
		if (!m_VulkanSurface->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanDevice = std::make_unique<VulkanDevice>();
		m_VulkanDevice->Initialize(*m_VulkanInstance, *m_VulkanSurface);
		if (!m_VulkanDevice->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanMemoryAllocator = std::make_unique<VulkanMemoryAllocator>();
		m_VulkanMemoryAllocator->Initialize(*m_VulkanInstance, *m_VulkanDevice);
		if (!m_VulkanMemoryAllocator->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanSwapchain = std::make_unique<VulkanSwapchain>();
		m_VulkanSwapchain->Initialize(*m_VulkanDevice, *m_VulkanSurface, window);
		if (!m_VulkanSwapchain->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanSynchronization = std::make_unique<VulkanSynchronization>();
		m_VulkanSynchronization->Initialize(*m_VulkanDevice, *m_VulkanSwapchain);
		if (!m_VulkanSynchronization->IsInitialized())
		{
			Shutdown();

			return;
		}

		m_VulkanCommandPool = std::make_unique<VulkanCommandPool>();
		m_VulkanCommandPool->Initialize(*m_VulkanDevice, static_cast<uint32_t>(VulkanSynchronization::k_MaxFramesInFlight) * k_CommandBuffersPerFrame);
		if (!m_VulkanCommandPool->IsInitialized())
		{
			Shutdown();

			return;
		}

		if (CreateDiagnosticResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the diagnostic resources");

			Shutdown();

			return;
		}

		if (CreatePathtraceResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the path tracing resources");

			Shutdown();

			return;
		}

		if (CreateToneMapResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the tone map resources");

			Shutdown();

			return;
		}

		if (CreateDisplayResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the display resources");

			Shutdown();

			return;
		}

		if (m_UIEnabled && CreateUIBackend() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the UI backend");

			Shutdown();

			return;
		}

		if (CreateRenderView() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the render view");

			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		const bool l_CoreReady = m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanSurface && m_VulkanSurface->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized() && m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized();
		const bool l_FrameReady = m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized() && m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized() && m_VulkanCommandPool && m_VulkanCommandPool->IsInitialized();
		const bool l_PipelinesReady = m_DiagnosticPipeline && m_DiagnosticPipeline->IsInitialized() && m_PathtracePipeline && m_PathtracePipeline->IsInitialized() && m_ToneMapPipeline && m_ToneMapPipeline->IsInitialized() && m_DisplayPipeline && m_DisplayPipeline->IsInitialized() && m_DisplaySampler != VK_NULL_HANDLE;
		const bool l_UIReady = !m_UIEnabled || (m_UIBackend && m_UIBackend->IsInitialized());
		const bool l_ViewReady = m_RenderView && m_RenderView->IsInitialized();

		return l_CoreReady && l_FrameReady && l_PipelinesReady && l_UIReady && l_ViewReady;
	}

	RenderOutcome VulkanRenderer::Render(const RenderRequest& request)
	{
		if (m_Fatal || !IsInitialized())
		{
			return RenderOutcome::Fatal;
		}

		FrameRecord l_Frame{};

		// 1. Replace the swapchain before acquiring from it, so every acquire that still references the old one is retired first. The view's images follow the request, not the swapchain, so nothing else is recreated here
		if (m_VulkanSwapchain->NeedsRecreate())
		{
			const VkResult l_AcquireWaitResult = m_VulkanSynchronization->WaitForPendingAcquires();
			if (l_AcquireWaitResult != VK_SUCCESS)
			{
				return FailFrame(l_Frame, "retiring outstanding acquires", l_AcquireWaitResult);
			}

			const SwapchainResult l_RecreateResult = m_VulkanSwapchain->Recreate();
			switch (l_RecreateResult.Status)
			{
				case SwapchainStatus::Success:
				{
					// The display and UI pipelines were built against the format the surface offered at initialization, a surface that changes its mind is not recovered from here
					if (m_VulkanSwapchain->GetImageFormat() != m_DisplayPipeline->GetColorFormat())
					{
						PT_CORE_CRITICAL("The swapchain format changed from {} to {} on recreate, the display pipelines no longer match it", static_cast<int>(m_DisplayPipeline->GetColorFormat()), static_cast<int>(m_VulkanSwapchain->GetImageFormat()));

						return FailFrame(l_Frame, "recreating the swapchain", VK_ERROR_FORMAT_NOT_SUPPORTED);
					}
					break;
				}
				case SwapchainStatus::Deferred:
				{
					// Zero-sized framebuffer, nothing can be presented until the window has a size again
					return RenderOutcome::Skipped;
				}
				default:
				{
					// Retrying a failed creation every frame is the sleep-and-retry loop this policy exists to prevent
					return FailFrame(l_Frame, "recreating the swapchain", l_RecreateResult.Error);
				}
			}
		}

		// 2. Rehook semaphores after a generation change, then wait for the slot's previous batches and its acquire
		const VkResult l_WaitResult = m_VulkanSynchronization->WaitForFrame();
		if (l_WaitResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "waiting for the frame slot", l_WaitResult);
		}

		// Captured before Submit advances the submitted frame count
		l_Frame.FrameSlot = m_VulkanSynchronization->GetFrameIndex();
		l_Frame.SwapchainGeneration = m_VulkanSwapchain->GetGeneration();

		// 3. Extract and upload the scene into this slot's buffers. The slot is retired, and nothing is acquired yet, so a failure here leaves no image outstanding
		const VkResult l_SceneResult = PrepareSceneResources(request, l_Frame.FrameSlot);
		if (l_SceneResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "uploading the scene", l_SceneResult);
		}

		// 4. Size the view, apply the invalidation key and write the slot's integrator constants
		uint32_t l_SampleCount = 0;
		const VkResult l_ViewResult = PrepareRenderView(request, l_Frame.FrameSlot, l_SampleCount);
		if (l_ViewResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "preparing the render view", l_ViewResult);
		}

		// 5. Record and submit the view batch. No swapchain image is involved, so the GPU starts on it while the acquire below may still block
		const uint32_t l_ViewCommandBufferIndex = GetViewCommandBufferIndex(l_Frame.FrameSlot);
		VkCommandBuffer l_ViewCommandBuffer = m_VulkanCommandPool->GetCommandBuffer(l_ViewCommandBufferIndex);

		const VkResult l_ViewBeginResult = m_VulkanCommandPool->Begin(l_ViewCommandBufferIndex);
		if (l_ViewBeginResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "beginning the view command buffer", l_ViewBeginResult);
		}

		const VkResult l_ViewRecordResult = RecordViewFrame(l_ViewCommandBuffer, l_Frame.FrameSlot, request);
		if (l_ViewRecordResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "recording the view batch", l_ViewRecordResult);
		}

		const VkResult l_ViewEndResult = m_VulkanCommandPool->End(l_ViewCommandBufferIndex);
		if (l_ViewEndResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "ending the view command buffer", l_ViewEndResult);
		}

		const VkResult l_ViewSubmitResult = m_VulkanSynchronization->SubmitCompute(m_VulkanDevice->GetGraphicsQueue(), l_ViewCommandBuffer, l_Frame.ViewTimelineValue);
		if (l_ViewSubmitResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "submitting the view batch", l_ViewSubmitResult);
		}

		// The samples count from here: the queue accepted the batch, and nothing that fails later in this frame can take it back
		m_RenderView->CommitSamples(l_SampleCount);
		m_FrameResources[l_Frame.FrameSlot].SubmittedTimelineValue = l_Frame.ViewTimelineValue;

		// 6. Acquire
		const SwapchainResult l_AcquireResult = m_VulkanSwapchain->AcquireNextImage(m_VulkanSynchronization->GetImageAvailableSemaphore(), m_VulkanSynchronization->GetAcquireFence(), l_Frame.ImageIndex);
		switch (l_AcquireResult.Status)
		{
			case SwapchainStatus::Success:
			case SwapchainStatus::Suboptimal:
			{
				break;
			}
			case SwapchainStatus::Deferred:
			case SwapchainStatus::OutOfDate:
			{
				// Nothing was acquired, the next Render() recreates and retries. The view batch already submitted keeps its samples, the next frame reads them from the accumulation image
				return RenderOutcome::Skipped;
			}
			default:
			{
				return FailFrame(l_Frame, "acquiring a swapchain image", l_AcquireResult.Error);
			}
		}

		l_Frame.AcquireStage = FrameRecord::Stage::Acquired;
		m_VulkanSynchronization->MarkAcquirePending();

		// 7. Record the present batch: the display pass or the UI pass into the swapchain image, both sample the display texture the view batch tone mapped
		const uint32_t l_PresentCommandBufferIndex = GetPresentCommandBufferIndex(l_Frame.FrameSlot);
		VkCommandBuffer l_PresentCommandBuffer = m_VulkanCommandPool->GetCommandBuffer(l_PresentCommandBufferIndex);

		const VkResult l_BeginResult = m_VulkanCommandPool->Begin(l_PresentCommandBufferIndex);
		if (l_BeginResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "beginning the present command buffer", l_BeginResult);
		}

		const VkResult l_RecordResult = RecordPresentFrame(l_PresentCommandBuffer, l_Frame.ImageIndex, request);
		if (l_RecordResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "recording the present batch", l_RecordResult);
		}

		const VkResult l_EndResult = m_VulkanCommandPool->End(l_PresentCommandBufferIndex);
		if (l_EndResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "ending the present command buffer", l_EndResult);
		}

		// 8. Submit, the frame only counts as in flight once the queue accepted it. Only this batch waits for the acquire
		const VkResult l_SubmitResult = m_VulkanSynchronization->Submit(m_VulkanDevice->GetGraphicsQueue(), l_PresentCommandBuffer, l_Frame.ImageIndex, l_Frame.SubmittedTimelineValue);
		if (l_SubmitResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "submitting the present batch", l_SubmitResult);
		}

		l_Frame.AcquireStage = FrameRecord::Stage::Submitted;

		// The slot's frame-local resources are now owned by the GPU until this timeline value is reached
		m_FrameResources[l_Frame.FrameSlot].SubmittedTimelineValue = l_Frame.SubmittedTimelineValue;

		// 9. Present
		const SwapchainResult l_PresentResult = m_VulkanSwapchain->Present(m_VulkanDevice->GetGraphicsQueue(), m_VulkanSynchronization->GetRenderFinishedSemaphore(l_Frame.ImageIndex), l_Frame.ImageIndex);
		switch (l_PresentResult.Status)
		{
			case SwapchainStatus::Success:
			case SwapchainStatus::Suboptimal:
			{
				l_Frame.AcquireStage = FrameRecord::Stage::Presented;

				return RenderOutcome::Presented;
			}
			case SwapchainStatus::OutOfDate:
			{
				return RenderOutcome::Skipped;
			}
			default:
			{
				return FailFrame(l_Frame, "presenting the swapchain image", l_PresentResult.Error);
			}
		}
	}

	RenderOutcome VulkanRenderer::FailFrame(const FrameRecord& frame, const char* stage, VkResult result)
	{
		const char* l_AcquireStage = "none";
		switch (frame.AcquireStage)
		{
			case FrameRecord::Stage::Acquired:
			{
				l_AcquireStage = "acquired, not submitted";
				break;
			}
			case FrameRecord::Stage::Submitted:
			{
				l_AcquireStage = "submitted, not presented";
				break;
			}
			case FrameRecord::Stage::Presented:
			{
				l_AcquireStage = "presented";
				break;
			}
			default:
			{
				break;
			}
		}

		PT_CORE_CRITICAL("Frame failed while {}: {} ({}), slot {}, image {}, swapchain generation {}, view timeline value {}, present timeline value {}, acquire {}", stage, VulkanUtilities::ResultToString(result), VulkanUtilities::FailureKindToString(VulkanUtilities::ClassifyResult(result)), frame.FrameSlot, frame.ImageIndex, frame.SwapchainGeneration, frame.ViewTimelineValue, frame.SubmittedTimelineValue, l_AcquireStage);

		m_Fatal = true;

		return RenderOutcome::Fatal;
	}

	VkResult VulkanRenderer::PrepareRenderView(const RenderRequest& request, uint32_t frameSlot, uint32_t& sampleCount)
	{
		sampleCount = 0;

		const RenderView& l_View = request.View;

		// A zero extent renders one pixel rather than failing, the same rule Camera.cpp applies to a zero camera extent
		const uint32_t l_Width = std::max(l_View.Width, 1u);
		const uint32_t l_Height = std::max(l_View.Height, 1u);

		// 1. The images follow the requested extent, never the window. Every batch that touched the old images is retired first, the Step 15 retirement queue replaces this wait
		if (m_RenderView->NeedsResize(l_Width, l_Height))
		{
			const VkResult l_WaitResult = m_VulkanSynchronization->WaitForAllFrames();
			if (l_WaitResult != VK_SUCCESS)
			{
				return l_WaitResult;
			}

			const VkResult l_ResizeResult = m_RenderView->Resize(l_Width, l_Height);
			if (l_ResizeResult != VK_SUCCESS)
			{
				return l_ResizeResult;
			}
		}

		// 2. The invalidation key: camera pose and field of view, extent, scene content, integrator settings and the mode. Exposure is display only and stays out
		const RenderViewKey l_Key
		{
			.CameraPosition = l_View.ActiveCamera.Position,
			.CameraOrientation = l_View.ActiveCamera.Orientation,
			.VerticalFieldOfView = l_View.ActiveCamera.VerticalFieldOfView,
			.Width = l_Width,
			.Height = l_Height,
			.SceneRevision = m_RenderScene.Revision,
			.MaxBounces = l_View.Settings.MaxBounces,
			.Seed = l_View.Settings.Seed,
			.Mode = static_cast<uint32_t>(l_View.Mode),
		};

		m_RenderView->Invalidate(l_Key);

		if (l_View.Mode != DiagnosticMode::PathTraced)
		{
			return VK_SUCCESS;
		}

		// 3. The slot's constants, written now because WaitForFrame retired every batch that read them
		const uint64_t l_Accumulated = m_RenderView->GetAccumulatedSamples();

		uint32_t l_SamplesPerFrame = std::max(l_View.Settings.SamplesPerFrame, 1u);
		if (l_Accumulated + l_SamplesPerFrame > k_MaxAccumulatedSamples)
		{
			// Saturated: a dispatch with no new samples rewrites the same mean, and nothing is committed
			l_SamplesPerFrame = 0;
		}

		const PathtraceParameters l_Parameters
		{
			.Camera = ToCameraBlock(GetViewRayFrame(l_View, m_RenderView->GetExtent())),
			.Width = l_Width,
			.Height = l_Height,
			.PrimitiveCount = static_cast<uint32_t>(m_RenderScene.Primitives.size()),
			.MaterialCount = static_cast<uint32_t>(m_RenderScene.Materials.size()),
			.EnvironmentRadiance = ToFloat4(m_RenderScene.EnvironmentRadiance),
			.SampleIndex = static_cast<uint32_t>(l_Accumulated),
			.SamplesPerFrame = l_SamplesPerFrame,
			.MaxBounces = l_View.Settings.MaxBounces,
			.Seed = l_View.Settings.Seed,
		};

		FrameResources& l_Slot = m_FrameResources[frameSlot];
		if (!l_Slot.PathtraceConstantBuffer.IsInitialized())
		{
			const VulkanBufferSpecification l_Specification
			{
				.Size = sizeof(PathtraceParameters),
				.Usage = k_ConstantBufferUsage,
				.Memory = BufferMemory::HostUpload,
				.DebugName = "pathtrace constants",
			};

			const VkResult l_Result = l_Slot.PathtraceConstantBuffer.Initialize(*m_VulkanMemoryAllocator, l_Specification);
			if (l_Result != VK_SUCCESS)
			{
				return l_Result;
			}
		}

		const VkResult l_UploadResult = l_Slot.PathtraceConstantBuffer.Upload(&l_Parameters, sizeof(l_Parameters));
		if (l_UploadResult != VK_SUCCESS)
		{
			return l_UploadResult;
		}

		sampleCount = l_SamplesPerFrame;

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::RecordViewFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot, const RenderRequest& request)
	{
		const VkExtent2D l_Extent = m_RenderView->GetExtent();
		const FrameResources& l_Slot = m_FrameResources[frameSlot];
		VulkanImage& l_Output = m_RenderView->GetOutputImage();

		// 1. The previous view batch's tone map fetched the output image, the compute write waits for it. Contents are overwritten in full, so the old ones are discarded regardless of the tracked layout
		l_Output.RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);

		// The descriptor writes go straight into the command buffer so no descriptor set outlives the recording. The slot's buffers were filled before the acquire
		const VkDescriptorImageInfo l_OutputImageInfo
		{
			.imageView = l_Output.GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		const VkDescriptorBufferInfo l_PrimitiveBufferInfo
		{
			.buffer = l_Slot.PrimitiveBuffer.GetHandle(),
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		const VkDescriptorBufferInfo l_MaterialBufferInfo
		{
			.buffer = l_Slot.MaterialBuffer.GetHandle(),
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		const VkDescriptorBufferInfo l_VertexBufferInfo
		{
			.buffer = l_Slot.VertexBuffer.GetHandle(),
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		const VkDescriptorBufferInfo l_TriangleBufferInfo
		{
			.buffer = l_Slot.TriangleBuffer.GetHandle(),
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		if (request.View.Mode == DiagnosticMode::PathTraced)
		{
			VulkanImage& l_Accumulation = m_RenderView->GetAccumulationImage();

			// 2. The accumulation image keeps its contents: the previous view batch wrote the running sum this one reads, so the barrier covers that write and never discards
			l_Accumulation.RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, false);

			const VkDescriptorImageInfo l_AccumulationImageInfo
			{
				.imageView = l_Accumulation.GetView(),
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			};

			const VkDescriptorBufferInfo l_ConstantBufferInfo
			{
				.buffer = l_Slot.PathtraceConstantBuffer.GetHandle(),
				.offset = 0,
				.range = sizeof(PathtraceParameters),
			};

			const std::array<VkWriteDescriptorSet, 7> l_DescriptorWrites
			{ {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &l_AccumulationImageInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &l_OutputImageInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_PrimitiveBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_MaterialBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 4,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &l_ConstantBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 5,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_VertexBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 6,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_TriangleBufferInfo,
				},
			} };

			m_PathtracePipeline->Bind(commandBuffer);
			m_PathtracePipeline->PushDescriptors(commandBuffer, l_DescriptorWrites);

			// Rounded up so partial edge workgroups are dispatched, the shader bounds-checks the invocations that fall outside
			vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_PathtraceWorkgroupSize), DivideRoundingUp(l_Extent.height, k_PathtraceWorkgroupSize), 1);
		}
		else
		{
			// 2. The camera block carries the exact vectors the CPU ray generation uses, the shader evaluates the same expression from them
			const DiagnosticParameters l_Parameters
			{
				.Camera = ToCameraBlock(GetViewRayFrame(request.View, l_Extent)),
				.Width = l_Extent.width,
				.Height = l_Extent.height,
				.Mode = static_cast<uint32_t>(request.View.Mode),
				.PrimitiveCount = static_cast<uint32_t>(m_RenderScene.Primitives.size()),
				.MaterialCount = static_cast<uint32_t>(m_RenderScene.Materials.size()),
				.Padding0 = 0,
				.Padding1 = 0,
				.Padding2 = 0,
			};

			const std::array<VkWriteDescriptorSet, 5> l_DescriptorWrites
			{ {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &l_OutputImageInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_PrimitiveBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 2,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_MaterialBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 3,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_VertexBufferInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 4,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &l_TriangleBufferInfo,
				},
			} };

			m_DiagnosticPipeline->Bind(commandBuffer);
			m_DiagnosticPipeline->PushDescriptors(commandBuffer, l_DescriptorWrites);
			m_DiagnosticPipeline->PushConstants(commandBuffer, &l_Parameters, sizeof(l_Parameters));

			vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_DiagnosticWorkgroupSize), DivideRoundingUp(l_Extent.height, k_DiagnosticWorkgroupSize), 1);
		}

		// 3. The view's writes become visible to the tone map's sampled read below
		l_Output.RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		// 4. Tone map into the display texture at the view extent: exposure and the ACES curve turn the linear output into sRGB display values. The previous present batch sampled the texture in its fragment stage, and it is overwritten in full
		VulkanImage& l_Display = m_RenderView->GetDisplayImage();
		l_Display.RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);

		const VkDescriptorImageInfo l_ToneMapInputInfo
		{
			.imageView = l_Output.GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		const VkDescriptorImageInfo l_ToneMapOutputInfo
		{
			.imageView = l_Display.GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		const std::array<VkWriteDescriptorSet, 2> l_ToneMapDescriptorWrites
		{ {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.pImageInfo = &l_ToneMapInputInfo,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &l_ToneMapOutputInfo,
			},
		} };

		const ToneMapParameters l_ToneMapParameters
		{
			.Width = l_Extent.width,
			.Height = l_Extent.height,
			.Exposure = request.Exposure,
			.Padding0 = 0.0f,
		};

		m_ToneMapPipeline->Bind(commandBuffer);
		m_ToneMapPipeline->PushDescriptors(commandBuffer, l_ToneMapDescriptorWrites);
		m_ToneMapPipeline->PushConstants(commandBuffer, &l_ToneMapParameters, sizeof(l_ToneMapParameters));

		vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_ToneMapWorkgroupSize), DivideRoundingUp(l_Extent.height, k_ToneMapWorkgroupSize), 1);

		// 5. From here the display texture is sampled by fragment shaders: the display pass or the UI pass in the present batch, which follows on the same queue in submission order
		l_Display.RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::RecordPresentFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderRequest& request)
	{
		VkImage l_SwapchainImage = m_VulkanSwapchain->GetImage(imageIndex);
		VkImageView l_SwapchainView = m_VulkanSwapchain->GetImageView(imageIndex);
		const VkExtent2D l_SwapchainExtent = m_VulkanSwapchain->GetExtent();

		if (l_SwapchainImage == VK_NULL_HANDLE || l_SwapchainView == VK_NULL_HANDLE)
		{
			// Closed by construction since Step 1, kept as the cheap guard the Step 4 leftovers asked for
			PT_CORE_ERROR("Swapchain image {} has no image or view to render into", imageIndex);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// 1. The swapchain image's first access is the colour attachment write, the acquire semaphore is waited at that stage so the transition starts there
		const VkImageSubresourceRange l_ColorRange = VulkanImage::GetColorRange();

		VkImageMemoryBarrier2 l_ToAttachmentBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_SwapchainImage,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToAttachmentDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToAttachmentBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToAttachmentDependency);

		// 2. One dynamic rendering pass over the whole image. A view that fills the window leaves nothing of the previous contents, so the load is a don't-care, the UI on its own starts from a cleared image
		const bool l_DrawView = request.DrawViewToWindow;

		const VkRenderingAttachmentInfo l_ColorAttachment
		{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = l_SwapchainView,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = l_DrawView ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = {.color = {.float32 = { k_UIClearColor[0], k_UIClearColor[1], k_UIClearColor[2], k_UIClearColor[3] } } },
		};

		const VkRenderingInfo l_RenderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = {.offset = { 0, 0 }, .extent = l_SwapchainExtent },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &l_ColorAttachment,
		};

		vkCmdBeginRendering(commandBuffer, &l_RenderingInfo);

		if (l_DrawView)
		{
			// 3. The fullscreen triangle samples the display texture the view batch tone mapped, filtered to the swapchain extent
			const VkViewport l_Viewport
			{
				.x = 0.0f,
				.y = 0.0f,
				.width = static_cast<float>(l_SwapchainExtent.width),
				.height = static_cast<float>(l_SwapchainExtent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};

			const VkRect2D l_Scissor
			{
				.offset = { 0, 0 },
				.extent = l_SwapchainExtent,
			};

			vkCmdSetViewport(commandBuffer, 0, 1, &l_Viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &l_Scissor);

			const VkDescriptorImageInfo l_DisplayImageInfo
			{
				.imageView = m_RenderView->GetDisplayImage().GetView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			const VkDescriptorImageInfo l_DisplaySamplerInfo
			{
				.sampler = m_DisplaySampler,
			};

			const std::array<VkWriteDescriptorSet, 2> l_DisplayDescriptorWrites
			{ {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
					.pImageInfo = &l_DisplayImageInfo,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstBinding = 1,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
					.pImageInfo = &l_DisplaySamplerInfo,
				},
			} };

			m_DisplayPipeline->Bind(commandBuffer);
			m_DisplayPipeline->PushDescriptors(commandBuffer, l_DisplayDescriptorWrites);

			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		}

		// 4. The UI draws over the view or over the cleared image. The backend sets its own viewport and scissor and records nothing without draw data
		if (m_UIBackend)
		{
			m_UIBackend->Record(commandBuffer);
		}

		vkCmdEndRendering(commandBuffer);

		// 5. Presentation engine reads are made visible through the render finished semaphore, the barrier only changes layout
		VkImageMemoryBarrier2 l_ToPresentBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_SwapchainImage,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToPresentDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToPresentBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToPresentDependency);

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::CreateDiagnosticResources()
	{
		PT_CORE_INFO("------- CREATING DIAGNOSTIC RESOURCES -------");

		// The module only has to outlive pipeline creation
		VulkanShaderModule l_Shader;
		VkResult l_Result = l_Shader.Initialize(*m_VulkanDevice, FileSystem::GetShaderDirectory() / k_DiagnosticShaderFile);
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		const std::array<VkDescriptorSetLayoutBinding, 5> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		} };

		// The camera block and the record counts are small enough to push per dispatch, the records themselves live in the per-slot scene buffers
		const std::array<VkPushConstantRange, 1> l_PushConstantRanges
		{ {
			{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = static_cast<uint32_t>(sizeof(DiagnosticParameters)),
			},
		} };

		const VulkanComputePipelineSpecification l_PipelineSpecification
		{
			.Shader = &l_Shader,
			.EntryPoint = k_DiagnosticEntryPoint,
			.Bindings = l_Bindings,
			.PushConstantRanges = l_PushConstantRanges,
			.DebugName = "diagnostic",
		};

		m_DiagnosticPipeline = std::make_unique<VulkanComputePipeline>();
		l_Result = m_DiagnosticPipeline->Initialize(*m_VulkanDevice, l_PipelineSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyDiagnosticResources();

			return l_Result;
		}

		l_Shader.Shutdown();

		PT_CORE_INFO("------- DIAGNOSTIC RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	void VulkanRenderer::DestroyDiagnosticResources()
	{
		if (m_DiagnosticPipeline)
		{
			m_DiagnosticPipeline->Shutdown();
			m_DiagnosticPipeline.reset();
		}
	}

	VkResult VulkanRenderer::CreatePathtraceResources()
	{
		PT_CORE_INFO("------- CREATING PATH TRACING RESOURCES -------");

		// The module only has to outlive pipeline creation
		VulkanShaderModule l_Shader;
		VkResult l_Result = l_Shader.Initialize(*m_VulkanDevice, FileSystem::GetShaderDirectory() / k_PathtraceShaderFile);
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		const std::array<VkDescriptorSetLayoutBinding, 7> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 5,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 6,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		} };

		// The constants ride in the per-slot uniform buffer, so this layout has no push constant range
		const VulkanComputePipelineSpecification l_PipelineSpecification
		{
			.Shader = &l_Shader,
			.EntryPoint = k_PathtraceEntryPoint,
			.Bindings = l_Bindings,
			.DebugName = "pathtrace",
		};

		m_PathtracePipeline = std::make_unique<VulkanComputePipeline>();
		l_Result = m_PathtracePipeline->Initialize(*m_VulkanDevice, l_PipelineSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyPathtraceResources();

			return l_Result;
		}

		l_Shader.Shutdown();

		PT_CORE_INFO("------- PATH TRACING RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	void VulkanRenderer::DestroyPathtraceResources()
	{
		if (m_PathtracePipeline)
		{
			m_PathtracePipeline->Shutdown();
			m_PathtracePipeline.reset();
		}
	}

	VkResult VulkanRenderer::CreateToneMapResources()
	{
		PT_CORE_INFO("------- CREATING TONE MAP RESOURCES -------");

		// The module only has to outlive pipeline creation
		VulkanShaderModule l_Shader;
		VkResult l_Result = l_Shader.Initialize(*m_VulkanDevice, FileSystem::GetShaderDirectory() / k_ToneMapShaderFile);
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		const std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		} };

		// The parameters are small enough to push per dispatch, so the pass owns no frame-local buffers
		const std::array<VkPushConstantRange, 1> l_PushConstantRanges
		{ {
			{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = static_cast<uint32_t>(sizeof(ToneMapParameters)),
			},
		} };

		const VulkanComputePipelineSpecification l_PipelineSpecification
		{
			.Shader = &l_Shader,
			.EntryPoint = k_ToneMapEntryPoint,
			.Bindings = l_Bindings,
			.PushConstantRanges = l_PushConstantRanges,
			.DebugName = "tone map",
		};

		m_ToneMapPipeline = std::make_unique<VulkanComputePipeline>();
		l_Result = m_ToneMapPipeline->Initialize(*m_VulkanDevice, l_PipelineSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyToneMapResources();

			return l_Result;
		}

		l_Shader.Shutdown();

		PT_CORE_INFO("------- TONE MAP RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	void VulkanRenderer::DestroyToneMapResources()
	{
		if (m_ToneMapPipeline)
		{
			m_ToneMapPipeline->Shutdown();
			m_ToneMapPipeline.reset();
		}
	}

	VkResult VulkanRenderer::CreateDisplayResources()
	{
		PT_CORE_INFO("------- CREATING DISPLAY RESOURCES -------");

		// The module only has to outlive pipeline creation, both entry points come from it
		VulkanShaderModule l_Shader;
		VkResult l_Result = l_Shader.Initialize(*m_VulkanDevice, FileSystem::GetShaderDirectory() / k_DisplayShaderFile);
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		const std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		} };

		// The swapchain chose its format before any deferral, so the pipeline can be built against it now
		const VulkanGraphicsPipelineSpecification l_PipelineSpecification
		{
			.Shader = &l_Shader,
			.VertexEntryPoint = k_DisplayVertexEntryPoint,
			.FragmentEntryPoint = k_DisplayFragmentEntryPoint,
			.ColorFormat = m_VulkanSwapchain->GetImageFormat(),
			.Bindings = l_Bindings,
			.DebugName = "display",
		};

		m_DisplayPipeline = std::make_unique<VulkanGraphicsPipeline>();
		l_Result = m_DisplayPipeline->Initialize(*m_VulkanDevice, l_PipelineSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyDisplayResources();

			return l_Result;
		}

		l_Shader.Shutdown();

		// Linear filtering with clamped edges: the view may render below the window size, and the swapchain extent is rarely a multiple of it
		const VkSamplerCreateInfo l_SamplerCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.anisotropyEnable = VK_FALSE,
			.compareEnable = VK_FALSE,
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.unnormalizedCoordinates = VK_FALSE,
		};

		l_Result = vkCreateSampler(m_VulkanDevice->GetHandle(), &l_SamplerCreateInfo, nullptr, &m_DisplaySampler);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateSampler for the display pass: {}", VulkanUtilities::ResultToString(l_Result));

			m_DisplaySampler = VK_NULL_HANDLE;
			DestroyDisplayResources();

			return l_Result;
		}

		PT_CORE_INFO("------- DISPLAY RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	void VulkanRenderer::DestroyDisplayResources()
	{
		if (m_DisplaySampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(m_VulkanDevice->GetHandle(), m_DisplaySampler, nullptr);
			m_DisplaySampler = VK_NULL_HANDLE;
		}

		if (m_DisplayPipeline)
		{
			m_DisplayPipeline->Shutdown();
			m_DisplayPipeline.reset();
		}
	}

	VkResult VulkanRenderer::CreateUIBackend()
	{
		m_UIBackend = std::make_unique<VulkanUIBackend>();

		const VkResult l_Result = m_UIBackend->Initialize(*m_VulkanInstance, *m_VulkanDevice, m_VulkanSwapchain->GetImageFormat());
		if (l_Result != VK_SUCCESS)
		{
			DestroyUIBackend();
		}

		return l_Result;
	}

	void VulkanRenderer::DestroyUIBackend()
	{
		if (m_UIBackend)
		{
			m_UIBackend->Shutdown();
			m_UIBackend.reset();
		}
	}

	VkResult VulkanRenderer::CreateRenderView()
	{
		// The images are created by the first Render(), once a request names the extent. The view registers its display texture with the UI backend when there is one
		m_RenderView = std::make_unique<VulkanRenderView>();

		const VkResult l_Result = m_RenderView->Initialize(*m_VulkanDevice, *m_VulkanMemoryAllocator, m_UIBackend.get());
		if (l_Result != VK_SUCCESS)
		{
			DestroyRenderView();
		}

		return l_Result;
	}

	void VulkanRenderer::DestroyRenderView()
	{
		if (m_RenderView)
		{
			m_RenderView->Shutdown();
			m_RenderView.reset();
		}
	}

	VkResult VulkanRenderer::PrepareSceneResources(const RenderRequest& request, uint32_t frameSlot)
	{
		// 1. Re-extract when the client's scene changed. Revisions come from one process-wide counter, so a swapped scene is a new revision too, and a null scene is the empty revision 0
		const uint64_t l_Revision = request.ActiveScene != nullptr ? request.ActiveScene->GetRadianceRevision() : 0;
		if (m_RenderScene.Revision != l_Revision)
		{
			if (request.ActiveScene != nullptr)
			{
				BuildRenderScene(*request.ActiveScene, request.Assets, m_RenderScene);
			}
			else
			{
				m_RenderScene = RenderScene{};
			}
		}

		// 2. Upload into this slot's buffers when they hold an older revision. WaitForFrame retired the slot's previous batches, so replacing or rewriting them cannot race the GPU. The mesh data rides with the revision too: Part B keys it on the assets and builds the BLAS from it once
		FrameResources& l_Slot = m_FrameResources[frameSlot];
		if (l_Slot.UploadedRevision == m_RenderScene.Revision && l_Slot.PrimitiveBuffer.IsInitialized() && l_Slot.MaterialBuffer.IsInitialized() && l_Slot.VertexBuffer.IsInitialized() && l_Slot.TriangleBuffer.IsInitialized())
		{
			return VK_SUCCESS;
		}

		VkResult l_Result = UploadSceneBuffer(l_Slot.PrimitiveBuffer, m_RenderScene.Primitives.data(), m_RenderScene.Primitives.size() * sizeof(RenderPrimitiveRecord), sizeof(RenderPrimitiveRecord), "scene primitives");
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		l_Result = UploadSceneBuffer(l_Slot.MaterialBuffer, m_RenderScene.Materials.data(), m_RenderScene.Materials.size() * sizeof(RenderMaterialRecord), sizeof(RenderMaterialRecord), "scene materials");
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		l_Result = UploadSceneBuffer(l_Slot.VertexBuffer, m_RenderScene.Vertices.data(), m_RenderScene.Vertices.size() * sizeof(RenderVertexRecord), sizeof(RenderVertexRecord), "scene vertices");
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		l_Result = UploadSceneBuffer(l_Slot.TriangleBuffer, m_RenderScene.Triangles.data(), m_RenderScene.Triangles.size() * sizeof(RenderTriangleRecord), sizeof(RenderTriangleRecord), "scene triangles");
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		l_Slot.UploadedRevision = m_RenderScene.Revision;

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::UploadSceneBuffer(VulkanBuffer& buffer, const void* data, VkDeviceSize size, VkDeviceSize minimumSize, const char* debugName)
	{
		// A bound storage buffer needs a non-zero size even for an empty scene, and growth allocates headroom so a run of additions does not recreate the buffer every frame
		const VkDeviceSize l_Required = std::max(size, minimumSize);
		if (!buffer.IsInitialized() || buffer.GetSize() < l_Required)
		{
			buffer.Shutdown();

			const VulkanBufferSpecification l_Specification
			{
				.Size = l_Required * 2,
				.Usage = k_SceneBufferUsage,
				.Memory = BufferMemory::HostUpload,
				.DebugName = debugName,
			};

			const VkResult l_Result = buffer.Initialize(*m_VulkanMemoryAllocator, l_Specification);
			if (l_Result != VK_SUCCESS)
			{
				return l_Result;
			}
		}

		if (size == 0)
		{
			return VK_SUCCESS;
		}

		return buffer.Upload(data, size);
	}

	void VulkanRenderer::DestroySceneResources()
	{
		for (FrameResources& l_FrameResources : m_FrameResources)
		{
			l_FrameResources.PrimitiveBuffer.Shutdown();
			l_FrameResources.MaterialBuffer.Shutdown();
			l_FrameResources.VertexBuffer.Shutdown();
			l_FrameResources.TriangleBuffer.Shutdown();
			l_FrameResources.PathtraceConstantBuffer.Shutdown();
			l_FrameResources.UploadedRevision = 0;
		}

		m_RenderScene = RenderScene{};
	}

	void VulkanRenderer::OnFramebufferResized()
	{
		if (m_VulkanSwapchain)
		{
			m_VulkanSwapchain->RequestRecreate();
		}
	}

	uint64_t VulkanRenderer::GetViewTextureId() const
	{
		if (!m_RenderView)
		{
			return 0;
		}

		return m_RenderView->GetDisplayTextureId();
	}

	void VulkanRenderer::Shutdown()
	{
		if (!m_VolkInitialized && !m_VulkanInstance)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN RENDERER -------");

		if (m_VulkanSynchronization)
		{
			m_VulkanSynchronization->WaitForAllFrames();
		}

		for (FrameResources& l_FrameResources : m_FrameResources)
		{
			l_FrameResources.SubmittedTimelineValue = 0;
		}

		DestroySceneResources();
		DestroyRenderView();
		DestroyUIBackend();
		DestroyDisplayResources();
		DestroyToneMapResources();
		DestroyPathtraceResources();
		DestroyDiagnosticResources();

		if (m_VulkanCommandPool)
		{
			m_VulkanCommandPool->Shutdown();
			m_VulkanCommandPool.reset();
		}

		if (m_VulkanSynchronization)
		{
			m_VulkanSynchronization->Shutdown();
			m_VulkanSynchronization.reset();
		}

		if (m_VulkanSwapchain)
		{
			m_VulkanSwapchain->Shutdown();
			m_VulkanSwapchain.reset();
		}

		if (m_VulkanMemoryAllocator)
		{
			m_VulkanMemoryAllocator->Shutdown();
			m_VulkanMemoryAllocator.reset();
		}

		if (m_VulkanDevice)
		{
			m_VulkanDevice->Shutdown();
			m_VulkanDevice.reset();
		}

		if (m_VulkanSurface)
		{
			m_VulkanSurface->Shutdown();
			m_VulkanSurface.reset();
		}

		if (m_VulkanInstance)
		{
			m_VulkanInstance->Shutdown();
			m_VulkanInstance.reset();
		}

		ShutdownVolk();

		m_UIEnabled = false;
		m_Fatal = false;

		PT_CORE_INFO("------- VULKAN RENDERER SHUTDOWN COMPLETE -------");
	}

	void VulkanRenderer::InitializeVolk()
	{
		if (m_VolkInitialized)
		{
			return;
		}

		PT_CORE_TRACE("Initializing Volk");

		if (volkInitialize() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to initialize volk, no Vulkan loader was found");

			return;
		}

		m_VolkInitialized = true;

		const uint32_t l_LoaderVersion = volkGetInstanceVersion();

		PT_CORE_TRACE("Vulkan loader version: {}.{}.{}", VK_API_VERSION_MAJOR(l_LoaderVersion), VK_API_VERSION_MINOR(l_LoaderVersion), VK_API_VERSION_PATCH(l_LoaderVersion));

		if (l_LoaderVersion < VK_API_VERSION_1_4)
		{
			PT_CORE_CRITICAL("Vulkan 1.4 is required, the installed loader is too old");

			ShutdownVolk();

			return;
		}

		PT_CORE_TRACE("Volk Initialized");
	}

	void VulkanRenderer::ShutdownVolk()
	{
		if (!m_VolkInitialized)
		{
			return;
		}

		PT_CORE_TRACE("Deinitializing Volk");

		volkFinalize();
		m_VolkInitialized = false;

		PT_CORE_TRACE("Volk Deinitialized");
	}
}