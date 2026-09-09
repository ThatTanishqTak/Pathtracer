#pragma once

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