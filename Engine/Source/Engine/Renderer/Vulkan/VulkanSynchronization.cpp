#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanSynchronization::VulkanSynchronization() = default;
	VulkanSynchronization::~VulkanSynchronization() = default;

	void VulkanSynchronization::Initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain)
	{
		if (m_TimelineSemaphore != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Vulkan synchronization is already initialized");

			return;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid logical device is required to create synchronization objects");

			return;
		}

		if (!swapchain.IsInitialized())
		{
			PT_CORE_CRITICAL("A valid swapchain is required to create synchronization objects");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN SYNCHRONIZATION -------");

		m_Device = &device;
		m_Swapchain = &swapchain;
		m_TimelineValue = 0;
		m_SubmittedFrameCount = 0;
		m_SwapchainGeneration = swapchain.GetGeneration();
		m_SlotTimelineValues.fill(0);

		if (CreateTimelineSemaphore() != VK_SUCCESS || CreateAcquireFences() != VK_SUCCESS || CreateFrameSemaphores() != VK_SUCCESS)
		{
			Shutdown();

			return;
		}

		if (swapchain.GetImageCount() > 0)
		{
			if (CreateImageSemaphores(swapchain.GetImageCount()) != VK_SUCCESS)
			{
				Shutdown();

				return;
			}
		}

		PT_CORE_TRACE("Frames In Flight: {}", k_MaxFramesInFlight);

		PT_CORE_INFO("------- VULKAN SYNCHRONIZATION INITIALIZED -------");
	}

	void VulkanSynchronization::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SYNCHRONIZATION -------");

		VkResult l_Result = WaitForPendingAcquires();
		if (l_Result == VK_SUCCESS)
		{
			l_Result = m_Device->WaitIdle();
		}

		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Destroying synchronization objects without a completion guarantee: {}", VulkanUtilities::ResultToString(l_Result));
		}

		DestroyImageSemaphores();
		DestroyFrameSemaphores();
		DestroyAcquireFences();
		DestroyTimelineSemaphore();

		m_TimelineValue = 0;
		m_SubmittedFrameCount = 0;
		m_SwapchainGeneration = 0;
		m_SlotTimelineValues.fill(0);
		m_Swapchain = nullptr;
		m_Device = nullptr;

		PT_CORE_INFO("------- VULKAN SYNCHRONIZATION SHUTDOWN COMPLETE -------");
	}

	VkResult VulkanSynchronization::RehookBinarySemaphores()
	{
		if (m_Swapchain == nullptr || !m_Swapchain->IsInitialized())
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		// Rehook on a swapchain generation change, or when an earlier recreate or recovery failed and left the sets empty or mismatched
		const bool l_GenerationChanged = m_Swapchain->GetGeneration() != m_SwapchainGeneration;
		const bool l_SetsIncomplete = m_ImageAvailableSemaphores.empty() || m_RenderFinishedSemaphores.size() != m_Swapchain->GetImageCount();
		if (!l_GenerationChanged && !l_SetsIncomplete)
		{
			return VK_SUCCESS;
		}

		PT_CORE_TRACE("Rehooking Binary Semaphores");

		VkResult l_Result = WaitForPendingAcquires();
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		l_Result = m_Device->WaitIdle();
		if (l_Result != VK_SUCCESS)
		{
			return l_Result;
		}

		DestroyImageSemaphores();
		DestroyFrameSemaphores();

		l_Result = CreateFrameSemaphores();
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed to recreate frame semaphores");

			return l_Result;
		}

		l_Result = CreateImageSemaphores(m_Swapchain->GetImageCount());
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed to recreate image semaphores");

			DestroyFrameSemaphores();

			return l_Result;
		}

		m_SwapchainGeneration = m_Swapchain->GetGeneration();

		PT_CORE_TRACE("Binary Semaphores Rehooked");

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::WaitForFrame()
	{
		if (!IsInitialized())
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		const VkResult l_RehookResult = RehookBinarySemaphores();
		if (l_RehookResult != VK_SUCCESS)
		{
			return l_RehookResult;
		}

		// Every batch the slot submitted, the view batch and the present batch alike, signalled a value at most this one
		const uint32_t l_FrameSlot = GetFrameIndex();
		const uint64_t l_SlotValue = m_SlotTimelineValues[l_FrameSlot];
		if (l_SlotValue != 0)
		{
			const VkResult l_TimelineResult = WaitForTimelineValue(l_SlotValue);
			if (l_TimelineResult != VK_SUCCESS)
			{
				return l_TimelineResult;
			}
		}

		// The slot's acquire semaphore and fence are about to be reused, establish that their previous acquire completed
		return WaitForAcquire(l_FrameSlot);
	}

	VkResult VulkanSynchronization::SubmitCompute(VkQueue queue, VkCommandBuffer commandBuffer, uint64_t& submittedValue)
	{
		submittedValue = 0;

		if (!IsInitialized())
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		const uint64_t l_SignalValue = m_TimelineValue + 1;

		VkSemaphoreSubmitInfo l_SignalSemaphoreInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_TimelineSemaphore,
			.value = l_SignalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		};

		VkCommandBufferSubmitInfo l_CommandBufferSubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = commandBuffer,
		};

		VkSubmitInfo2 l_SubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &l_CommandBufferSubmitInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &l_SignalSemaphoreInfo,
		};

		const VkResult l_Result = vkQueueSubmit2(queue, 1, &l_SubmitInfo, VK_NULL_HANDLE);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkQueueSubmit2 for a compute batch: {}", VulkanUtilities::ResultToString(l_Result));

			return l_Result;
		}

		// The slot now has GPU work outstanding even though no frame was presented, WaitForFrame retires it through this value
		m_TimelineValue = l_SignalValue;
		m_SlotTimelineValues[GetFrameIndex()] = l_SignalValue;
		submittedValue = l_SignalValue;

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::Submit(VkQueue queue, VkCommandBuffer commandBuffer, uint32_t imageIndex, uint64_t& submittedValue)
	{
		submittedValue = 0;

		if (!IsInitialized())
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (imageIndex >= m_RenderFinishedSemaphores.size())
		{
			PT_CORE_ERROR("Swapchain image index {} is out of range for {} render finished semaphore(s)", imageIndex, m_RenderFinishedSemaphores.size());

			return VK_ERROR_UNKNOWN;
		}

		const uint64_t l_SignalValue = m_TimelineValue + 1;

		VkSemaphoreSubmitInfo l_WaitSemaphoreInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = m_ImageAvailableSemaphores[GetFrameIndex()],
				.value = 0,
				.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		};

		VkSemaphoreSubmitInfo l_SignalSemaphoreInfos[2]
		{
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = m_RenderFinishedSemaphores[imageIndex],
				.value = 0,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
			{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = m_TimelineSemaphore,
				.value = l_SignalValue,
				.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			},
		};

		VkCommandBufferSubmitInfo l_CommandBufferSubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = commandBuffer,
		};

		VkSubmitInfo2 l_SubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &l_WaitSemaphoreInfo,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &l_CommandBufferSubmitInfo,
			.signalSemaphoreInfoCount = 2,
			.pSignalSemaphoreInfos = l_SignalSemaphoreInfos,
		};

		const VkResult l_Result = vkQueueSubmit2(queue, 1, &l_SubmitInfo, VK_NULL_HANDLE);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkQueueSubmit2: {}", VulkanUtilities::ResultToString(l_Result));

			return l_Result;
		}

		// Only counted as in flight once the queue accepted the work, and only a present batch moves the slot along
		m_TimelineValue = l_SignalValue;
		m_SlotTimelineValues[GetFrameIndex()] = l_SignalValue;
		m_SubmittedFrameCount += 1;
		submittedValue = l_SignalValue;

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::WaitForAllFrames()
	{
		if (!IsInitialized())
		{
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (m_TimelineValue == 0)
		{
			return VK_SUCCESS;
		}

		return WaitForTimelineValue(m_TimelineValue);
	}

	void VulkanSynchronization::MarkAcquirePending()
	{
		const uint32_t l_FrameSlot = GetFrameIndex();
		if (l_FrameSlot < m_AcquireFencePending.size())
		{
			m_AcquireFencePending[l_FrameSlot] = true;
		}
	}

	VkResult VulkanSynchronization::WaitForPendingAcquires()
	{
		for (uint32_t i = 0; i < m_AcquireFences.size(); i++)
		{
			const VkResult l_Result = WaitForAcquire(i);
			if (l_Result != VK_SUCCESS)
			{
				return l_Result;
			}
		}

		return VK_SUCCESS;
	}

	VkSemaphore VulkanSynchronization::GetImageAvailableSemaphore() const
	{
		if (m_ImageAvailableSemaphores.empty())
		{
			return VK_NULL_HANDLE;
		}

		return m_ImageAvailableSemaphores[GetFrameIndex()];
	}

	VkFence VulkanSynchronization::GetAcquireFence() const
	{
		if (m_AcquireFences.empty())
		{
			return VK_NULL_HANDLE;
		}

		return m_AcquireFences[GetFrameIndex()];
	}

	VkSemaphore VulkanSynchronization::GetRenderFinishedSemaphore(uint32_t imageIndex) const
	{
		if (imageIndex >= m_RenderFinishedSemaphores.size())
		{
			return VK_NULL_HANDLE;
		}

		return m_RenderFinishedSemaphores[imageIndex];
	}

	VkResult VulkanSynchronization::CreateTimelineSemaphore()
	{
		PT_CORE_TRACE("Creating Timeline Semaphore");

		VkSemaphoreTypeCreateInfo l_SemaphoreTypeCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0,
		};

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &l_SemaphoreTypeCreateInfo,
		};

		const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_TimelineSemaphore);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_CRITICAL("Failed vkCreateSemaphore for the timeline semaphore: {}", VulkanUtilities::ResultToString(l_Result));

			m_TimelineSemaphore = VK_NULL_HANDLE;

			return l_Result;
		}

		PT_CORE_TRACE("Timeline Semaphore Created");

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::CreateFrameSemaphores()
	{
		PT_CORE_TRACE("Creating Image Available Semaphores");

		m_ImageAvailableSemaphores.resize(k_MaxFramesInFlight, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); i++)
		{
			const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphores[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateSemaphore for image available semaphore {}: {}", i, VulkanUtilities::ResultToString(l_Result));

				DestroyFrameSemaphores();

				return l_Result;
			}
		}

		PT_CORE_TRACE("Image Available Semaphores Created: {}", m_ImageAvailableSemaphores.size());

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::CreateAcquireFences()
	{
		PT_CORE_TRACE("Creating Acquire Fences");

		m_AcquireFences.resize(k_MaxFramesInFlight, VK_NULL_HANDLE);
		m_AcquireFencePending.assign(k_MaxFramesInFlight, false);

		// Unsignaled, vkAcquireNextImageKHR requires it and WaitForAcquire resets it after every use
		VkFenceCreateInfo l_FenceCreateInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

		for (size_t i = 0; i < m_AcquireFences.size(); i++)
		{
			const VkResult l_Result = vkCreateFence(m_Device->GetHandle(), &l_FenceCreateInfo, nullptr, &m_AcquireFences[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateFence for acquire fence {}: {}", i, VulkanUtilities::ResultToString(l_Result));

				DestroyAcquireFences();

				return l_Result;
			}
		}

		PT_CORE_TRACE("Acquire Fences Created: {}", m_AcquireFences.size());

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::CreateImageSemaphores(uint32_t swapchainImageCount)
	{
		PT_CORE_TRACE("Creating Render Finished Semaphores");

		m_RenderFinishedSemaphores.resize(swapchainImageCount, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++)
		{
			const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateSemaphore for render finished semaphore {}: {}", i, VulkanUtilities::ResultToString(l_Result));

				DestroyImageSemaphores();

				return l_Result;
			}
		}

		PT_CORE_TRACE("Render Finished Semaphores Created: {}", m_RenderFinishedSemaphores.size());

		return VK_SUCCESS;
	}

	void VulkanSynchronization::DestroyImageSemaphores()
	{
		if (m_RenderFinishedSemaphores.empty())
		{
			return;
		}

		for (VkSemaphore l_Semaphore : m_RenderFinishedSemaphores)
		{
			if (l_Semaphore != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_Device->GetHandle(), l_Semaphore, nullptr);
			}
		}

		m_RenderFinishedSemaphores.clear();
	}

	void VulkanSynchronization::DestroyFrameSemaphores()
	{
		if (m_ImageAvailableSemaphores.empty())
		{
			return;
		}

		for (VkSemaphore l_Semaphore : m_ImageAvailableSemaphores)
		{
			if (l_Semaphore != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(m_Device->GetHandle(), l_Semaphore, nullptr);
			}
		}

		m_ImageAvailableSemaphores.clear();
	}

	void VulkanSynchronization::DestroyAcquireFences()
	{
		if (m_AcquireFences.empty())
		{
			return;
		}

		for (VkFence l_Fence : m_AcquireFences)
		{
			if (l_Fence != VK_NULL_HANDLE)
			{
				vkDestroyFence(m_Device->GetHandle(), l_Fence, nullptr);
			}
		}

		m_AcquireFences.clear();
		m_AcquireFencePending.clear();
	}

	void VulkanSynchronization::DestroyTimelineSemaphore()
	{
		if (m_TimelineSemaphore == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_TRACE("Destroying Timeline Semaphore");

		vkDestroySemaphore(m_Device->GetHandle(), m_TimelineSemaphore, nullptr);
		m_TimelineSemaphore = VK_NULL_HANDLE;

		PT_CORE_TRACE("Timeline Semaphore Destroyed");
	}

	VkResult VulkanSynchronization::WaitForAcquire(uint32_t frameSlot)
	{
		if (frameSlot >= m_AcquireFences.size() || !m_AcquireFencePending[frameSlot])
		{
			return VK_SUCCESS;
		}

		// The presentation engine signals this fence when it releases the image, whether or not the image is ever presented
		VkResult l_Result = vkWaitForFences(m_Device->GetHandle(), 1, &m_AcquireFences[frameSlot], VK_TRUE, UINT64_MAX);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkWaitForFences for acquire fence {}: {}", frameSlot, VulkanUtilities::ResultToString(l_Result));

			return l_Result;
		}

		// Fence rules: reset only after the wait established it is signaled and nothing else is pending on it
		l_Result = vkResetFences(m_Device->GetHandle(), 1, &m_AcquireFences[frameSlot]);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkResetFences for acquire fence {}: {}", frameSlot, VulkanUtilities::ResultToString(l_Result));

			return l_Result;
		}

		m_AcquireFencePending[frameSlot] = false;

		return VK_SUCCESS;
	}

	VkResult VulkanSynchronization::WaitForTimelineValue(uint64_t value)
	{
		VkSemaphoreWaitInfo l_WaitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &m_TimelineSemaphore,
			.pValues = &value,
		};

		const VkResult l_Result = vkWaitSemaphores(m_Device->GetHandle(), &l_WaitInfo, UINT64_MAX);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkWaitSemaphores for timeline value {}: {}", value, VulkanUtilities::ResultToString(l_Result));

			return l_Result;
		}

		return VK_SUCCESS;
	}
}