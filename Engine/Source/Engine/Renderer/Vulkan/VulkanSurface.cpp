#include "Engine/Renderer/Vulkan/VulkanSurface.hpp"

#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanSurface::VulkanSurface() = default;
	VulkanSurface::~VulkanSurface() = default;

	void VulkanSurface::Initialize()
	{
		PT_CORE_INFO("------- INITIALIZING VULKAN SURFACE -------");



		PT_CORE_INFO("------- VULKAN SURFACE INITIALIZED -------");
	}

	void VulkanSurface::Shutdown()
	{
		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SURFACE -------");



		PT_CORE_INFO("------- VULKAN SURFACE SHUTDOWN COMPLETE -------");
	}

	void VulkanSurface::CreateSurface()
	{

	}
}