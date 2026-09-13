#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
#include "Engine/Renderer/Vulkan/VulkanCommandPool.hpp"
#include "Engine/Core/Log.hpp"

#include <volk.h>

#include <cstdint>

namespace Engine
{
	namespace
	{
		// Placeholder until the pathtracer output is blitted into the swapchain
		constexpr VkClearColorValue k_ClearColor{ .float32 = { 0.05f, 0.05f, 0.05f, 1.0f } };
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
			return;
		}

		m_VulkanSurface = std::make_unique<VulkanSurface>();
		m_VulkanSurface->Initialize(*m_VulkanInstance, window);
		if (!m_VulkanSurface->IsInitialized())
		{
			return;
		}

		m_VulkanDevice = std::make_unique<VulkanDevice>();
		m_VulkanDevice->Initialize(*m_VulkanInstance, *m_VulkanSurface);
		if (!m_VulkanDevice->IsInitialized())
		{
			return;
		}

		m_VulkanMemoryAllocator = std::make_unique<VulkanMemoryAllocator>();
		m_VulkanMemoryAllocator->Initialize(*m_VulkanInstance, *m_VulkanDevice);
		if (!m_VulkanMemoryAllocator->IsInitialized())
		{
			return;
		}

		m_VulkanSwapchain = std::make_unique<VulkanSwapchain>();
		m_VulkanSwapchain->Initialize(*m_VulkanDevice, *m_VulkanSurface, window);
		if (!m_VulkanSwapchain->IsInitialized())
		{
			return;
		}

		m_VulkanSynchronization = std::make_unique<VulkanSynchronization>();
		m_VulkanSynchronization->Initialize(*m_VulkanDevice, *m_VulkanSwapchain);
		if (!m_VulkanSynchronization->IsInitialized())
		{
			return;
		}

		m_VulkanCommandPool = std::make_unique<VulkanCommandPool>();
		m_VulkanCommandPool->Initialize(*m_VulkanDevice, static_cast<uint32_t>(VulkanSynchronization::k_MaxFramesInFlight));
		if (!m_VulkanCommandPool->IsInitialized())
		{
			return;
		}

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		const bool l_CoreReady = m_VulkanInstance && m_VulkanInstance->IsInitialized()
			&& m_VulkanSurface && m_VulkanSurface->IsInitialized()
			&& m_VulkanDevice && m_VulkanDevice->IsInitialized()
			&& m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized();

		const bool l_FrameReady = m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized()
			&& m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized()
			&& m_VulkanCommandPool && m_VulkanCommandPool->IsInitialized();

		return l_CoreReady && l_FrameReady;
	}

	bool VulkanRenderer::Render()
	{
		if (!IsInitialized())
		{
			return false;
		}

		// Blocks until the frame that last used this slot has finished on the GPU, and rehooks semaphores after a swapchain recreate
		if (!m_VulkanSynchronization->WaitForFrame())
		{
			return false;
		}

		uint32_t l_ImageIndex = 0;
		if (!m_VulkanSwapchain->AcquireNextImage(m_VulkanSynchronization->GetImageAvailableSemaphore(), l_ImageIndex))
		{
			// Swapchain was recreated or is not renderable, the next frame will rehook and retry
			return false;
		}

		const uint32_t l_FrameIndex = m_VulkanSynchronization->GetFrameIndex();
		VkCommandBuffer l_CommandBuffer = m_VulkanCommandPool->GetCommandBuffer(l_FrameIndex);

		if (!m_VulkanCommandPool->Begin(l_FrameIndex))
		{
			m_VulkanSynchronization->RecoverAbandonedAcquire();

			return false;
		}

		RecordFrame(l_CommandBuffer, l_ImageIndex);

		if (!m_VulkanCommandPool->End(l_FrameIndex))
		{
			m_VulkanSynchronization->RecoverAbandonedAcquire();

			return false;
		}

		if (!m_VulkanSynchronization->Submit(m_VulkanDevice->GetGraphicsQueue(), l_CommandBuffer, l_ImageIndex))
		{
			m_VulkanSynchronization->RecoverAbandonedAcquire();

			return false;
		}

		return m_VulkanSwapchain->Present(m_VulkanDevice->GetGraphicsQueue(), m_VulkanSynchronization->GetRenderFinishedSemaphore(l_ImageIndex), l_ImageIndex);
	}

	void VulkanRenderer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex)
	{
		VkImage l_Image = m_VulkanSwapchain->GetImage(imageIndex);

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

		vkCmdClearColorImage(commandBuffer, l_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &k_ClearColor, 1, &l_ColorRange);

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