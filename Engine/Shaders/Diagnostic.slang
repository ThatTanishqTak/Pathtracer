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
	uint Padding;
};

// Must match Engine::DiagnosticMode
static const uint k_ModeRayDirection = 0;
static const uint k_ModeSphereHit = 1;
static const uint k_ModeSphereNormal = 2;
static const uint k_ModeSphereDistance = 3;

// The one object every mode looks at until Step 6 uploads a scene: a unit sphere at the origin
static const float3 k_SphereCentre = float3(0.0, 0.0, 0.0);
static const float k_SphereRadius = 1.0;

// Distances beyond this render black in the distance mode
static const float k_MaxDisplayDistance = 10.0;

// Binding 0: the linear HDR image the tone map reads, the explicit format matches VK_FORMAT_R32G32B32A32_SFLOAT
[[vk::binding(0, 0)]]
[format("rgba32f")]
RWTexture2D<float4> u_Output;

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

// Nearest positive distance along a unit-direction ray to the sphere surface, negative when the ray misses
float IntersectSphere(Ray ray, float3 centre, float radius)
{
	const float3 l_Offset = ray.Origin - centre;
	const float l_B = dot(l_Offset, ray.Direction);
	const float l_C = dot(l_Offset, l_Offset) - radius * radius;
	const float l_Discriminant = l_B * l_B - l_C;
	if (l_Discriminant < 0.0)
	{
		return -1.0;
	}

	const float l_Root = sqrt(l_Discriminant);

	const float l_Near = -l_B - l_Root;
	if (l_Near > 0.0)
	{
		return l_Near;
	}

	// The origin is inside the sphere, the far intersection is the one in front
	const float l_Far = -l_B + l_Root;

	return l_Far > 0.0 ? l_Far : -1.0;
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
	else if (l_Mode == k_ModeSphereHit || l_Mode == k_ModeSphereNormal || l_Mode == k_ModeSphereDistance)
	{
		const float l_Distance = IntersectSphere(l_Ray, k_SphereCentre, k_SphereRadius);
		if (l_Distance < 0.0)
		{
			l_Color = l_Mode == k_ModeSphereHit ? MissColor(l_Ray.Direction) : float3(0.0, 0.0, 0.0);
		}
		else if (l_Mode == k_ModeSphereHit)
		{
			l_Color = float3(1.0, 1.0, 1.0);
		}
		else if (l_Mode == k_ModeSphereNormal)
		{
			const float3 l_Normal = normalize(l_Ray.Origin + l_Ray.Direction * l_Distance - k_SphereCentre);

			l_Color = l_Normal * 0.5 + 0.5;
		}
		else
		{
			// Brighter is closer, black at and beyond the display range
			l_Color = float3(saturate(1.0 - l_Distance / k_MaxDisplayDistance));
		}
	}
	else
	{
		// An unknown mode is a C++ side bug, magenta makes it visible
		l_Color = float3(1.0, 0.0, 1.0);
	}

	u_Output[l_Pixel] = float4(l_Color, 1.0);
}