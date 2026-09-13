#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Renderer/Vulkan/VulkanMemoryAllocator.hpp"
#include "Engine/Renderer/Vulkan/VulkanSwapchain.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
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

		PT_CORE_INFO("------- VULKAN RENDERER INITIALIZED -------");
	}

	bool VulkanRenderer::IsInitialized() const
	{
		return m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanSurface && m_VulkanSurface->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized() && m_VulkanMemoryAllocator && m_VulkanMemoryAllocator->IsInitialized() && m_VulkanSwapchain && m_VulkanSwapchain->IsInitialized() && m_VulkanSynchronization && m_VulkanSynchronization->IsInitialized();
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