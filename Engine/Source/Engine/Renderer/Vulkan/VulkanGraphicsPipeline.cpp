#include "Engine/Renderer/Vulkan/VulkanGraphicsPipeline.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanShaderModule.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/Log.hpp"

#include <array>

namespace Engine
{
	VulkanGraphicsPipeline::VulkanGraphicsPipeline() = default;
	VulkanGraphicsPipeline::~VulkanGraphicsPipeline() = default;

	VkResult VulkanGraphicsPipeline::Initialize(const VulkanDevice& device, const VulkanGraphicsPipelineSpecification& specification)
	{
		if (m_Pipeline != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Graphics pipeline '{}' is already initialized", m_DebugName);

			return VK_SUCCESS;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_ERROR("A valid logical device is required to create graphics pipeline '{}'", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.Shader == nullptr || !specification.Shader->IsInitialized())
		{
			PT_CORE_ERROR("Graphics pipeline '{}' needs an initialized shader module", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (specification.ColorFormat == VK_FORMAT_UNDEFINED)
		{
			PT_CORE_ERROR("Graphics pipeline '{}' needs the format of the attachment it renders into", specification.DebugName);

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN GRAPHICS PIPELINE -------");

		m_Device = &device;
		m_DebugName = specification.DebugName;
		m_ColorFormat = specification.ColorFormat;

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

		PT_CORE_TRACE("Graphics Pipeline '{}' Created", m_DebugName);
		PT_CORE_INFO("------- VULKAN GRAPHICS PIPELINE INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanGraphicsPipeline::Shutdown()
	{
		if (m_Device == nullptr)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN GRAPHICS PIPELINE -------");

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

		PT_CORE_TRACE("Graphics Pipeline '{}' Destroyed", m_DebugName);
		PT_CORE_INFO("------- VULKAN GRAPHICS PIPELINE SHUTDOWN COMPLETE -------");

		m_ColorFormat = VK_FORMAT_UNDEFINED;
		m_Device = nullptr;
	}

	void VulkanGraphicsPipeline::Bind(VkCommandBuffer commandBuffer) const
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
	}

	void VulkanGraphicsPipeline::PushDescriptors(VkCommandBuffer commandBuffer, std::span<const VkWriteDescriptorSet> writes) const
	{
		if (writes.empty())
		{
			return;
		}

		// Core in Vulkan 1.4 through the pushDescriptor feature enabled in VulkanDevice
		vkCmdPushDescriptorSet(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, static_cast<uint32_t>(writes.size()), writes.data());
	}

	void VulkanGraphicsPipeline::PushConstants(VkCommandBuffer commandBuffer, VkShaderStageFlags stages, const void* data, uint32_t size) const
	{
		if (data == nullptr || size == 0)
		{
			return;
		}

		vkCmdPushConstants(commandBuffer, m_PipelineLayout, stages, 0, size, data);
	}

	VkResult VulkanGraphicsPipeline::CreateDescriptorSetLayout(const VulkanGraphicsPipelineSpecification& specification)
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

	VkResult VulkanGraphicsPipeline::CreatePipelineLayout(const VulkanGraphicsPipelineSpecification& specification)
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

	VkResult VulkanGraphicsPipeline::CreatePipeline(const VulkanGraphicsPipelineSpecification& specification)
	{
		// Both entry points come out of the same module, -fvk-use-entrypoint-name kept their Slang names
		const std::array<VkPipelineShaderStageCreateInfo, 2> l_Stages
		{ {
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = specification.Shader->GetHandle(),
				.pName = specification.VertexEntryPoint,
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = specification.Shader->GetHandle(),
				.pName = specification.FragmentEntryPoint,
			},
		} };

		// No vertex buffers, the vertex shader derives its positions from the vertex index
		const VkPipelineVertexInputStateCreateInfo l_VertexInputState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		};

		const VkPipelineInputAssemblyStateCreateInfo l_InputAssemblyState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			.primitiveRestartEnable = VK_FALSE,
		};

		// Viewport and scissor are dynamic, the counts are all the pipeline fixes
		const VkPipelineViewportStateCreateInfo l_ViewportState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		const VkPipelineRasterizationStateCreateInfo l_RasterizationState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
			.lineWidth = 1.0f,
		};

		const VkPipelineMultisampleStateCreateInfo l_MultisampleState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = VK_FALSE,
		};

		// Opaque writes, the pass replaces the attachment
		const VkPipelineColorBlendAttachmentState l_BlendAttachment
		{
			.blendEnable = VK_FALSE,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		const VkPipelineColorBlendStateCreateInfo l_ColorBlendState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = &l_BlendAttachment,
		};

		const std::array<VkDynamicState, 2> l_DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

		const VkPipelineDynamicStateCreateInfo l_DynamicState
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = static_cast<uint32_t>(l_DynamicStates.size()),
			.pDynamicStates = l_DynamicStates.data(),
		};

		// Dynamic rendering: the attachment format replaces a render pass, the dynamicRendering feature is enabled in VulkanDevice
		const VkPipelineRenderingCreateInfo l_RenderingInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &m_ColorFormat,
		};

		const VkGraphicsPipelineCreateInfo l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &l_RenderingInfo,
			.stageCount = static_cast<uint32_t>(l_Stages.size()),
			.pStages = l_Stages.data(),
			.pVertexInputState = &l_VertexInputState,
			.pInputAssemblyState = &l_InputAssemblyState,
			.pViewportState = &l_ViewportState,
			.pRasterizationState = &l_RasterizationState,
			.pMultisampleState = &l_MultisampleState,
			.pDepthStencilState = nullptr,
			.pColorBlendState = &l_ColorBlendState,
			.pDynamicState = &l_DynamicState,
			.layout = m_PipelineLayout,
			.renderPass = VK_NULL_HANDLE,
		};

		const VkResult l_Result = vkCreateGraphicsPipelines(m_Device->GetHandle(), VK_NULL_HANDLE, 1, &l_CreateInfo, nullptr, &m_Pipeline);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateGraphicsPipelines for '{}': {}", m_DebugName, VulkanUtilities::ResultToString(l_Result));

			m_Pipeline = VK_NULL_HANDLE;
		}

		return l_Result;
	}
}