#pragma once

#include <memory>

namespace Engine
{
	class VulkanRenderer;

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		void Initialize();
		void Shutdown();

		void Render();

	private:
		std::unique_ptr<VulkanRenderer> m_VulkanRenderer;
	};
}