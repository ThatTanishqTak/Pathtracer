// Analytic intersection against the unit shapes in object space and the closest-hit loop over the uploaded records
module Intersection;

import Camera;
import SceneRecords;

// Negative distances mean a miss everywhere below
public static const float k_Miss = -1.0;

// Hits closer than this are the surface the ray just left, so secondary rays skip them
public static const float k_MinRayDistance = 1e-4;

// Nothing in a scene is this far away, a primary ray can pass it as its far limit
public static const float k_MaxRayDistance = 1e30;

public struct Hit
{
	public float Distance; // Along the world ray, k_Miss when nothing was hit
	public float3 Position; // World space
	public float3 Normal; // Unit length, world space, facing the ray
	public uint MaterialIndex;
};

// Nearest distance beyond minDistance along an object-space ray to the unit sphere at the origin, the direction need not be unit length
public float IntersectUnitSphere(float3 origin, float3 direction, float minDistance)
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
	if (l_Near > minDistance)
	{
		return l_Near;
	}

	// The origin is inside the sphere or sits on its surface, the far intersection is the one in front
	const float l_Far = (-l_B + l_Root) / l_A;

	return l_Far > minDistance ? l_Far : k_Miss;
}

// Distance beyond minDistance to the unit square in the object XY plane, both sides count
public float IntersectUnitQuad(float3 origin, float3 direction, float minDistance)
{
	if (abs(direction.z) < 1e-8)
	{
		return k_Miss;
	}

	const float l_Distance = -origin.z / direction.z;
	if (l_Distance <= minDistance)
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

// Closest primitive along the ray inside (minDistance, maxDistance), a brute-force loop over every record until Step 13 adds acceleration
public Hit TraceClosest(Ray ray, StructuredBuffer<PrimitiveRecord> primitives, uint primitiveCount, float minDistance, float maxDistance)
{
	Hit l_Hit;
	l_Hit.Distance = k_Miss;
	l_Hit.Position = ray.Origin;
	l_Hit.Normal = float3(0.0, 0.0, 0.0);
	l_Hit.MaterialIndex = 0;

	for (uint i_Primitive = 0; i_Primitive < primitiveCount; ++i_Primitive)
	{
		const PrimitiveRecord l_Record = primitives[i_Primitive];

		const float3 l_Origin = TransformPoint(l_Record, ray.Origin);
		const float3 l_Direction = TransformDirection(l_Record, ray.Direction);

		float l_Distance = k_Miss;
		float3 l_ObjectNormal = float3(0.0, 0.0, 1.0);
		if (l_Record.Type == k_PrimitiveSphere)
		{
			l_Distance = IntersectUnitSphere(l_Origin, l_Direction, minDistance);
			l_ObjectNormal = l_Origin + l_Direction * l_Distance;
		}
		else if (l_Record.Type == k_PrimitiveQuad)
		{
			l_Distance = IntersectUnitQuad(l_Origin, l_Direction, minDistance);
		}

		if (l_Distance > 0.0 && l_Distance < maxDistance && (l_Hit.Distance < 0.0 || l_Distance < l_Hit.Distance))
		{
			l_Hit.Distance = l_Distance;
			l_Hit.Normal = normalize(TransformNormal(l_Record, l_ObjectNormal));
			l_Hit.MaterialIndex = l_Record.MaterialIndex;
		}
	}

	if (l_Hit.Distance > 0.0)
	{
		l_Hit.Position = ray.Origin + ray.Direction * l_Hit.Distance;

		// Two-sided: the normal always faces the incoming ray
		if (dot(l_Hit.Normal, ray.Direction) > 0.0)
		{
			l_Hit.Normal = -l_Hit.Normal;
		}
	}

	return l_Hit;
}

// Secondary rays start a little off the surface along the face-forward normal, scaled with the magnitude of the position so far-away surfaces get a proportionally larger step
public float3 OffsetRayOrigin(float3 position, float3 normal)
{
	const float l_Magnitude = max(1.0, max(abs(position.x), max(abs(position.y), abs(position.z))));

	return position + normal * (k_MinRayDistance * l_Magnitude);
}