#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() = default;

	void VulkanRenderer::Initialize(const Window& window)
	{
		m_Window = &window;

		m_VulkanInstance = std::make_unique<VulkanInstance>();
		m_VulkanInstance->Initialize(window);
		if (!m_VulkanInstance->IsInitialized())
		{
			return;
		}

		m_VulkanDevice = std::make_unique<VulkanDevice>();
		m_VulkanDevice->Initialize(*m_VulkanInstance);
		if (!m_VulkanDevice->IsInitialized())
		{
			return;
		}

		m_VulkanSurface = std::make_unique<VulkanSurface>();
		m_VulkanSurface->Initialize();
	}

	bool VulkanRenderer::IsInitialized() const
	{
		return m_VulkanInstance && m_VulkanInstance->IsInitialized() && m_VulkanDevice && m_VulkanDevice->IsInitialized();
	}

	void VulkanRenderer::Shutdown()
	{
		if (m_VulkanSurface)
		{
			m_VulkanSurface->Shutdown();
			m_VulkanSurface.reset();
		}

		if (m_VulkanDevice)
		{
			m_VulkanDevice->Shutdown();
			m_VulkanDevice.reset();
		}

		if (m_VulkanInstance)
		{
			m_VulkanInstance->Shutdown();
			m_VulkanInstance.reset();
		}

		m_Window = nullptr;
	}
}