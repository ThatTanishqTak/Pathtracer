// The progressive path tracer: BSDF sampling only, lit by the constant environment and emissive geometry, accumulated across frames while the view stays still
import Camera;
import SceneRecords;
import Intersection;
import Sampling;
import Bsdf;

// Must match PathtraceParameters in VulkanRenderer.cpp, std140 uniform layout: CameraFrameBlock at 0, uint2 at 64, uints at 72 and 76, float4 at 80, uints at 96, 100, 104 and 108
struct PathtraceParameters
{
	CameraFrameBlock Camera;
	uint2 Extent;
	uint PrimitiveCount;
	uint MaterialCount;
	float4 EnvironmentRadiance; // Linear RGB returned on a miss, w unused
	uint SampleIndex; // Samples already accumulated before this dispatch, zero means the accumulation image is overwritten
	uint SamplesPerFrame; // Paths traced per pixel by this dispatch, at least one
	uint MaxBounces; // Reflections after the camera ray, zero shows emitters and the environment only
	uint Seed;
};

// Binding 0: the running radiance sum, read and rewritten every dispatch, the explicit format matches VK_FORMAT_R32G32B32A32_SFLOAT
[[vk::binding(0, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Accumulation;

// Binding 1: the view's linear output image the tone map reads, the running mean of the accumulation
[[vk::binding(1, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Output;

// Bindings 2 and 3: the scene records the renderer extracted, PrimitiveCount and MaterialCount bound the reads
[[vk::binding(2, 0)]]
StructuredBuffer<PrimitiveRecord> u_Primitives;

[[vk::binding(3, 0)]]
StructuredBuffer<MaterialRecord> u_Materials;

// Binding 4: the per-slot constants, a uniform buffer because the diagnostic push block already uses 96 of the 128 guaranteed bytes
[[vk::binding(4, 0)]]
ConstantBuffer<PathtraceParameters> u_Parameters;

// Bindings 5 and 6: the vertices and triangles of every mesh the records reference, read through each mesh record's triangle range
[[vk::binding(5, 0)]]
StructuredBuffer<VertexRecord> u_Vertices;

[[vk::binding(6, 0)]]
StructuredBuffer<TriangleRecord> u_Triangles;

// Must match k_PathtraceWorkgroupSize
static const uint k_WorkgroupSize = 8;

MaterialRecord LoadMaterial(uint index)
{
	// Index 0 is the fallback the extraction always writes, so an index past the upload reads it instead of undefined memory
	return u_Materials[index < u_Parameters.MaterialCount ? index : 0];
}

// One path from the camera: emitted radiance is collected where the path lands, the environment where it escapes. The bounce limit is the documented quality limit of this step
float3 TracePath(Ray ray, inout RandomState random)
{
	float3 l_Radiance = float3(0.0, 0.0, 0.0);
	float3 l_Throughput = float3(1.0, 1.0, 1.0);

	// The camera ray is segment 0, each bounce adds one segment
	for (uint i_Segment = 0; i_Segment <= u_Parameters.MaxBounces; ++i_Segment)
    {
        const Hit l_Hit = TraceClosest(ray, u_Primitives, u_Parameters.PrimitiveCount, u_Vertices, u_Triangles, i_Segment == 0 ? 0.0 : k_MinRayDistance, k_MaxRayDistance);
		if (l_Hit.Distance < 0.0)
		{
			l_Radiance += l_Throughput * u_Parameters.EnvironmentRadiance.xyz;
			break;
		}

		const MaterialRecord l_Material = LoadMaterial(l_Hit.MaterialIndex);
		l_Radiance += l_Throughput * l_Material.EmittedRadiance.xyz;

		if (i_Segment == u_Parameters.MaxBounces)
		{
			break;
		}

		// Diffuse surfaces are two-sided and the hit normal already faces the ray, so the BSDF sees an outgoing direction on its hemisphere
		const BsdfSample l_Sample = SampleBsdf(l_Material, l_Hit.Normal, -ray.Direction, NextFloat2(random));
		if (l_Sample.Pdf <= 0.0)
		{
			break;
		}

		l_Throughput *= l_Sample.Weight;
		if (max(l_Throughput.x, max(l_Throughput.y, l_Throughput.z)) <= 0.0)
		{
			// A black albedo, the emissive quad for example, ends the path without another trace
			break;
		}

		ray.Origin = OffsetRayOrigin(l_Hit.Position, l_Hit.Normal);
		ray.Direction = l_Sample.Direction;
	}

	return l_Radiance;
}

[shader("compute")]
[numthreads(k_WorkgroupSize, k_WorkgroupSize, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
	const uint2 l_Pixel = threadId.xy;
	const uint2 l_Extent = u_Parameters.Extent;

	// The dispatch rounds up to whole workgroups, the C++ side verified both images have this extent
	if (l_Pixel.x >= l_Extent.x || l_Pixel.y >= l_Extent.y)
	{
		return;
	}

	float3 l_Sum = float3(0.0, 0.0, 0.0);
	for (uint i_Sample = 0; i_Sample < u_Parameters.SamplesPerFrame; ++i_Sample)
	{
		// The stream depends on the pixel and the global sample index, so every frame draws new numbers and the same seed reproduces the same image
		RandomState l_Random = InitializeRandom(l_Pixel, u_Parameters.SampleIndex + i_Sample, u_Parameters.Seed);

		// Subpixel jitter changes only the x and y the shared ray expression sees
		const Ray l_Ray = GenerateCameraRay(u_Parameters.Camera, l_Pixel, l_Extent, NextFloat2(l_Random));

		float3 l_Radiance = TracePath(l_Ray, l_Random);

		// One non-finite sample would poison the running sum for the rest of the accumulation
		if (any(isnan(l_Radiance)) || any(isinf(l_Radiance)))
		{
			l_Radiance = float3(0.0, 0.0, 0.0);
		}

		l_Sum += l_Radiance;
	}

	// S_new = S_old + sum of the new samples, N_new = N_old + k, the output shows S_new / N_new. A zero SampleIndex ignores whatever the image holds, which is how a reset works without a clear
	const float3 l_Previous = u_Parameters.SampleIndex == 0 ? float3(0.0, 0.0, 0.0) : u_Accumulation[l_Pixel].xyz;
	const float3 l_Total = l_Previous + l_Sum;
	const uint l_SampleCount = u_Parameters.SampleIndex + u_Parameters.SamplesPerFrame;

	u_Accumulation[l_Pixel] = float4(l_Total, 0.0);
	u_Output[l_Pixel] = float4(l_Total / float(max(l_SampleCount, 1u)), 1.0);
}