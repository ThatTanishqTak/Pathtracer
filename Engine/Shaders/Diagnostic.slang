// Primary-ray views of the uploaded scene. Must match DiagnosticParameters in VulkanRenderer.cpp, std430 push constant layout: CameraFrameBlock at 0, uint2 at 64, then five uints from 72
import Camera;
import SceneRecords;
import Intersection;

struct DiagnosticParameters
{
	CameraFrameBlock Camera;
	uint2 Extent;
	uint Mode;
	uint PrimitiveCount;
	uint MaterialCount;
	uint Padding0;
	uint Padding1;
	uint Padding2;
};

// Must match Engine::DiagnosticMode, PathTraced is handled by Pathtrace.slang and never reaches this shader
static const uint k_ModeRayDirection = 0;
static const uint k_ModeHit = 1;
static const uint k_ModeNormal = 2;
static const uint k_ModeDistance = 3;
static const uint k_ModeBaseColor = 4;

// Distances beyond this render black in the distance mode
static const float k_MaxDisplayDistance = 10.0;

// Binding 0: the view's linear output image the tone map reads, the explicit format matches VK_FORMAT_R32G32B32A32_SFLOAT
[[vk::binding(0, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Output;

// Bindings 1 and 2: the scene records the renderer extracted, PrimitiveCount and MaterialCount bound the reads
[[vk::binding(1, 0)]]
StructuredBuffer<PrimitiveRecord> u_Primitives;

[[vk::binding(2, 0)]]
StructuredBuffer<MaterialRecord> u_Materials;

// Bindings 3 and 4: the vertices and triangles of every mesh the records reference, read through each mesh record's triangle range
[[vk::binding(3, 0)]]
StructuredBuffer<VertexRecord> u_Vertices;

[[vk::binding(4, 0)]]
StructuredBuffer<TriangleRecord> u_Triangles;

// One small block per dispatch, pushed straight into the command buffer
[[vk::push_constant]]
ConstantBuffer<DiagnosticParameters> u_Parameters;

// Must match k_DiagnosticWorkgroupSize
static const uint k_WorkgroupSize = 8;

// Sky above the horizon and ground below it, so a miss still shows which way is up
float3 MissColor(float3 direction)
{
	return direction.y >= 0.0 ? float3(0.25, 0.4, 0.7) : float3(0.15, 0.12, 0.1);
}

[shader("compute")]
[numthreads(k_WorkgroupSize, k_WorkgroupSize, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
	const uint2 l_Pixel = threadId.xy;
	const uint2 l_Extent = u_Parameters.Extent;

	// The dispatch rounds up to whole workgroups, the C++ side verified the image has this extent
	if (l_Pixel.x >= l_Extent.x || l_Pixel.y >= l_Extent.y)
	{
		return;
	}

	const Ray l_Ray = GenerateCameraRay(u_Parameters.Camera, l_Pixel, l_Extent);
	const uint l_Mode = u_Parameters.Mode;

	float3 l_Color;
	if (l_Mode == k_ModeRayDirection)
	{
		// World-space direction as RGB, the centre pixel of an identity camera is (0.5, 0.5, 0)
		l_Color = l_Ray.Direction * 0.5 + 0.5;
	}
	else if (l_Mode == k_ModeHit || l_Mode == k_ModeNormal || l_Mode == k_ModeDistance || l_Mode == k_ModeBaseColor)
    {
        const Hit l_Hit = TraceClosest(l_Ray, u_Primitives, u_Parameters.PrimitiveCount, u_Vertices, u_Triangles, 0.0, k_MaxRayDistance);
		if (l_Hit.Distance < 0.0)
		{
			l_Color = l_Mode == k_ModeHit ? MissColor(l_Ray.Direction) : float3(0.0, 0.0, 0.0);
		}
		else if (l_Mode == k_ModeHit)
		{
			l_Color = float3(1.0, 1.0, 1.0);
		}
		else if (l_Mode == k_ModeNormal)
		{
			l_Color = l_Hit.Normal * 0.5 + 0.5;
		}
		else if (l_Mode == k_ModeDistance)
		{
			// Brighter is closer, black at and beyond the display range
			l_Color = float3(saturate(1.0 - l_Hit.Distance / k_MaxDisplayDistance));
		}
		else if (l_Hit.MaterialIndex < u_Parameters.MaterialCount)
		{
			l_Color = u_Materials[l_Hit.MaterialIndex].BaseColor.xyz;
		}
		else
		{
			// A material index the upload does not cover is an extraction bug, magenta makes it visible
			l_Color = float3(1.0, 0.0, 1.0);
		}
	}
	else
	{
		// An unknown mode is a C++ side bug, magenta makes it visible
		l_Color = float3(1.0, 0.0, 1.0);
	}

	u_Output[l_Pixel] = float4(l_Color, 1.0);
}