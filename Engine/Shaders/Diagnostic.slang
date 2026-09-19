struct CameraFrameBlock
{
	float4 Origin;
	float4 Forward;
	float4 RightScaled; // Right * aspect * tan(fov / 2)
	float4 UpScaled; // Up * tan(fov / 2)
};

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

// Must match Engine::RenderPrimitiveRecord. The matrices are four columns each, so no matrix layout convention is involved
struct PrimitiveRecord
{
	float4 ObjectToWorld[4]; // world = C0 * x + C1 * y + C2 * z + C3
	float4 WorldToObject[4];
	uint Type;
	uint MaterialIndex;
	uint EntityIdLow;
	uint EntityIdHigh;
};

// Must match Engine::RenderMaterialRecord
struct MaterialRecord
{
	float4 BaseColor; // Linear RGB, w unused
	float4 EmittedRadiance; // Linear RGB, w unused
	uint Type;
	uint Padding0;
	uint Padding1;
	uint Padding2;
};

// Must match Engine::DiagnosticMode
static const uint k_ModeRayDirection = 0;
static const uint k_ModeHit = 1;
static const uint k_ModeNormal = 2;
static const uint k_ModeDistance = 3;
static const uint k_ModeBaseColor = 4;

// Must match Engine::RenderPrimitiveType
static const uint k_PrimitiveSphere = 0;
static const uint k_PrimitiveQuad = 1;

// Distances beyond this render black in the distance mode
static const float k_MaxDisplayDistance = 10.0;

// Negative distances mean a miss everywhere below
static const float k_Miss = -1.0;

// Binding 0: the linear HDR image the tone map reads, the explicit format matches VK_FORMAT_R32G32B32A32_SFLOAT
[[vk::binding(0, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Output;

// Bindings 1 and 2: the scene records the renderer extracted, PrimitiveCount and MaterialCount bound the reads
[[vk::binding(1, 0)]]
StructuredBuffer<PrimitiveRecord> u_Primitives;

[[vk::binding(2, 0)]]
StructuredBuffer<MaterialRecord> u_Materials;

// One small block per dispatch, pushed straight into the command buffer
[[vk::push_constant]]
ConstantBuffer<DiagnosticParameters> u_Parameters;

// Must match k_DiagnosticWorkgroupSize
static const uint k_WorkgroupSize = 8;

struct Ray
{
	float3 Origin;
	float3 Direction;
};

struct Hit
{
	float Distance; // Along the world ray, k_Miss when nothing was hit
	float3 Normal; // Unit length, world space, facing the ray
	uint MaterialIndex;
};

// The same expression as Engine::GenerateCameraRay: pixel centres, row 0 at the top so +y is up
Ray GenerateCameraRay(uint2 pixel, uint2 extent)
{
	const float l_X = 2.0 * (float(pixel.x) + 0.5) / float(extent.x) - 1.0;
	const float l_Y = 1.0 - 2.0 * (float(pixel.y) + 0.5) / float(extent.y);

	Ray l_Ray;
	l_Ray.Origin = u_Parameters.Camera.Origin.xyz;
	l_Ray.Direction = normalize(u_Parameters.Camera.Forward.xyz + l_X * u_Parameters.Camera.RightScaled.xyz + l_Y * u_Parameters.Camera.UpScaled.xyz);

	return l_Ray;
}

float3 TransformPoint(PrimitiveRecord record, float3 point)
{
	return record.WorldToObject[0].xyz * point.x + record.WorldToObject[1].xyz * point.y + record.WorldToObject[2].xyz * point.z + record.WorldToObject[3].xyz;
}

// Not normalized on purpose: with the direction scaled by the same matrix as the origin, an object-space distance is also the world-space distance
float3 TransformDirection(PrimitiveRecord record, float3 direction)
{
	return record.WorldToObject[0].xyz * direction.x + record.WorldToObject[1].xyz * direction.y + record.WorldToObject[2].xyz * direction.z;
}

// Normals transform by the inverse transpose of ObjectToWorld, which is the transpose of WorldToObject: each component is a dot with one column
float3 TransformNormal(PrimitiveRecord record, float3 normal)
{
	return float3(dot(record.WorldToObject[0].xyz, normal), dot(record.WorldToObject[1].xyz, normal), dot(record.WorldToObject[2].xyz, normal));
}

// Nearest positive distance along an object-space ray to the unit sphere at the origin, the direction need not be unit length
float IntersectUnitSphere(float3 origin, float3 direction)
{
	const float l_A = dot(direction, direction);
	const float l_B = dot(origin, direction);
	const float l_C = dot(origin, origin) - 1.0;
	const float l_Discriminant = l_B * l_B - l_A * l_C;
	if (l_Discriminant < 0.0 || l_A <= 0.0)
	{
		return k_Miss;
	}

	const float l_Root = sqrt(l_Discriminant);

	const float l_Near = (-l_B - l_Root) / l_A;
	if (l_Near > 0.0)
	{
		return l_Near;
	}

	// The origin is inside the sphere, the far intersection is the one in front
	const float l_Far = (-l_B + l_Root) / l_A;

	return l_Far > 0.0 ? l_Far : k_Miss;
}

// Positive distance to the unit square in the object XY plane, both sides count
float IntersectUnitQuad(float3 origin, float3 direction)
{
	if (abs(direction.z) < 1e-8)
	{
		return k_Miss;
	}

	const float l_Distance = -origin.z / direction.z;
	if (l_Distance <= 0.0)
	{
		return k_Miss;
	}

	const float2 l_Point = origin.xy + direction.xy * l_Distance;
	if (abs(l_Point.x) > 0.5 || abs(l_Point.y) > 0.5)
	{
		return k_Miss;
	}

	return l_Distance;
}

// Closest primitive along the ray, a brute-force loop over every record until Step 13 adds acceleration
Hit TraceClosest(Ray ray)
{
	Hit l_Hit;
	l_Hit.Distance = k_Miss;
	l_Hit.Normal = float3(0.0, 0.0, 0.0);
	l_Hit.MaterialIndex = 0;

	for (uint i_Primitive = 0; i_Primitive < u_Parameters.PrimitiveCount; ++i_Primitive)
	{
		const PrimitiveRecord l_Record = u_Primitives[i_Primitive];

		const float3 l_Origin = TransformPoint(l_Record, ray.Origin);
		const float3 l_Direction = TransformDirection(l_Record, ray.Direction);

		float l_Distance = k_Miss;
		float3 l_ObjectNormal = float3(0.0, 0.0, 1.0);
		if (l_Record.Type == k_PrimitiveSphere)
		{
			l_Distance = IntersectUnitSphere(l_Origin, l_Direction);
			l_ObjectNormal = l_Origin + l_Direction * l_Distance;
		}
		else if (l_Record.Type == k_PrimitiveQuad)
		{
			l_Distance = IntersectUnitQuad(l_Origin, l_Direction);
		}

		if (l_Distance > 0.0 && (l_Hit.Distance < 0.0 || l_Distance < l_Hit.Distance))
		{
			l_Hit.Distance = l_Distance;
			l_Hit.Normal = normalize(TransformNormal(l_Record, l_ObjectNormal));
			l_Hit.MaterialIndex = l_Record.MaterialIndex;
		}
	}

	// Two-sided: the normal always faces the incoming ray
	if (l_Hit.Distance > 0.0 && dot(l_Hit.Normal, ray.Direction) > 0.0)
	{
		l_Hit.Normal = -l_Hit.Normal;
	}

	return l_Hit;
}

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

	const Ray l_Ray = GenerateCameraRay(l_Pixel, l_Extent);
	const uint l_Mode = u_Parameters.Mode;

	float3 l_Color;
	if (l_Mode == k_ModeRayDirection)
	{
		// World-space direction as RGB, the centre pixel of an identity camera is (0.5, 0.5, 0)
		l_Color = l_Ray.Direction * 0.5 + 0.5;
	}
	else if (l_Mode == k_ModeHit || l_Mode == k_ModeNormal || l_Mode == k_ModeDistance || l_Mode == k_ModeBaseColor)
	{
		const Hit l_Hit = TraceClosest(l_Ray);
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