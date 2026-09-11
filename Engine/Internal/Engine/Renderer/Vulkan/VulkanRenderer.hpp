#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <memory>

namespace Engine
{
	class VulkanInstance;
	class Window;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		void Initialize(const Window& window);
		void Shutdown();

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;

		const Window* m_Window = nullptr;
	};
}