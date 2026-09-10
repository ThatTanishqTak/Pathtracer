#include "Engine/Renderer/Renderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Core/Log.hpp"

#include <memory>

namespace Engine
{
	Renderer::Renderer() = default;
	Renderer::~Renderer() = default;

	void Renderer::Initialize()
	{
		m_VulkanRenderer = std::make_unique<VulkanRenderer>();
		m_VulkanRenderer->Initialize();
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
}