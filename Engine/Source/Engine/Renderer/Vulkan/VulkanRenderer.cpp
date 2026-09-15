#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

#include <volk.h>

#include <cstdint>

namespace Engine
{
	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() = default;

	void VulkanRenderer::Initialize(const Window& window)
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
		m_VulkanCommandPool->Initialize(*m_VulkanDevice, static_cast<uint32_t>(VulkanSynchronization::k_MaxFramesInFlight));
		if (!m_VulkanCommandPool->IsInitialized())
		{
			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		const bool l_CoreReady = m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanSurface && m_VulkanSurface->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized() && m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized();
		const bool l_FrameReady = m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized() && m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized() && m_VulkanCommandPool && m_VulkanCommandPool->IsInitialized();

		return l_CoreReady && l_FrameReady;
	}

	RenderOutcome VulkanRenderer::Render(const RenderRequest& request)
	{
		if (m_Fatal || !IsInitialized())
		{
			return RenderOutcome::Fatal;
		}

		FrameRecord l_Frame{};

		// 1. Replace the swapchain before acquiring from it, so every acquire that still references the old one is retired first
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

		// 2. Rehook semaphores after a generation change, then wait for the slot's previous frame and its acquire
		const VkResult l_WaitResult = m_VulkanSynchronization->WaitForFrame();
		if (l_WaitResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "waiting for the frame slot", l_WaitResult);
		}

		// Captured before Submit advances the submitted frame count
		l_Frame.FrameSlot = m_VulkanSynchronization->GetFrameIndex();
		l_Frame.SwapchainGeneration = m_VulkanSwapchain->GetGeneration();

		// 3. Acquire
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
				// Nothing was acquired, the next Render() recreates and retries
				return RenderOutcome::Skipped;
			}
			default:
			{
				return FailFrame(l_Frame, "acquiring a swapchain image", l_AcquireResult.Error);
			}
		}

		l_Frame.AcquireStage = FrameRecord::Stage::Acquired;
		m_VulkanSynchronization->MarkAcquirePending();

		// 4. Record
		VkCommandBuffer l_CommandBuffer = m_VulkanCommandPool->GetCommandBuffer(l_Frame.FrameSlot);

		const VkResult l_BeginResult = m_VulkanCommandPool->Begin(l_Frame.FrameSlot);
		if (l_BeginResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "beginning the command buffer", l_BeginResult);
		}

		RecordFrame(l_CommandBuffer, l_Frame.ImageIndex, request);

		const VkResult l_EndResult = m_VulkanCommandPool->End(l_Frame.FrameSlot);
		if (l_EndResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "ending the command buffer", l_EndResult);
		}

		// 5. Submit, the frame only counts as in flight once the queue accepted it
		const VkResult l_SubmitResult = m_VulkanSynchronization->Submit(m_VulkanDevice->GetGraphicsQueue(), l_CommandBuffer, l_Frame.ImageIndex, l_Frame.SubmittedTimelineValue);
		if (l_SubmitResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "submitting the command buffer", l_SubmitResult);
		}

		l_Frame.AcquireStage = FrameRecord::Stage::Submitted;

		// 6. Present
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

		PT_CORE_CRITICAL("Frame failed while {}: {} ({}), slot {}, image {}, swapchain generation {}, timeline value {}, acquire {}", stage, VulkanUtilities::ResultToString(result), VulkanUtilities::FailureKindToString(VulkanUtilities::ClassifyResult(result)), frame.FrameSlot, frame.ImageIndex, frame.SwapchainGeneration, frame.SubmittedTimelineValue, l_AcquireStage);

		m_Fatal = true;

		return RenderOutcome::Fatal;
	}

	void VulkanRenderer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderRequest& request)
	{
		VkImage l_Image = m_VulkanSwapchain->GetImage(imageIndex);

		// Placeholder until the pathtracer output is blitted into the swapchain, the client picks the color
		const VkClearColorValue l_ClearColor{ .float32 = { request.ClearColor[0], request.ClearColor[1], request.ClearColor[2], request.ClearColor[3] } };

		const VkImageSubresourceRange l_ColorRange
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		// The acquire semaphore is waited at the transfer stage, so the transition must start there to be ordered after it
		VkImageMemoryBarrier2 l_ToTransferBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_Image,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToTransferDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToTransferBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToTransferDependency);

		vkCmdClearColorImage(commandBuffer, l_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &l_ClearColor, 1, &l_ColorRange);

		// Presentation engine reads are made visible through the render finished semaphore, the barrier only changes layout
		VkImageMemoryBarrier2 l_ToPresentBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
			.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_Image,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToPresentDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToPresentBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToPresentDependency);
	}

	void VulkanRenderer::OnFramebufferResized()
	{
		if (m_VulkanSwapchain)
		{
			m_VulkanSwapchain->RequestRecreate();
		}
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