#include "Engine/Renderer/Renderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Core/Log.hpp"

namespace Engine
{
	Renderer::Renderer() = default;
	Renderer::~Renderer() = default;

	void Renderer::Initialize(const Window& window)
	{
		m_VulkanRenderer = std::make_unique<VulkanRenderer>();
		m_VulkanRenderer->Initialize(window);
	}

	bool Renderer::IsInitialized() const
	{
		return m_VulkanRenderer && m_VulkanRenderer->IsInitialized();
	}

	void Renderer::Shutdown()
	{
		if (m_VulkanRenderer)
		{
			m_VulkanRenderer->Shutdown();
			m_VulkanRenderer.reset();
		}
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