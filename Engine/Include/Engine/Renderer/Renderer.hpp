#pragma once

#include "Engine/Renderer/RenderRequest.hpp"

#include <memory>

namespace Engine
{
	class VulkanRenderer;
	class Window;

	// What the application can act on, the backend keeps the finer distinctions for its own recovery and logging
	enum class RenderOutcome
	{
		Presented, // A frame was submitted and handed to the presentation engine
		Skipped, // Nothing was presented this call, retry next iteration (window not renderable, swapchain being replaced)
		Fatal, // Rendering cannot continue, the application must stop the loop and shut down
	};

	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		void Initialize(const Window& window);
		void Shutdown();

		bool IsInitialized() const;
		RenderOutcome Render(const RenderRequest& request);

		void OnFramebufferResized();

	private:
		std::unique_ptr<VulkanRenderer> m_VulkanRenderer;
	};
}