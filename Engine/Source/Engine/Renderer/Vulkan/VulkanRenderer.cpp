#include "Engine/Renderer/Vulkan/VulkanRenderer.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanRenderer::VulkanRenderer() = default;
	VulkanRenderer::~VulkanRenderer() = default;

	void VulkanRenderer::Initialize()
	{
		m_VulkanInstance = std::make_unique<VulkanInstance>();
		m_VulkanInstance->Initialize();
	}

	void VulkanRenderer::Shutdown()
	{
		if (m_VulkanInstance)
		{
			m_VulkanInstance->Shutdown();
			m_VulkanInstance.reset();
		}
	}
}