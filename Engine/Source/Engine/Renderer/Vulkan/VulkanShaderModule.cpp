#include "Engine/Renderer/Vulkan/VulkanShaderModule.hpp"

#include "Engine/Renderer/Vulkan/VulkanDevice.hpp"
#include "Engine/Renderer/Vulkan/VulkanUtilities.hpp"
#include "Engine/Core/FileSystem.hpp"
#include "Engine/Core/Log.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Engine
{
	namespace
	{
		// First word of every SPIR-V module, see the SPIR-V specification's physical layout section
		constexpr uint32_t k_SpirvMagicNumber = 0x07230203u;
	}

	VulkanShaderModule::VulkanShaderModule() = default;
	VulkanShaderModule::~VulkanShaderModule() = default;

	VkResult VulkanShaderModule::Initialize(const VulkanDevice& device, const std::filesystem::path& path)
	{
		if (m_ShaderModule != VK_NULL_HANDLE)
		{
			PT_CORE_WARN("Shader module '{}' is already initialized", m_Path.string());

			return VK_SUCCESS;
		}

		if (!device.IsInitialized())
		{
			PT_CORE_ERROR("A valid logical device is required to create shader module '{}'", path.string());

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		PT_CORE_INFO("------- INITIALIZING VULKAN SHADER MODULE -------");

		std::vector<std::byte> l_Bytes;
		if (!FileSystem::ReadBinaryFile(path, l_Bytes))
		{
			PT_CORE_ERROR("Shader '{}' could not be read, was the shader build step run and is the Shaders directory next to the executable?", path.string());

			return VK_ERROR_INITIALIZATION_FAILED;
		}

		if (l_Bytes.size() < sizeof(uint32_t) || l_Bytes.size() % sizeof(uint32_t) != 0)
		{
			PT_CORE_ERROR("Shader '{}' is not a SPIR-V binary, its size {} is not a multiple of 4", path.string(), l_Bytes.size());

			return VK_ERROR_INVALID_SHADER_NV;
		}

		// pCode needs 4-byte alignment, a byte vector does not promise that, so the words are copied
		std::vector<uint32_t> l_Words(l_Bytes.size() / sizeof(uint32_t));
		std::memcpy(l_Words.data(), l_Bytes.data(), l_Bytes.size());

		if (l_Words[0] != k_SpirvMagicNumber)
		{
			PT_CORE_ERROR("Shader '{}' has an unexpected magic number {:#x}, it is not SPIR-V", path.string(), l_Words[0]);

			return VK_ERROR_INVALID_SHADER_NV;
		}

		m_Device = &device;
		m_Path = path;

		VkShaderModuleCreateInfo l_CreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = l_Bytes.size(),
			.pCode = l_Words.data(),
		};

		const VkResult l_Result = vkCreateShaderModule(m_Device->GetHandle(), &l_CreateInfo, nullptr, &m_ShaderModule);
		if (l_Result != VK_SUCCESS)
		{
			PT_CORE_ERROR("Failed vkCreateShaderModule for '{}': {}", path.string(), VulkanUtilities::ResultToString(l_Result));

			m_ShaderModule = VK_NULL_HANDLE;
			m_Device = nullptr;
			m_Path.clear();

			return l_Result;
		}

		PT_CORE_TRACE("Shader Module Created: {} ({} bytes)", path.filename().string(), l_Bytes.size());
		PT_CORE_INFO("------- VULKAN SHADER MODULE INITIALIZED -------");

		return VK_SUCCESS;
	}

	void VulkanShaderModule::Shutdown()
	{
		if (m_ShaderModule == VK_NULL_HANDLE)
		{
			return;
		}

		PT_CORE_INFO("------- SHUTTING DOWN VULKAN SHADER MODULE -------");

		vkDestroyShaderModule(m_Device->GetHandle(), m_ShaderModule, nullptr);

		PT_CORE_TRACE("Shader Module Destroyed: {}", m_Path.filename().string());

		m_ShaderModule = VK_NULL_HANDLE;
		m_Device = nullptr;
		m_Path.clear();

		PT_CORE_INFO("------- VULKAN SHADER MODULE SHUTDOWN COMPLETE -------");
	}
}