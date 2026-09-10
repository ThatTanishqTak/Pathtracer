#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <memory>

namespace Engine
{
	class VulkanInstance;

	class VulkanRenderer
	{
	public:
		VulkanRenderer();
		~VulkanRenderer();

		void Initialize();
		void Shutdown();

	private:
		std::unique_ptr<VulkanInstance> m_VulkanInstance;
	};
}