#pragma once

#ifndef PT_ENGINE_BUILD
#error "Engine internal header, not part of the public API"
#endif

#include "Engine/Math/Math.hpp"
#include "Engine/Renderer/Vulkan/VulkanImage.hpp"

#include <volk.h>

#include <cstdint>

namespace Engine
{
	class VulkanDevice;
	class VulkanMemoryAllocator;

	// Everything that restarts the accumulation when it changes, compared exactly: any change resets, exposure and selection never appear here
	struct RenderViewKey
	{
		Math::Vector3 CameraPosition{ 0.0f, 0.0f, 0.0f };
		Math::Quaternion CameraOrientation = Math::k_IdentityRotation;
		float VerticalFieldOfView = 0.0f;
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint64_t SceneRevision = 0;
		uint32_t MaxBounces = 0;
		uint32_t Seed = 0;
		uint32_t Mode = 0;

		bool operator==(const RenderViewKey&) const = default;
	};

	// The GPU side of one RenderView: the running radiance sum, the resolved output the tone map reads, and the sample count. Independent of the swapchain image and the frame slot
	class VulkanRenderView
	{
	public:
		VulkanRenderView();
		~VulkanRenderView();

		VulkanRenderView(const VulkanRenderView&) = delete;
		VulkanRenderView& operator=(const VulkanRenderView&) = delete;
		VulkanRenderView(VulkanRenderView&&) = delete;
		VulkanRenderView& operator=(VulkanRenderView&&) = delete;

		// Images are created by the first Resize, once a request names the extent
		VkResult Initialize(const VulkanDevice& device, const VulkanMemoryAllocator& allocator);
		void Shutdown();

		bool IsInitialized() const { return m_Device != nullptr; }
		bool HasImages() const { return m_AccumulationImage.IsInitialized() && m_OutputImage.IsInitialized(); }

		// True when Resize must run for this extent. The caller retires every GPU user of the current images before calling Resize
		bool NeedsResize(uint32_t width, uint32_t height) const;
		VkResult Resize(uint32_t width, uint32_t height);

		// Restarts the accumulation when the key differs from the one the samples were rendered with, returns true when it did
		bool Invalidate(const RenderViewKey& key);

		// Called only once the queue accepted the submission carrying the samples, a frame that fails after recording never counts
		void CommitSamples(uint32_t sampleCount);

		uint64_t GetAccumulatedSamples() const { return m_AccumulatedSamples; }
		VkExtent2D GetExtent() const { return VkExtent2D{ m_Width, m_Height }; }
		VulkanImage& GetAccumulationImage() { return m_AccumulationImage; }
		VulkanImage& GetOutputImage() { return m_OutputImage; }

	private:
		void DestroyImages();

	private:
		const VulkanDevice* m_Device = nullptr;
		const VulkanMemoryAllocator* m_Allocator = nullptr;

		VulkanImage m_AccumulationImage; // Running radiance sum, storage only
		VulkanImage m_OutputImage; // Running mean, or the diagnostic view, storage for the view pass and sampled by the tone map

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		uint64_t m_AccumulatedSamples = 0;
		RenderViewKey m_Key;
	};
}