#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <memory>

namespace Engine
{
	class Window;
	class VulkanInstance;
	class VulkanDevice;
	class VulkanSurface;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		void Initialize(const Window& window);
		void Shutdown();

		bool IsInitialized() const;

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;
		std::unique_ptr<VulkanDevice> m_VulkanDevice;
		std::unique_ptr<VulkanSurface> m_VulkanSurface;

		const Window* m_Window = nullptr;
	};
}