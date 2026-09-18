struct GradientParameters
{
	float4 BaseColor;
	uint2 Extent;
	float Phase;
	float Padding;
};

// Binding 0: the storage image, the explicit format matches VK_FORMAT_R32G32B32A32_SFLOAT and avoids the write-without-format feature
[[vk::binding(0, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Output;

// Binding 1: frame-local parameters, one uniform buffer per frame slot
[[vk::binding(1, 0)]]
ConstantBuffer<GradientParameters> u_Parameters;

// Must match k_GradientWorkgroupSize
static const uint k_WorkgroupSize = 8;

[shader("compute")]
[numthreads(k_WorkgroupSize, k_WorkgroupSize, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
	const uint2 l_Pixel = threadId.xy;

	uint l_Width = 0;
	uint l_Height = 0;
	u_Output.GetDimensions(l_Width, l_Height);

	// The dispatch rounds up to whole workgroups, so edge invocations can land outside the image
	if (l_Pixel.x >= l_Width || l_Pixel.y >= l_Height)
	{
		return;
	}

	// Pixel centres normalized against the extent the C++ side believes the image has, a mismatch shows up as a stretched gradient
	const float2 l_UV = (float2(l_Pixel) + 0.5) / float2(max(u_Parameters.Extent, uint2(1, 1)));

	// Horizontal wave that scrolls with the phase the client advances from FrameTime
	const float l_Wave = 0.5 + 0.5 * sin(u_Parameters.Phase + 6.28318530718 * l_UV.x);

	const float3 l_Gradient = float3(l_UV.x, l_UV.y, l_Wave);

	// The clear color survives as a tint, so the Space binding in Sandbox still changes the picture
	const float3 l_Color = u_Parameters.BaseColor.rgb + l_Gradient * 0.9;

	u_Output[l_Pixel] = float4(l_Color, 1.0);
}