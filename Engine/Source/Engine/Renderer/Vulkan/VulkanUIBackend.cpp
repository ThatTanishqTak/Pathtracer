#include "Engine/Renderer/Vulkan/VulkanUIBackend.hpp"

#include "Engine/Renderer/Vulkan/VulkanInstance.hpp"
#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanSynchronization.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <bit>

namespace Engine
{
	namespace
	{
		// Sampled-image descriptors the backend's own pool hands out: the font atlas, the live and the retired view texture, and room for the Step 11 overlays
		constexpr uint32_t k_DescriptorPoolSize = 16;

		// The backend cycles its vertex and index buffers through this many sets, one more than the frames in flight so a set is never rewritten before WaitForFrame retired the batch that read it
		constexpr uint32_t k_RenderBufferSets = static_cast<uint32_t>(VulkanSynchronization::k_MaxFramesInFlight) + 1;

		// ImTextureID is a 64-bit value and VkDescriptorSet is 64 bits wide on every target, so the handle's bits pass through unchanged
		static_assert(sizeof(VkDescriptorSet) == sizeof(uint64_t), "VkDescriptorSet must fit the 64-bit texture id");

		void CheckResult(VkResult result)
		{
			if (result != VK_SUCCESS)
			{
				PT_CORE_ERROR("Dear ImGui Vulkan backend call failed: {}", VulkanUtilities::ResultToString(result));
			}
		}
	}

	VulkanUIBackend::VulkanUIBackend() = default;
	VulkanUIBackend::~VulkanUIBackend() = default;

	VkResult VulkanUIBackend::Initialize(const VulkanInstance& instance, const VulkanDevice& device, VkFormat colorFormat)
	{
		if (m_Initialized)
		{
			PT_CORE_WARN("Vulkan UI backend is already initialized");

			return VK_SUCCESS;
		}

		if (!instance.IsInitialized() || !device.IsInitialized())
		{
			PT_CORE_ERROR("A valid instance and device are required to create the UI backend");

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (colorFormat == VK_FORMAT_UNDEFINED)
		{
			PT_CORE_ERROR("The UI backend needs the swapchain format its pipeline renders into");

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (ImGui::GetCurrentContext() == nullptr)
		{
			PT_CORE_ERROR("The UI layer must create the Dear ImGui context before the Vulkan backend");

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN UI BACKEND -------");

		m_Device = &device;
		m_ColorFormat = colorFormat;

		// Dynamic rendering against the swapchain format. The backend copies the format array during Init, so the pointer into this object is only read here
		VkPipelineRenderingCreateInfo l_RenderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &m_ColorFormat,
		};

		// volk already loaded every entry point and IMGUI_IMPL_VULKAN_USE_VOLK makes the backend call them directly, the 1.4 API version makes it pick the core vkCmdBeginRendering for its own passes
		ImGui_ImplVulkan_InitInfo l_InitInfo{};
		l_InitInfo.ApiVersion = VK_API_VERSION_1_4;
		l_InitInfo.Instance = instance.GetHandle();
		l_InitInfo.PhysicalDevice = device.GetPhysicalDevice();
		l_InitInfo.Device = device.GetHandle();
		l_InitInfo.QueueFamily = device.GetGraphicsQueueFamilyIndex();
		l_InitInfo.Queue = device.GetGraphicsQueue();
		l_InitInfo.DescriptorPool = VK_NULL_HANDLE; // The backend creates its own pool of DescriptorPoolSize sampled images
		l_InitInfo.DescriptorPoolSize = k_DescriptorPoolSize;
		l_InitInfo.MinImageCount = 2;
		l_InitInfo.ImageCount = k_RenderBufferSets;
		l_InitInfo.UseDynamicRendering = true;
		l_InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		l_InitInfo.PipelineInfoMain.PipelineRenderingCreateInfo = l_RenderingInfo;
		l_InitInfo.CheckVkResultFn = &CheckResult;

		if (!ImGui_ImplVulkan_Init(&l_InitInfo))
		{
			PT_CORE_ERROR("Failed to initialize the Dear ImGui Vulkan backend");

			m_ColorFormat = VK_FORMAT_UNDEFINED;
			m_Device = nullptr;

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		m_Initialized = true;

		PT_CORE_TRACE("UI Backend: {} descriptor(s), {} render buffer set(s), colour format {}", k_DescriptorPoolSize, k_RenderBufferSets, static_cast<int>(m_ColorFormat));
		PT_CORE_INFO("------- VULKAN UI BACKEND INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanUIBackend::Shutdown()
	{
		if (!m_Initialized)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN UI BACKEND -------");

		// The backend destroys its font texture, buffers and pools outright, so the presentation retirement policy applies: a checked device wait first
		if (m_Device != nullptr && m_Device->IsInitialized())
		{
			m_Device->WaitIdle();
		}

		ImGui_ImplVulkan_Shutdown();

		m_Initialized = false;
		m_ColorFormat = VK_FORMAT_UNDEFINED;
		m_Device = nullptr;

		PT_CORE_INFO("------- VULKAN UI BACKEND SHUTDOWN COMPLETE -------");
	}

	void VulkanUIBackend::Record(VkCommandBuffer commandBuffer) const
	{
		if (!m_Initialized)
		{
			return;
		}

		// Null until the first ImGui::Render, and the backend itself skips a zero-sized display. A new font atlas is uploaded from inside this call through the backend's own command buffer and a queue wait, once
		ImDrawData* l_DrawData = ImGui::GetDrawData();
		if (l_DrawData == nullptr || !l_DrawData->Valid)
		{
			return;
		}

		ImGui_ImplVulkan_RenderDrawData(l_DrawData, commandBuffer);
	}

	uint64_t VulkanUIBackend::RegisterTexture(VkImageView view, VkImageLayout layout) const
	{
		if (!m_Initialized || view == VK_NULL_HANDLE)
		{
			return 0;
		}

		// The backend binds its own linear sampler next to this sampled image, so the view needs no sampler of its own
		const VkDescriptorSet l_DescriptorSet = ImGui_ImplVulkan_AddTexture(view, layout);
		if (l_DescriptorSet == VK_NULL_HANDLE)
		{
			PT_CORE_ERROR("Failed to register a UI texture, the backend's pool of {} descriptor(s) is exhausted", k_DescriptorPoolSize);

			return 0;
		}

		return std::bit_cast<uint64_t>(l_DescriptorSet);
	}

	void VulkanUIBackend::UnregisterTexture(uint64_t textureId) const
	{
		if (!m_Initialized || textureId == 0)
		{
			return;
		}

		ImGui_ImplVulkan_RemoveTexture(std::bit_cast<VkDescriptorSet>(textureId));
	}
}