#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include <volk.h>

#include <cstring>
#include <vector>

namespace Engine
{
	namespace VulkanUtilities
	{
		// The set can change between the count and fill calls, VK_INCOMPLETE means retry
		template<typename T, typename EnumerateFn>
		VkResult Enumerate(std::vector<T>& out, EnumerateFn&& enumerate)
		{
			uint32_t l_Count = 0;
			VkResult l_Result = VK_INCOMPLETE;

			do
			{
				l_Result = enumerate(&l_Count, nullptr);

				if (l_Result != VK_SUCCESS)
				{
					break;
				}

				out.resize(l_Count);
				l_Result = enumerate(&l_Count, out.data());
			} while (l_Result == VK_INCOMPLETE);

			if (l_Result != VK_SUCCESS)
			{
				out.clear();

				return l_Result;
			}

			// The fill call wrote back the count it actually delivered
			out.resize(l_Count);

			return VK_SUCCESS;
		}

		inline bool IsExtensionSupported(const std::vector<VkExtensionProperties>& available, const char* name)
		{
			for (const VkExtensionProperties& l_Extension : available)
			{
				if (std::strcmp(l_Extension.extensionName, name) == 0)
				{
					return true;
				}
			}

			return false;
		}

		inline const char* ResultToString(VkResult result)
		{
			switch (result)
			{
				case VK_SUCCESS:
				{
					return "VK_SUCCESS";
				}
				case VK_NOT_READY:
				{
					return "VK_NOT_READY";
				}
				case VK_TIMEOUT:
				{
					return "VK_TIMEOUT";
				}
				case VK_EVENT_SET:
				{
					return "VK_EVENT_SET";
				}
				case VK_EVENT_RESET:
				{
					return "VK_EVENT_RESET";
				}
				case VK_INCOMPLETE:
				{
					return "VK_INCOMPLETE";
				}
				case VK_ERROR_OUT_OF_HOST_MEMORY:
				{
					return "VK_ERROR_OUT_OF_HOST_MEMORY";
				}
				case VK_ERROR_OUT_OF_DEVICE_MEMORY:
				{
					return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
				}
				case VK_ERROR_INITIALIZATION_FAILED:
				{
					return "VK_ERROR_INITIALIZATION_FAILED";
				}
				case VK_ERROR_DEVICE_LOST:
				{
					return "VK_ERROR_DEVICE_LOST";
				}
				case VK_ERROR_MEMORY_MAP_FAILED:
				{
					return "VK_ERROR_MEMORY_MAP_FAILED";
				}
				case VK_ERROR_LAYER_NOT_PRESENT:
				{
					return "VK_ERROR_LAYER_NOT_PRESENT";
				}
				case VK_ERROR_EXTENSION_NOT_PRESENT:
				{
					return "VK_ERROR_EXTENSION_NOT_PRESENT";
				}
				case VK_ERROR_FEATURE_NOT_PRESENT:
				{
					return "VK_ERROR_FEATURE_NOT_PRESENT";
				}
				case VK_ERROR_INCOMPATIBLE_DRIVER:
				{
					return "VK_ERROR_INCOMPATIBLE_DRIVER";
				}
				case VK_ERROR_TOO_MANY_OBJECTS:
				{
					return "VK_ERROR_TOO_MANY_OBJECTS";
				}
				case VK_ERROR_FORMAT_NOT_SUPPORTED:
				{
					return "VK_ERROR_FORMAT_NOT_SUPPORTED";
				}
				case VK_ERROR_FRAGMENTED_POOL:
				{
					return "VK_ERROR_FRAGMENTED_POOL";
				}
				case VK_ERROR_UNKNOWN:
				{
					return "VK_ERROR_UNKNOWN";
				}
				case VK_ERROR_OUT_OF_POOL_MEMORY:
				{
					return "VK_ERROR_OUT_OF_POOL_MEMORY";
				}
				case VK_ERROR_INVALID_EXTERNAL_HANDLE:
				{
					return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
				}
				case VK_ERROR_FRAGMENTATION:
				{
					return "VK_ERROR_FRAGMENTATION";
				}
				case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
				{
					return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
				}
				case VK_PIPELINE_COMPILE_REQUIRED:
				{
					return "VK_PIPELINE_COMPILE_REQUIRED";
				}
				case VK_ERROR_SURFACE_LOST_KHR:
				{
					return "VK_ERROR_SURFACE_LOST_KHR";
				}
				case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
				{
					return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
				}
				case VK_SUBOPTIMAL_KHR:
				{
					return "VK_SUBOPTIMAL_KHR";
				}
				case VK_ERROR_OUT_OF_DATE_KHR:
				{
					return "VK_ERROR_OUT_OF_DATE_KHR";
				}
				case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
				{
					return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
				}
				case VK_ERROR_VALIDATION_FAILED_EXT:
				{
					return "VK_ERROR_VALIDATION_FAILED_EXT";
				}
				case VK_ERROR_INVALID_SHADER_NV:
				{
					return "VK_ERROR_INVALID_SHADER_NV";
				}
				case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
				{
					return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
				}
				case VK_ERROR_NOT_PERMITTED:
				{
					return "VK_ERROR_NOT_PERMITTED";
				}
				case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
				{
					return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
				}
				case VK_THREAD_IDLE_KHR:
				{
					return "VK_THREAD_IDLE_KHR";
				}
				case VK_THREAD_DONE_KHR:
				{
					return "VK_THREAD_DONE_KHR";
				}
				case VK_OPERATION_DEFERRED_KHR:
				{
					return "VK_OPERATION_DEFERRED_KHR";
				}
				case VK_OPERATION_NOT_DEFERRED_KHR:
				{
					return "VK_OPERATION_NOT_DEFERRED_KHR";
				}
				case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:
				{
					return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
				}
				default:
				{
					return "VK_UNKNOWN_RESULT";
				}
			}
		}
	}
}