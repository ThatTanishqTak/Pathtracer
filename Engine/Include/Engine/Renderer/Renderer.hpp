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

		bool IsInitialized() const;

		void Render();

		void OnFramebufferResized();

	private:
		std::unique_ptr<VulkanRenderer> m_VulkanRenderer;
	};
}