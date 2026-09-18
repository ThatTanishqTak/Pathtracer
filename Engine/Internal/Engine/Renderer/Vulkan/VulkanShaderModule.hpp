#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <filesystem>

namespace Engine
{
	class VulkanDevice;

	// Loads one SPIR-V binary produced by the shader rules in Engine/CMakeLists.txt
	class VulkanShaderModule
	{
	public:
		VulkanShaderModule();
		~VulkanShaderModule();

		VulkanShaderModule(const VulkanShaderModule&) = delete;
		VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
		VulkanShaderModule(VulkanShaderModule&&) = delete;
		VulkanShaderModule& operator=(VulkanShaderModule&&) = delete;

		// Reads the file, validates the SPIR-V header and creates the module, failures are logged with the path
		VkResult Initialize(const VulkanDevice& device, const std::filesystem::path& path);
		void Shutdown();

		bool IsInitialized() const { return m_ShaderModule != VK_NULL_HANDLE; }

		VkShaderModule GetHandle() const { return m_ShaderModule; }
		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		const VulkanDevice* m_Device = nullptr;

		VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
		std::filesystem::path m_Path;
	};
}