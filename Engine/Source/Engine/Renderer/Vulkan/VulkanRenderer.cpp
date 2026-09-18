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
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Scene/Camera.hpp"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine
{
	namespace
	{
		constexpr VkFormat k_HdrImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		constexpr VkImageUsageFlags k_HdrImageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		constexpr uint32_t k_DiagnosticWorkgroupSize = 8;
		constexpr const char* k_DiagnosticShaderFile = "Diagnostic.slang.spv";
		constexpr const char* k_DiagnosticEntryPoint = "computeMain";

		constexpr uint32_t k_ToneMapWorkgroupSize = 8;
		constexpr const char* k_ToneMapShaderFile = "ToneMap.slang.spv";
		constexpr const char* k_ToneMapEntryPoint = "computeMain";

		// Must match CameraFrameBlock in Diagnostic.slang, std430 layout: four float4 at 0, 16, 32 and 48. The w components are unused
		struct CameraFrameBlock
		{
			std::array<float, 4> Origin{};
			std::array<float, 4> Forward{};
			std::array<float, 4> RightScaled{};
			std::array<float, 4> UpScaled{};
		};

		static_assert(sizeof(CameraFrameBlock) == 64, "CameraFrameBlock must match the 64 byte block in Diagnostic.slang");
		static_assert(offsetof(CameraFrameBlock, Origin) == 0, "Origin must sit at std430 offset 0");
		static_assert(offsetof(CameraFrameBlock, Forward) == 16, "Forward must sit at std430 offset 16");
		static_assert(offsetof(CameraFrameBlock, RightScaled) == 32, "RightScaled must sit at std430 offset 32");
		static_assert(offsetof(CameraFrameBlock, UpScaled) == 48, "UpScaled must sit at std430 offset 48");

		// Must match DiagnosticParameters in Diagnostic.slang, std430 push constant layout: CameraFrameBlock at 0, uint2 at 64, uint at 72, uint at 76
		struct DiagnosticParameters
		{
			CameraFrameBlock Camera{};
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Mode = 0;
			uint32_t Padding = 0;
		};

		static_assert(sizeof(DiagnosticParameters) == 80, "DiagnosticParameters must match the 80 byte push constant block in Diagnostic.slang");
		static_assert(offsetof(DiagnosticParameters, Camera) == 0, "Camera must sit at std430 offset 0");
		static_assert(offsetof(DiagnosticParameters, Width) == 64, "Extent.x must sit at std430 offset 64");
		static_assert(offsetof(DiagnosticParameters, Height) == 68, "Extent.y must sit at std430 offset 68");
		static_assert(offsetof(DiagnosticParameters, Mode) == 72, "Mode must sit at std430 offset 72");
		static_assert(offsetof(DiagnosticParameters, Padding) == 76, "Padding must sit at std430 offset 76");

		std::array<float, 4> ToFloat4(const Math::Vector3& value)
		{
			return { value.x, value.y, value.z, 0.0f };
		}

		// Must match ToneMapParameters in ToneMap.slang, std430 push constant layout: uint2 at 0, float at 8, float at 12
		struct ToneMapParameters
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			float Exposure = 1.0f;
			float Padding = 0.0f;
		};

		static_assert(sizeof(ToneMapParameters) == 16, "ToneMapParameters must match the 16 byte push constant block in ToneMap.slang");
		static_assert(offsetof(ToneMapParameters, Width) == 0, "Extent.x must sit at std430 offset 0");
		static_assert(offsetof(ToneMapParameters, Height) == 4, "Extent.y must sit at std430 offset 4");
		static_assert(offsetof(ToneMapParameters, Exposure) == 8, "Exposure must sit at std430 offset 8");
		static_assert(offsetof(ToneMapParameters, Padding) == 12, "Padding must sit at std430 offset 12");

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

		if (CreateDiagnosticResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the diagnostic resources");

			Shutdown();

			return;
		}

		if (CreateToneMapResources() != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed to create the tone map resources");

			Shutdown();

			return;
		}

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		const bool l_CoreReady = m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanSurface && m_VulkanSurface->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized() && m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized();
		const bool l_FrameReady = m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized() && m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized() && m_VulkanCommandPool && m_VulkanCommandPool->IsInitialized();
		const bool l_DiagnosticReady = m_DiagnosticPipeline && m_DiagnosticPipeline->IsInitialized();
		const bool l_ToneMapReady = m_ToneMapPipeline && m_ToneMapPipeline->IsInitialized();

		return l_CoreReady && l_FrameReady && l_DiagnosticReady && l_ToneMapReady;
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

			// Recreate waited for the device to go idle, so the old HDR image has no GPU users left and can follow the extent
			const VkResult l_ImageResult = CreateHdrImage();
			if (l_ImageResult != VK_SUCCESS)
			{
				return FailFrame(l_Frame, "recreating the HDR image", l_ImageResult);
			}
		}

		// The image tracks the swapchain extent through the recreate path above, anything else is a logic error worth stopping on
		if (!m_HdrImage || !m_HdrImage->IsInitialized())
		{
			return FailFrame(l_Frame, "validating the HDR image", VK_ERROR_INITIALIZATION_FAILED);
		}

		const VkExtent2D l_SwapchainExtent = m_VulkanSwapchain->GetExtent();
		const VkExtent2D l_HdrExtent = m_HdrImage->GetExtent();
		if (l_SwapchainExtent.width != l_HdrExtent.width || l_SwapchainExtent.height != l_HdrExtent.height)
		{
			PT_CORE_ERROR("HDR image is {}x{} but the swapchain is {}x{}", l_HdrExtent.width, l_HdrExtent.height, l_SwapchainExtent.width, l_SwapchainExtent.height);

			return FailFrame(l_Frame, "validating the HDR image extent", VK_ERROR_UNKNOWN);
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

		const VkResult l_RecordResult = RecordFrame(l_CommandBuffer, l_Frame.ImageIndex, request);
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

	VkResult VulkanRenderer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, const RenderRequest& request)
	{
		VkImage l_SwapchainImage = m_VulkanSwapchain->GetImage(imageIndex);
		const VkExtent2D l_Extent = m_HdrImage->GetExtent();

		// 1. The previous frame's tone map fetched the image, the compute write must wait for it and the layout must become GENERAL. Contents are overwritten in full, so the old ones can be discarded regardless of the tracked layout
		m_HdrImage->RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true);

		// 2. The camera block carries the exact vectors the CPU ray generation uses, the shader evaluates the same expression from them
		const CameraRayFrame l_CameraFrame = GetCameraRayFrame(request.ActiveCamera);

		const DiagnosticParameters l_Parameters
		{
			.Camera =
			{
				.Origin = ToFloat4(l_CameraFrame.Origin),
				.Forward = ToFloat4(l_CameraFrame.Forward),
				.RightScaled = ToFloat4(l_CameraFrame.RightScaled),
				.UpScaled = ToFloat4(l_CameraFrame.UpScaled),
			},
			.Width = l_Extent.width,
			.Height = l_Extent.height,
			.Mode = static_cast<uint32_t>(request.Mode),
			.Padding = 0,
		};

		// 3. Dispatch, the descriptor write goes straight into the command buffer so no descriptor set outlives the recording
		const VkDescriptorImageInfo l_OutputImageInfo
		{
			.imageView = m_HdrImage->GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		const std::array<VkWriteDescriptorSet, 1> l_DescriptorWrites
		{ {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &l_OutputImageInfo,
			},
		} };

		m_DiagnosticPipeline->Bind(commandBuffer);
		m_DiagnosticPipeline->PushDescriptors(commandBuffer, l_DescriptorWrites);
		m_DiagnosticPipeline->PushConstants(commandBuffer, &l_Parameters, sizeof(l_Parameters));

		// Rounded up so partial edge workgroups are dispatched, the shader bounds-checks the invocations that fall outside
		vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_DiagnosticWorkgroupSize), DivideRoundingUp(l_Extent.height, k_DiagnosticWorkgroupSize), 1);

		// 4. Diagnostic writes become visible to the tone map's sampled read
		m_HdrImage->RecordTransition(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

		// 5. The swapchain image's first access is the tone map's storage write, the acquire semaphore is waited at the compute stage so the transition starts there
		const VkImageSubresourceRange l_ColorRange = VulkanImage::GetColorRange();

		VkImageMemoryBarrier2 l_ToStorageBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_2_NONE,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = l_SwapchainImage,
			.subresourceRange = l_ColorRange,
		};

		VkDependencyInfo l_ToStorageDependency
		{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &l_ToStorageBarrier,
		};

		vkCmdPipelineBarrier2(commandBuffer, &l_ToStorageDependency);

		// 6. Tone map: exposure and the ACES curve turn linear scene light into sRGB display values written straight into the swapchain image
		const VkDescriptorImageInfo l_ToneMapInputInfo
		{
			.imageView = m_HdrImage->GetView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		const VkDescriptorImageInfo l_ToneMapOutputInfo
		{
			.imageView = m_VulkanSwapchain->GetImageView(imageIndex),
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
			.Padding = 0.0f,
		};

		m_ToneMapPipeline->Bind(commandBuffer);
		m_ToneMapPipeline->PushDescriptors(commandBuffer, l_ToneMapDescriptorWrites);
		m_ToneMapPipeline->PushConstants(commandBuffer, &l_ToneMapParameters, sizeof(l_ToneMapParameters));

		vkCmdDispatch(commandBuffer, DivideRoundingUp(l_Extent.width, k_ToneMapWorkgroupSize), DivideRoundingUp(l_Extent.height, k_ToneMapWorkgroupSize), 1);

		// 7. Presentation engine reads are made visible through the render finished semaphore, the barrier only changes layout
		VkImageMemoryBarrier2 l_ToPresentBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			.dstAccessMask = VK_ACCESS_2_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
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

		const std::array<VkDescriptorSetLayoutBinding, 1> l_Bindings
		{ {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		} };

		// The camera block is small enough to push per dispatch, so the pass owns no frame-local buffers
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

		// A deferred swapchain has no extent yet, the recreate path in Render() creates the image once it does
		const VkExtent2D l_Extent = m_VulkanSwapchain->GetExtent();
		if (l_Extent.width > 0 && l_Extent.height > 0)
		{
			l_Result = CreateHdrImage();
			if (l_Result != VK_SUCCESS)
			{
				DestroyDiagnosticResources();

				return l_Result;
			}
		}

		PT_CORE_INFO("------- DIAGNOSTIC RESOURCES CREATED -------");

		return VK_SUCCESS;
	}

	VkResult VulkanRenderer::CreateHdrImage()
	{
		if (m_HdrImage)
		{
			m_HdrImage->Shutdown();
		}
		else
		{
			m_HdrImage = std::make_unique<VulkanImage>();
		}

		const VkExtent2D l_Extent = m_VulkanSwapchain->GetExtent();

		const VulkanImageSpecification l_ImageSpecification
		{
			.Width = l_Extent.width,
			.Height = l_Extent.height,
			.Format = k_HdrImageFormat,
			.Usage = k_HdrImageUsage,
			.DebugName = "hdr",
		};

		return m_HdrImage->Initialize(*m_VulkanDevice, *m_VulkanMemoryAllocator, l_ImageSpecification);
	}

	void VulkanRenderer::DestroyDiagnosticResources()
	{
		if (m_HdrImage)
		{
			m_HdrImage->Shutdown();
			m_HdrImage.reset();
		}

		if (m_DiagnosticPipeline)
		{
			m_DiagnosticPipeline->Shutdown();
			m_DiagnosticPipeline.reset();
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

		for (FrameResources& l_FrameResources : m_FrameResources)
		{
			l_FrameResources.SubmittedTimelineValue = 0;
		}

		DestroyToneMapResources();
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