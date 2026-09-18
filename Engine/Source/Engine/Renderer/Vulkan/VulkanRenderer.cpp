#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Renderer/Vulkan/VulkanBuffer.hpp"
#include "Engine/Renderer/Vulkan/VulkanImage.hpp"
#include "Engine/Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Engine/Renderer/Vulkan/VulkanComputePipeline.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{
	namespace
	{
		constexpr VkFormat k_GradientImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		constexpr VkImageUsageFlags k_GradientImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		constexpr uint32_t k_GradientWorkgroupSize = 8;
		constexpr const char* k_GradientShaderFile = "Gradient.slang.spv";
		constexpr const char* k_GradientEntryPoint = "computeMain";

		// Must match GradientParameters in Gradient.slang, std140 layout: float4 at 0, uint2 at 16, float at 24, float at 28
		struct GradientParameters
		{
			std::array<float, 4> BaseColor{};
			uint32_t Width = 0;
			uint32_t Height = 0;
			float Phase = 0.0f;
			float Padding = 0.0f;
		};

		static_assert(sizeof(GradientParameters) == 32, "GradientParameters must match the 32 byte std140 block in Gradient.slang");
		static_assert(offsetof(GradientParameters, BaseColor) == 0, "BaseColor must sit at std140 offset 0");
		static_assert(offsetof(GradientParameters, Width) == 16, "Extent.x must sit at std140 offset 16");
		static_assert(offsetof(GradientParameters, Height) == 20, "Extent.y must sit at std140 offset 20");
		static_assert(offsetof(GradientParameters, Phase) == 24, "Phase must sit at std140 offset 24");
		static_assert(offsetof(GradientParameters, Padding) == 28, "Padding must sit at std140 offset 28");

		constexpr uint32_t DivideRoundingUp(uint32_t value, uint32_t divisor)
		{
			return (value + divisor - 1) / divisor;
		}
	}

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

		if (CreateGradientResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the gradient resources");

			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		const bool l_CoreReady = m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanSurface && m_VulkanSurface->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized() && m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized();
		const bool l_FrameReady = m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized() && m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized() && m_VulkanCommandPool && m_VulkanCommandPool->IsInitialized();
		const bool l_GradientReady = m_GradientPipeline && m_GradientPipeline->IsInitialized();

		return l_CoreReady && l_FrameReady && l_GradientReady;
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

			// Recreate waited for the device to go idle, so the old gradient image has no GPU users left and can follow the extent
			const VkResult l_ImageResult = CreateGradientImage();
			if (l_ImageResult != VK_SUCCESS)
			{
				return FailFrame(l_Frame, "recreating the gradient image", l_ImageResult);
			}
		}

		// The image tracks the swapchain extent through the recreate path above, anything else is a logic error worth stopping on
		if (!m_GradientImage || !m_GradientImage->IsInitialized())
		{
			return FailFrame(l_Frame, "validating the gradient image", VK_ERROR_INITIALIZATION_FAILED);
		}

		const VkExtent2D l_SwapchainExtent = m_VulkanSwapchain->GetExtent();
		const VkExtent2D l_GradientExtent = m_GradientImage->GetExtent();
		if (l_SwapchainExtent.width != l_GradientExtent.width || l_SwapchainExtent.height != l_GradientExtent.height)
		{
			PT_CORE_ERROR("Gradient image is {}x{} but the swapchain is {}x{}", l_GradientExtent.width, l_GradientExtent.height, l_SwapchainExtent.width, l_SwapchainExtent.height);

			return FailFrame(l_Frame, "validating the gradient image extent", VK_ERROR_UNKNOWN);
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

		const VkResult l_RecordResult = RecordFrame(l_CommandBuffer, l_Frame.FrameSlot, l_Frame.ImageIndex, request);
		if (l_RecordResult != VK_SUCCESS)
		{
			return FailFrame(l_Frame, "recording the frame", l_RecordResult);
		}

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

		// The slot's frame-local resources are now owned by the GPU until this timeline value is reached
		m_FrameResources[l_Frame.FrameSlot].SubmittedTimelineValue = l_Frame.SubmittedTimelineValue;

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

	VkResult VulkanRenderer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint32_t imageIndex, const RenderRequest& request)
	{
		VkImage l_SwapchainImage = m_VulkanSwapchain->GetImage(imageIndex);
		const VkExtent2D l_Extent = m_GradientImage->GetExtent();
		FrameResources& l_FrameResources = m_FrameResources[frameSlot];

		// 1. Frame-local parameters, WaitForFrame already retired this slot's previous submission so the buffer is free to overwrite
		const GradientParameters l_Parameters
		{
			.BaseColor = request.ClearColor,
			.Width = l_Extent.width,
			.Height = l_Extent.height,
			.Phase = request.GradientPhase,
			.Padding = 0.0f,
		};

		const VkResult l_UploadResult = l_FrameResources.GradientParameters->Upload(&l_Parameters, sizeof(l_Parameters));
		if (l_UploadResult != VK_SUCCESS)
		{
			return l_UploadResult;
		}

		// 2. The previous frame's blit read the image, the compute write must wait for it and the layout must become GENERAL
		// Contents are overwritten in full, so the old ones can be discarded regardless of the tracked layout
		m_GradientImage->RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);

		// 3. Dispatch, the descriptor writes go straight into the command buffer so no descriptor set outlives the recording
		const VkDescriptorImageInfo l_OutputImageInfo
		{
			.imageView = m_GradientImage->GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		const VkDescriptorBufferInfo l_ParametersBufferInfo
		{
			.buffer = l_FrameResources.GradientParameters->GetHandle(),
			.offset = 0,
			.range = sizeof(GradientParameters),
		};

		const std::array<VkWriteDescriptorSet, 2> l_DescriptorWrites
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
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &l_ParametersBufferInfo,
			},
		} };

		m_GradientPipeline->Bind(commandBuffer);
		m_GradientPipeline->PushDescriptors(commandBuffer, l_DescriptorWrites);

		// Rounded up so partial edge workgroups are dispatched, the shader bounds-checks the invocations that fall outside
		vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_GradientWorkgroupSize), DivideRoundingUp(l_Extent.height, k_GradientWorkgroupSize), 1);

		// 4. Compute writes become visible to the blit's transfer read
		m_GradientImage->RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

		// 5. The swapchain image's first access is the blit, the acquire semaphore is waited at the transfer stage so the transition starts there
		const VkImageSubresourceRange l_ColorRange = VulkanImage::GetColorRange();

		VkImageMemoryBarrier2 l_ToTransferBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
			.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_SwapchainImage,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToTransferDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToTransferBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToTransferDependency);

		// 6. Temporary display path: a same-size blit converts the float image into the UNORM swapchain format, Step 4 replaces it with the tone map pass
		const VkImageSubresourceLayers l_ColorLayers
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		const VkOffset3D l_FullExtent{ static_cast<int32_t>(l_Extent.width), static_cast<int32_t>(l_Extent.height), 1 };

		const VkImageBlit2 l_BlitRegion
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
			.srcSubresource = l_ColorLayers,
			.srcOffsets = { VkOffset3D{ 0, 0, 0 }, l_FullExtent },
			.dstSubresource = l_ColorLayers,
			.dstOffsets = { VkOffset3D{ 0, 0, 0 }, l_FullExtent },
		};

		const VkBlitImageInfo2 l_BlitInfo
		{
			.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
			.srcImage = m_GradientImage->GetHandle(),
			.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.dstImage = l_SwapchainImage,
			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.regionCount = 1,
			.pRegions = &l_BlitRegion,
			.filter = VK_FILTER_NEAREST,
		};

		vkCmdBlitImage2(commandBuffer, &l_BlitInfo);

		// 7. Presentation engine reads are made visible through the render finished semaphore, the barrier only changes layout
		VkImageMemoryBarrier2 l_ToPresentBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
			.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

	VkResult VulkanRenderer::CreateGradientResources()
	{
		PT_CORE_INFO("------- CREATING GRADIENT RESOURCES -------");

		// The module only has to outlive pipeline creation
		VulkanShaderModule l_Shader;
		VkResult l_Result = l_Shader.Initialize(*m_VulkanDevice, FileSystem::GetShaderDirectory() / k_GradientShaderFile);
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		const std::array<VkDescriptorSetLayoutBinding, 2> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		} };

		const VulkanComputePipelineSpecification l_PipelineSpecification
		{
			.Shader = &l_Shader,
			.EntryPoint = k_GradientEntryPoint,
			.Bindings = l_Bindings,
			.DebugName = "gradient",
		};

		m_GradientPipeline = std::make_unique<VulkanComputePipeline>();
		l_Result = m_GradientPipeline->Initialize(*m_VulkanDevice, l_PipelineSpecification);
		if (l_Result != VK_SUCCESS)
		{
			DestroyGradientResources();

			return l_Result;
		}

		l_Shader.Shutdown();

		// One parameter buffer per frame slot, WaitForFrame guarantees the slot's previous submission finished before it is rewritten
		for (size_t i = 0; i < m_FrameResources.size(); i++)
		{
			const VulkanBufferSpecification l_BufferSpecification
			{
				.Size = sizeof(GradientParameters),
				.Usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.Memory = BufferMemory::HostUpload,
				.DebugName = "gradient parameters",
			};

			m_FrameResources[i].GradientParameters = std::make_unique<VulkanBuffer>();
			m_FrameResources[i].SubmittedTimelineValue = 0;

			l_Result = m_FrameResources[i].GradientParameters->Initialize(*m_VulkanMemoryAllocator, l_BufferSpecification);
			if (l_Result != VK_SUCCESS)
			{
				DestroyGradientResources();

				return l_Result;
			}
		}

		// A deferred swapchain has no extent yet, the recreate path in Render() creates the image once it does
		const VkExtent2D l_Extent = m_VulkanSwapchain->GetExtent();
		if (l_Extent.width > 0 && l_Extent.height > 0)
		{
			l_Result = CreateGradientImage();
			if (l_Result != VK_SUCCESS)
			{
				DestroyGradientResources();

				return l_Result;
			}
		}

		PT_CORE_INFO("------- GRADIENT RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::CreateGradientImage()
	{
		if (m_GradientImage)
		{
			m_GradientImage->Shutdown();
		}
		else
		{
			m_GradientImage = std::make_unique<VulkanImage>();
		}

		const VkExtent2D l_Extent = m_VulkanSwapchain->GetExtent();

		const VulkanImageSpecification l_ImageSpecification
		{
			.Width = l_Extent.width,
			.Height = l_Extent.height,
			.Format = k_GradientImageFormat,
			.Usage = k_GradientImageUsage,
			.AdditionalFormatFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT,
			.DebugName = "gradient",
		};

		return m_GradientImage->Initialize(*m_VulkanDevice, *m_VulkanMemoryAllocator, l_ImageSpecification);
	}

	void VulkanRenderer::DestroyGradientResources()
	{
		if (m_GradientImage)
		{
			m_GradientImage->Shutdown();
			m_GradientImage.reset();
		}

		for (FrameResources& l_FrameResources : m_FrameResources)
		{
			if (l_FrameResources.GradientParameters)
			{
				l_FrameResources.GradientParameters->Shutdown();
				l_FrameResources.GradientParameters.reset();
			}

			l_FrameResources.SubmittedTimelineValue = 0;
		}

		if (m_GradientPipeline)
		{
			m_GradientPipeline->Shutdown();
			m_GradientPipeline.reset();
		}
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

		DestroyGradientResources();

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