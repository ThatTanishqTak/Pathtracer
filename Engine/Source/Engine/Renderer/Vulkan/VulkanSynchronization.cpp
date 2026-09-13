#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
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
		m_SubmittedFrameCount = 0;
		m_SwapchainGeneration = swapchain.GetGeneration();

		CreateTimelineSemaphore();
		if (m_TimelineSemaphore == VK_NULL_HANDLE)
		{
			Shutdown();

			return;
		}

		CreateFrameSemaphores();
		if (m_ImageAvailableSemaphores.empty())
		{
			Shutdown();

			return;
		}

		if (swapchain.GetImageCount() > 0)
		{
			CreateImageSemaphores(swapchain.GetImageCount());
			if (m_RenderFinishedSemaphores.empty())
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

		if (m_Device->IsInitialized())
		{
			vkDeviceWaitIdle(m_Device->GetHandle());
		}

		DestroyImageSemaphores();
		DestroyFrameSemaphores();
		DestroyTimelineSemaphore();

		m_SubmittedFrameCount = 0;
		m_SwapchainGeneration = 0;
		m_Swapchain = nullptr;
		m_Device = nullptr;

		PT_CORE_INFO("------- VULKAN SYNCHRONIZATION SHUTDOWN COMPLETE -------");
	}

	void VulkanSynchronization::RehookBinarySemaphores()
	{
		if (m_Swapchain == nullptr || !m_Swapchain->IsInitialized())
		{
			return;
		}

		if (m_Swapchain->GetGeneration() == m_SwapchainGeneration)
		{
			return;
		}

		PT_CORE_TRACE("Rehooking Binary Semaphores");

		vkDeviceWaitIdle(m_Device->GetHandle());

		DestroyImageSemaphores();
		DestroyFrameSemaphores();

		CreateFrameSemaphores();
		if (m_ImageAvailableSemaphores.empty())
		{
			PT_CORE_ERROR("Failed to recreate frame semaphores, retrying next frame");

			return;
		}

		CreateImageSemaphores(m_Swapchain->GetImageCount());
		if (m_RenderFinishedSemaphores.empty())
		{
			PT_CORE_ERROR("Failed to recreate image semaphores, retrying next frame");

			DestroyFrameSemaphores();

			return;
		}

		m_SwapchainGeneration = m_Swapchain->GetGeneration();

		PT_CORE_TRACE("Binary Semaphores Rehooked");
	}

	bool VulkanSynchronization::WaitForFrame()
	{
		if (!IsInitialized())
		{
			return false;
		}

		RehookBinarySemaphores();

		if (m_ImageAvailableSemaphores.empty())
		{
			return false;
		}

		if (m_SubmittedFrameCount < k_MaxFramesInFlight)
		{
			return true;
		}

		return WaitForTimelineValue(m_SubmittedFrameCount + 1 - k_MaxFramesInFlight);
	}

	bool VulkanSynchronization::Submit(VkQueue queue, VkCommandBuffer commandBuffer, uint32_t imageIndex)
	{
		if (!IsInitialized())
		{
			return false;
		}

		if (imageIndex >= m_RenderFinishedSemaphores.size())
		{
			PT_CORE_ERROR("Swapchain image index {} is out of range for {} render finished semaphore(s)", imageIndex, m_RenderFinishedSemaphores.size());

			return false;
		}

		VkSemaphoreSubmitInfo l_WaitSemaphoreInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = m_ImageAvailableSemaphores[GetFrameIndex()],
			.value = 0,
			.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
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
				.value = m_SubmittedFrameCount + 1,
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
			PT_CORE_ERROR("Failed vkQueueSubmit2: {}", static_cast<int>(l_Result));

			return false;
		}

		m_SubmittedFrameCount++;

		return true;
	}

	void VulkanSynchronization::WaitForAllFrames()
	{
		if (!IsInitialized() || m_SubmittedFrameCount == 0)
		{
			return;
		}

		WaitForTimelineValue(m_SubmittedFrameCount);
	}

	void VulkanSynchronization::RecoverAbandonedAcquire()
	{
		if (!IsInitialized() || m_ImageAvailableSemaphores.empty())
		{
			return;
		}

		vkDeviceWaitIdle(m_Device->GetHandle());

		const uint32_t l_FrameIndex = GetFrameIndex();

		vkDestroySemaphore(m_Device->GetHandle(), m_ImageAvailableSemaphores[l_FrameIndex], nullptr);
		m_ImageAvailableSemaphores[l_FrameIndex] = VK_NULL_HANDLE;

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphores[l_FrameIndex]);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed to recreate image available semaphore: {}", static_cast<int>(l_Result));

			DestroyFrameSemaphores();
		}
	}

	VkSemaphore VulkanSynchronization::GetImageAvailableSemaphore() const
	{
		if (m_ImageAvailableSemaphores.empty())
		{
			return VK_NULL_HANDLE;
		}

		return m_ImageAvailableSemaphores[GetFrameIndex()];
	}

	VkSemaphore VulkanSynchronization::GetRenderFinishedSemaphore(uint32_t imageIndex) const
	{
		if (imageIndex >= m_RenderFinishedSemaphores.size())
		{
			return VK_NULL_HANDLE;
		}

		return m_RenderFinishedSemaphores[imageIndex];
	}

	void VulkanSynchronization::CreateTimelineSemaphore()
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
			PT_CORE_CRITICAL("Failed vkCreateSemaphore for the timeline semaphore: {}", static_cast<int>(l_Result));

			m_TimelineSemaphore = VK_NULL_HANDLE;

			return;
		}

		PT_CORE_TRACE("Timeline Semaphore Created");
	}

	void VulkanSynchronization::CreateFrameSemaphores()
	{
		PT_CORE_TRACE("Creating Image Available Semaphores");

		m_ImageAvailableSemaphores.resize(k_MaxFramesInFlight, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); i++)
		{
			const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphores[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateSemaphore for image available semaphore {}: {}", i, static_cast<int>(l_Result));

				DestroyFrameSemaphores();

				return;
			}
		}

		PT_CORE_TRACE("Image Available Semaphores Created: {}", m_ImageAvailableSemaphores.size());
	}

	void VulkanSynchronization::CreateImageSemaphores(uint32_t swapchainImageCount)
	{
		PT_CORE_TRACE("Creating Render Finished Semaphores");

		m_RenderFinishedSemaphores.resize(swapchainImageCount, VK_NULL_HANDLE);

		VkSemaphoreCreateInfo l_SemaphoreCreateInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); i++)
		{
			const VkResult l_Result = vkCreateSemaphore(m_Device->GetHandle(), &l_SemaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			if (l_Result != VK_SUCCESS)
			{
				PT_CORE_CRITICAL("Failed vkCreateSemaphore for render finished semaphore {}: {}", i, static_cast<int>(l_Result));

				DestroyImageSemaphores();

				return;
			}
		}

		PT_CORE_TRACE("Render Finished Semaphores Created: {}", m_RenderFinishedSemaphores.size());
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

	bool VulkanSynchronization::WaitForTimelineValue(uint64_t value)
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
			PT_CORE_ERROR("Failed vkWaitSemaphores for timeline value {}: {}", value, static_cast<int>(l_Result));

			return false;
		}

		return true;
	}
}