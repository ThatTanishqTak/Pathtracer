#include "Engine/Renderer/Renderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Core/Log.hpp"

namespace Engine
{
	Renderer::Renderer() = default;
	Renderer::~Renderer() = default;

	void Renderer::Initialize(const Window& window)
	{
		if (m_VulkanRenderer)
		{
			PT_CORE_WARN("Renderer is already initialized");

			return;
		}

		PT_CORE_INFO("------- INITIALIZING RENDERER -------");

		m_VulkanRenderer = std::make_unique<VulkanRenderer>();
		m_VulkanRenderer->Initialize(window);

		PT_CORE_INFO("------- RENDERER INITIALIZED -------");
	}

	bool Renderer::IsInitialized() const
	{
		return m_VulkanRenderer && m_VulkanRenderer->IsInitialized();
	}

	void Renderer::Shutdown()
	{
		PT_CORE_INFO("------- SHUTTING DOWN RENDERER -------");

		if (m_VulkanRenderer)
		{
			m_VulkanRenderer->Shutdown();
			m_VulkanRenderer.reset();
		}

		PT_CORE_INFO("------- RENDERER SHUTDOWN COMPLETE -------");
	}

	void Renderer::Render()
	{

	}

	void Renderer::OnFramebufferResized()
	{
		if (m_VulkanRenderer)
		{
			m_VulkanRenderer->OnFramebufferResized();
		}
	}
}