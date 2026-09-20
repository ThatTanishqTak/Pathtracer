// Must match ToneMapParameters in VulkanRenderer.cpp, std430 push constant layout: uint2 at 0, float at 8, one float of padding to 16
struct ToneMapParameters
{
	uint2 Extent; // The view extent, shared by the output image read and the display image written
	float Exposure;
	float Padding0;
};

// Binding 0: the linear scene color the view wrote, fetched by texel with Load, the pass runs at the view extent so no sampler is needed
[[vk::binding(0, 0)]]
Texture2D<float4> u_Input;

// Binding 1: the view's display texture, an R8G8B8A8_UNORM image the engine owns, so the format is declared and the write never relies on write-without-format It holds sRGB-encoded display values from here on, the display pass and the UI sample it without encoding again
[[vk::binding(1, 0)]]
[format("rgba8")]
RWTexture2D<float4> u_Output;

// One small block per dispatch, pushed straight into the command buffer
[[vk::push_constant]]
ConstantBuffer<ToneMapParameters> u_Parameters;

// Must match k_ToneMapWorkgroupSize
static const uint k_WorkgroupSize = 8;

// ACES filmic curve, the Narkowicz fit, maps linear scene light into [0, 1] display light
float3 ToneMapACES(float3 color)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;

	return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

// The display texture and the swapchain are UNORM images in the sRGB nonlinear color space, so the transfer function is applied here, once, not by the hardware
float3 LinearToSrgb(float3 color)
{
	const float3 l_Low = color * 12.92;
	const float3 l_High = 1.055 * pow(color, float3(1.0 / 2.4)) - 0.055;

	return select(color <= float3(0.0031308), l_Low, l_High);
}

[shader("compute")]
[numthreads(k_WorkgroupSize, k_WorkgroupSize, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
	const uint2 l_Pixel = threadId.xy;

    // The dispatch rounds up to whole workgroups, both images have the view extent
    if (l_Pixel.x >= u_Parameters.Extent.x || l_Pixel.y >= u_Parameters.Extent.y)
    {
        return;
    }

    // Same-size pass, the display pass or the UI viewport resamples the result to wherever it is shown
    const float3 l_Linear = u_Input.Load(int3(int2(l_Pixel), 0)).rgb;

	// Exposure is a linear scene-light scale applied before the curve
	const float3 l_Display = ToneMapACES(l_Linear * u_Parameters.Exposure);

	u_Output[l_Pixel] = float4(LinearToSrgb(l_Display), 1.0);
}