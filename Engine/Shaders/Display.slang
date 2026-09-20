// Binding 0: the view's display texture, sRGB-encoded UNORM the tone map wrote in the view batch
[[vk::binding(0, 0)]]
Texture2D<float4> u_Display;

// Binding 1: the sampler VulkanRenderer creates for it, linear so a view rendered below the window size is filtered up rather than fetched nearest
[[vk::binding(1, 0)]]
SamplerState u_Sampler;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

// One triangle covering the clip-space square from three vertex ids and no vertex buffer: texture coordinates (0, 0), (2, 0) and (0, 2), so the visible square maps to [0, 1]
[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
	VertexOutput l_Output;
	l_Output.TexCoord = float2((vertexId << 1) & 2, vertexId & 2);

	// Vulkan clip space has +Y down like the framebuffer, so texture row 0 lands on the top row of the window without a flip
	l_Output.Position = float4(l_Output.TexCoord * 2.0 - 1.0, 0.0, 1.0);

	return l_Output;
}

// The values are already display-encoded, the pass copies them into the UNORM swapchain image unchanged
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
	return float4(u_Display.Sample(u_Sampler, input.TexCoord).rgb, 1.0);
}