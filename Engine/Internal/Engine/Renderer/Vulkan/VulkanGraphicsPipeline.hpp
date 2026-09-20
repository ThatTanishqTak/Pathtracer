#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstdint>
#include <span>

namespace Engine
{
	class VulkanDevice;
	class VulkanShaderModule;

	// A screen-space graphics pipeline for dynamic rendering: no vertex input, one colour attachment, viewport and scissor set at record time
	struct VulkanGraphicsPipelineSpecification
	{
		const VulkanShaderModule* Shader = nullptr; // One binary holding both entry points, the Step 3 one-file-one-binary rule
		const char* VertexEntryPoint = "vertexMain";
		const char* FragmentEntryPoint = "fragmentMain";

		VkFormat ColorFormat = VK_FORMAT_UNDEFINED; // The attachment this pipeline renders into, the swapchain format for the display pass

		// One descriptor set, bound with push descriptors like the compute pipelines
		std::span<const VkDescriptorSetLayoutBinding> Bindings;
		std::span<const VkPushConstantRange> PushConstantRanges;

		const char* DebugName = "graphics pipeline";
	};

	// Descriptor set layout, pipeline layout and graphics pipeline for one vertex and fragment pair
	class VulkanGraphicsPipeline
	{
	public:
		VulkanGraphicsPipeline();
		~VulkanGraphicsPipeline();

		VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
		VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
		VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) = delete;
		VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) = delete;

		VkResult Initialize(const VulkanDevice& device, const VulkanGraphicsPipelineSpecification& specification);
		void Shutdown();

		bool IsInitialized() const { return m_Pipeline != VK_NULL_HANDLE; }

		void Bind(VkCommandBuffer commandBuffer) const;

		void PushDescriptors(VkCommandBuffer commandBuffer, std::span<const VkWriteDescriptorSet> writes) const; // Writes go straight into the command buffer, dstSet is ignored and set 0 is always the target
		void PushConstants(VkCommandBuffer commandBuffer, VkShaderStageFlags stages, const void* data, uint32_t size) const; // One block at offset 0 for the given stages, the specification's push constant range must cover size bytes

		VkPipeline GetHandle() const { return m_Pipeline; }
		VkPipelineLayout GetLayout() const { return m_PipelineLayout; }
		VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
		VkFormat GetColorFormat() const { return m_ColorFormat; }

	private:
		VkResult CreateDescriptorSetLayout(const VulkanGraphicsPipelineSpecification& specification);
		VkResult CreatePipelineLayout(const VulkanGraphicsPipelineSpecification& specification);
		VkResult CreatePipeline(const VulkanGraphicsPipelineSpecification& specification);

	private:
		const VulkanDevice* m_Device = nullptr;

		VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
		VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;

		const char* m_DebugName = "graphics pipeline";
	};
}