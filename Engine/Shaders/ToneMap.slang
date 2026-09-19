// Must match ToneMapParameters in VulkanRenderer.cpp, std430 push constant layout: uint2 at 0, uint2 at 8, float at 16, three floats of padding to 32
struct ToneMapParameters
{
	uint2 InputExtent; // The view's output image
	uint2 OutputExtent; // The swapchain image
	float Exposure;
	float Padding0;
	float Padding1;
	float Padding2;
};

// Binding 0: the linear scene color the view wrote, fetched by texel at the nearest source pixel so no sampler is needed when the two extents differ
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

	// The dispatch rounds up to whole workgroups, the C++ side verified the swapchain image has this extent
	if (l_Pixel.x >= u_Parameters.OutputExtent.x || l_Pixel.y >= u_Parameters.OutputExtent.y)
	{
		return;
	}

	// Nearest source pixel, the view renders at its own resolution and the window shows it stretched until Step 10 gives the Editor a sampled display texture
	const uint2 l_Source = min((l_Pixel * u_Parameters.InputExtent) / u_Parameters.OutputExtent, u_Parameters.InputExtent - uint2(1, 1));

	const float3 l_Linear = u_Input.Load(int3(int2(l_Source), 0)).rgb;

	// Exposure is a linear scene-light scale applied before the curve
	const float3 l_Display = ToneMapACES(l_Linear * u_Parameters.Exposure);

	u_Output[l_Pixel] = float4(LinearToSrgb(l_Display), 1.0);
}