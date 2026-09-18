#include "Engine/Renderer/Vulkan/VulkanComputePipeline.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

namespace Engine
{
	VulkanComputePipeline::VulkanComputePipeline() = default;

	VulkanComputePipeline::~VulkanComputePipeline()
	{
		Shutdown();
	}

	VkResult VulkanComputePipeline::Initialize(const VulkanDevice& device, const VulkanComputePipelineSpecification& specification)
	{
		if (m_Pipeline != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Compute pipeline '{}' is already initialized", m_DebugName);

			return VK_SUCCESS;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_ERROR("A valid logical device is required to create compute pipeline '{}'", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.Shader == nullptr || !specification.Shader->IsInitialized())
		{
			PT_CORE_ERROR("Compute pipeline '{}' needs an initialized shader module", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN COMPUTE PIPELINE -------");

		m_Device = &device;
		m_DebugName = specification.DebugName;

		VkResult l_Result = CreateDescriptorSetLayout(specification);
		if (l_Result != VK_SUCCESS)
		{
			Shutdown();

			return l_Result;
		}

		l_Result = CreatePipelineLayout(specification);
		if (l_Result != VK_SUCCESS)
		{
			Shutdown();

			return l_Result;
		}

		l_Result = CreatePipeline(specification);
		if (l_Result != VK_SUCCESS)
		{
			Shutdown();

			return l_Result;
		}

		PT_CORE_TRACE("Compute Pipeline '{}' Created", m_DebugName);
		PT_CORE_INFO("------- VULKAN COMPUTE PIPELINE INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanComputePipeline::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN COMPUTE PIPELINE -------");

		// The caller has retired every submission that bound this pipeline, the renderer waits for all frames before shutting resources down
		VkDevice l_Device = m_Device->GetHandle();

		if (m_Pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(l_Device, m_Pipeline, nullptr);
			m_Pipeline = VK_NULL_HANDLE;
		}

		if (m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(l_Device, m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
		}

		if (m_DescriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(l_Device, m_DescriptorSetLayout, nullptr);
			m_DescriptorSetLayout = VK_NULL_HANDLE;
		}

		PT_CORE_TRACE("Compute Pipeline '{}' Destroyed", m_DebugName);
		PT_CORE_INFO("------- VULKAN COMPUTE PIPELINE SHUTDOWN COMPLETE -------");

		m_Device = nullptr;
	}

	void VulkanComputePipeline::Bind(VkCommandBuffer commandBuffer) const
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
	}

	void VulkanComputePipeline::PushDescriptors(VkCommandBuffer commandBuffer, std::span<const VkWriteDescriptorSet> writes) const
	{
		if (writes.empty())
		{
			return;
		}

		// Core in Vulkan 1.4 through the pushDescriptor feature enabled in VulkanDevice
		vkCmdPushDescriptorSet(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_PipelineLayout, 0, static_cast<uint32_t>(writes.size()), writes.data());
	}

	VkResult VulkanComputePipeline::CreateDescriptorSetLayout(const VulkanComputePipelineSpecification& specification)
	{
		VkDescriptorSetLayoutCreateInfo l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
			.bindingCount = static_cast<uint32_t>(specification.Bindings.size()),
			.pBindings = specification.Bindings.empty() ? nullptr : specification.Bindings.data(),
		};

		const VkResult l_Result = vkCreateDescriptorSetLayout(m_Device->GetHandle(), &l_CreateInfo, nullptr, &m_DescriptorSetLayout);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateDescriptorSetLayout for '{}': {}", m_DebugName, VulkanUtilities::ResultToString(l_Result));

			m_DescriptorSetLayout = VK_NULL_HANDLE;
		}

		return l_Result;
	}

	VkResult VulkanComputePipeline::CreatePipelineLayout(const VulkanComputePipelineSpecification& specification)
	{
		VkPipelineLayoutCreateInfo l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &m_DescriptorSetLayout,
			.pushConstantRangeCount = static_cast<uint32_t>(specification.PushConstantRanges.size()),
			.pPushConstantRanges = specification.PushConstantRanges.empty() ? nullptr : specification.PushConstantRanges.data(),
		};

		const VkResult l_Result = vkCreatePipelineLayout(m_Device->GetHandle(), &l_CreateInfo, nullptr, &m_PipelineLayout);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreatePipelineLayout for '{}': {}", m_DebugName, VulkanUtilities::ResultToString(l_Result));

			m_PipelineLayout = VK_NULL_HANDLE;
		}

		return l_Result;
	}

	VkResult VulkanComputePipeline::CreatePipeline(const VulkanComputePipelineSpecification& specification)
	{
		VkPipelineShaderStageCreateInfo l_StageCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = specification.Shader->GetHandle(),
			.pName = specification.EntryPoint,
		};

		VkComputePipelineCreateInfo l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = l_StageCreateInfo,
			.layout = m_PipelineLayout,
		};

		const VkResult l_Result = vkCreateComputePipelines(m_Device->GetHandle(), VK_NULL_HANDLE, 1, &l_CreateInfo, nullptr, &m_Pipeline);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateComputePipelines for '{}': {}", m_DebugName, VulkanUtilities::ResultToString(l_Result));

			m_Pipeline = VK_NULL_HANDLE;
		}

		return l_Result;
	}
}