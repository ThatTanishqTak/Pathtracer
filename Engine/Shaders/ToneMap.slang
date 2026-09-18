// Must match ToneMapParameters in VulkanRenderer.cpp, std430 push constant layout: uint2 at 0, float at 8, float at 12
struct ToneMapParameters
{
	uint2 Extent;
	float Exposure;
	float Padding;
};

// Binding 0: the linear scene color the diagnostic pass wrote, fetched by texel so a same-size pass needs no sampler
[[vk::binding(0, 0)]]
Texture2D<float4> u_Input;

// Binding 1: the swapchain image, left without a format because storage images must match the view's format exactly and B8G8R8A8 has no SPIR-V equivalent
// The write relies on the shaderStorageImageWriteWithoutFormat feature VulkanDevice requires
[[vk::binding(1, 0)]]
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

// The swapchain is a UNORM image in the sRGB nonlinear color space, so the transfer function is applied here, not by the hardware
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

	// The dispatch rounds up to whole workgroups, the C++ side verified both images have this extent
	if (l_Pixel.x >= u_Parameters.Extent.x || l_Pixel.y >= u_Parameters.Extent.y)
	{
		return;
	}

	const float3 l_Linear = u_Input.Load(int3(int2(l_Pixel), 0)).rgb;

	// Exposure is a linear scene-light scale applied before the curve
	const float3 l_Display = ToneMapACES(l_Linear * u_Parameters.Exposure);

	u_Output[l_Pixel] = float4(LinearToSrgb(l_Display), 1.0);
}