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

	struct VulkanComputePipelineSpecification
	{
		const VulkanShaderModule* Shader = nullptr;
		const char* EntryPoint = "main";

		// One descriptor set, bound with push descriptors so no descriptor pool or per-frame set lifetime exists yet
		std::span<const VkDescriptorSetLayoutBinding> Bindings;
		std::span<const VkPushConstantRange> PushConstantRanges;

		const char* DebugName = "compute pipeline";
	};

	// Descriptor set layout, pipeline layout and compute pipeline for one shader
	class VulkanComputePipeline
	{
	public:
		VulkanComputePipeline();
		~VulkanComputePipeline();

		VulkanComputePipeline(const VulkanComputePipeline&) = delete;
		VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
		VulkanComputePipeline(VulkanComputePipeline&&) = delete;
		VulkanComputePipeline& operator=(VulkanComputePipeline&&) = delete;

		VkResult Initialize(const VulkanDevice& device, const VulkanComputePipelineSpecification& specification);
		void Shutdown();

		bool IsInitialized() const { return m_Pipeline != VK_NULL_HANDLE; }

		void Bind(VkCommandBuffer commandBuffer) const;

		// Writes go straight into the command buffer, dstSet is ignored and set 0 is always the target
		void PushDescriptors(VkCommandBuffer commandBuffer, std::span<const VkWriteDescriptorSet> writes) const;

		VkPipeline GetHandle() const { return m_Pipeline; }
		VkPipelineLayout GetLayout() const { return m_PipelineLayout; }
		VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }

	private:
		VkResult CreateDescriptorSetLayout(const VulkanComputePipelineSpecification& specification);
		VkResult CreatePipelineLayout(const VulkanComputePipelineSpecification& specification);
		VkResult CreatePipeline(const VulkanComputePipelineSpecification& specification);

	private:
		const VulkanDevice* m_Device = nullptr;

		VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;

		const char* m_DebugName = "compute pipeline";
	};
}