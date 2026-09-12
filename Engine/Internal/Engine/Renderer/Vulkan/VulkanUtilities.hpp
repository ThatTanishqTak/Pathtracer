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
	}
}