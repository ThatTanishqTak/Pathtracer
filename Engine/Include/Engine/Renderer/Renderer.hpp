#pragma once

#include <memory>

namespace Engine
{
	class VulkanRenderer;
	class Window;

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		void Initialize(const Window& window);
		void Shutdown();

		void Render();

	private:
		std::unique_ptr<VulkanRenderer> m_VulkanRenderer;
	};
}