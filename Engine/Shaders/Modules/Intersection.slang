// Analytic intersection against the unit shapes in object space, triangle intersection against the uploaded meshes, and the closest-hit loop over the records
module Intersection;

import Camera;
import SceneRecords;

// Negative distances mean a miss everywhere below
public static const float k_Miss = -1.0;

// Hits closer than this are the surface the ray just left, so secondary rays skip them
public static const float k_MinRayDistance = 1e-4;

// Nothing in a scene is this far away, a primary ray can pass it as its far limit
public static const float k_MaxRayDistance = 1e30;

// A direction component below this runs parallel to a slab, a determinant below it a ray parallel to a triangle's plane. Must match ScenePicking.cpp
static const float k_ParallelEpsilon = 1e-12;

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

// Whether the ray overlaps an axis-aligned box somewhere inside (minDistance, maxDistance), the CPU twin is IntersectBounds in ScenePicking.cpp. Each pair of slabs narrows the overlap, a component parallel to its slabs tests the origin instead of dividing by zero
public bool IntersectBounds(float3 origin, float3 direction, float3 boundsMin, float3 boundsMax, float minDistance, float maxDistance)
{
	float l_Enter = minDistance;
	float l_Exit = maxDistance;

	for (int i_Axis = 0; i_Axis < 3; ++i_Axis)
	{
		if (abs(direction[i_Axis]) < k_ParallelEpsilon)
		{
			if (origin[i_Axis] < boundsMin[i_Axis] || origin[i_Axis] > boundsMax[i_Axis])
			{
				return false;
			}

			continue;
		}

		const float l_Inverse = 1.0 / direction[i_Axis];
		const float l_First = (boundsMin[i_Axis] - origin[i_Axis]) * l_Inverse;
		const float l_Second = (boundsMax[i_Axis] - origin[i_Axis]) * l_Inverse;

		l_Enter = max(l_Enter, min(l_First, l_Second));
		l_Exit = min(l_Exit, max(l_First, l_Second));
		if (l_Enter > l_Exit)
		{
			return false;
		}
	}

	return true;
}

// Möller-Trumbore against one object-space triangle, both sides count, the CPU twin is IntersectTriangle in ScenePicking.cpp. Returns the distance beyond minDistance or k_Miss, and on a hit the barycentric weights of the second and third vertex
public float IntersectTriangle(float3 origin, float3 direction, float3 p0, float3 p1, float3 p2, float minDistance, out float2 barycentrics)
{
	barycentrics = float2(0.0, 0.0);

	const float3 l_Edge1 = p1 - p0;
	const float3 l_Edge2 = p2 - p0;
	const float3 l_P = cross(direction, l_Edge2);
	const float l_Determinant = dot(l_Edge1, l_P);
	if (abs(l_Determinant) < k_ParallelEpsilon)
	{
		return k_Miss;
	}

	const float l_InverseDeterminant = 1.0 / l_Determinant;
	const float3 l_T = origin - p0;
	const float l_U = dot(l_T, l_P) * l_InverseDeterminant;
	if (l_U < 0.0 || l_U > 1.0)
	{
		return k_Miss;
	}

	const float3 l_Q = cross(l_T, l_Edge1);
	const float l_V = dot(direction, l_Q) * l_InverseDeterminant;
	if (l_V < 0.0 || l_U + l_V > 1.0)
	{
		return k_Miss;
	}

	const float l_Distance = dot(l_Edge2, l_Q) * l_InverseDeterminant;
	if (l_Distance <= minDistance)
	{
		return k_Miss;
	}

	barycentrics = float2(l_U, l_V);

	return l_Distance;
}

// The closest triangle of one mesh record along an object-space ray, with the object-space shading normal at the hit. The full triangle loop of the prototype, Part B traces the TLAS instead. Only hits nearer than maxDistance count, so the bounds test and the loop prune against the best hit so far. The extraction validated every index
float IntersectMesh(PrimitiveRecord record, float3 origin, float3 direction, StructuredBuffer<VertexRecord> vertices, StructuredBuffer<TriangleRecord> triangles, float minDistance, float maxDistance, out float3 objectNormal)
{
	objectNormal = float3(0.0, 0.0, 1.0);

	if (!IntersectBounds(origin, direction, record.BoundsMin.xyz, record.BoundsMax.xyz, minDistance, maxDistance))
	{
		return k_Miss;
	}

	float l_Closest = k_Miss;
	float l_Limit = maxDistance;

	for (uint i_Triangle = 0; i_Triangle < record.TriangleCount; ++i_Triangle)
	{
		const TriangleRecord l_Triangle = triangles[record.FirstTriangle + i_Triangle];
		const VertexRecord l_V0 = vertices[l_Triangle.V0];
		const VertexRecord l_V1 = vertices[l_Triangle.V1];
		const VertexRecord l_V2 = vertices[l_Triangle.V2];

		float2 l_Barycentrics;
		const float l_Distance = IntersectTriangle(origin, direction, l_V0.PositionU.xyz, l_V1.PositionU.xyz, l_V2.PositionU.xyz, minDistance, l_Barycentrics);
		if (l_Distance > 0.0 && l_Distance < l_Limit)
		{
			l_Closest = l_Distance;
			l_Limit = l_Distance;

			// The shading normal from the vertex normals. Where they cancel, a hard edge with opposing normals, the face's own normal stands in
			float3 l_Normal = l_V0.NormalV.xyz * (1.0 - l_Barycentrics.x - l_Barycentrics.y) + l_V1.NormalV.xyz * l_Barycentrics.x + l_V2.NormalV.xyz * l_Barycentrics.y;
			if (dot(l_Normal, l_Normal) < k_ParallelEpsilon)
			{
				l_Normal = cross(l_V1.PositionU.xyz - l_V0.PositionU.xyz, l_V2.PositionU.xyz - l_V0.PositionU.xyz);
			}

			objectNormal = l_Normal;
		}
	}

	return l_Closest;
}

// Closest primitive along the ray inside (minDistance, maxDistance), a brute-force loop over every record and, inside a mesh's bounds, every triangle, until Part B adds acceleration
public Hit TraceClosest(Ray ray, StructuredBuffer<PrimitiveRecord> primitives, uint primitiveCount, StructuredBuffer<VertexRecord> vertices, StructuredBuffer<TriangleRecord> triangles, float minDistance, float maxDistance)
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

		// Only a hit nearer than the best so far can replace it, and the mesh loop prunes against that limit
		const float l_Limit = l_Hit.Distance > 0.0 ? l_Hit.Distance : maxDistance;

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
		else if (l_Record.Type == k_PrimitiveMesh)
		{
			l_Distance = IntersectMesh(l_Record, l_Origin, l_Direction, vertices, triangles, minDistance, l_Limit, l_ObjectNormal);
		}

		if (l_Distance > 0.0 && l_Distance < l_Limit)
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