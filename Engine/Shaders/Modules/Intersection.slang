// Analytic intersection against the unit shapes in object space, and the closest hit through the scene's TLAS with the mesh normals interpolated at it
module Intersection;

import Camera;
import SceneRecords;

// Negative distances mean a miss everywhere below
public static const float k_Miss = -1.0;

// Hits closer than this are the surface the ray just left, so secondary rays skip them
public static const float k_MinRayDistance = 1e-4;

// Nothing in a scene is this far away, a primary ray can pass it as its far limit
public static const float k_MaxRayDistance = 1e30;

// Interpolated vertex normals shorter than this have cancelled out, the face normal stands in
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

// The shading normal of one mesh triangle at a hit, in object space, from the barycentric weights of its second and third vertex. Where the vertex normals cancel, a hard edge with opposing normals, the face's own normal stands in. The extraction validated every index
float3 InterpolateMeshNormal(TriangleRecord triangle, StructuredBuffer<VertexRecord> vertices, float2 barycentrics)
{
    const VertexRecord l_V0 = vertices[triangle.V0];
    const VertexRecord l_V1 = vertices[triangle.V1];
    const VertexRecord l_V2 = vertices[triangle.V2];

    float3 l_Normal = l_V0.NormalV.xyz * (1.0 - barycentrics.x - barycentrics.y) + l_V1.NormalV.xyz * barycentrics.x + l_V2.NormalV.xyz * barycentrics.y;
    if (dot(l_Normal, l_Normal) < k_ParallelEpsilon)
    {
        l_Normal = cross(l_V1.PositionU.xyz - l_V0.PositionU.xyz, l_V2.PositionU.xyz - l_V0.PositionU.xyz);
    }

    return l_Normal;
}

// Closest primitive along the ray inside (minDistance, maxDistance), traversed through the scene's TLAS. Mesh triangles are opaque and committed by the hardware; a sphere or quad is a procedural box whose candidates run the unit-shape intersector in the record's object space. The instance's custom index is the record index
public Hit TraceClosest(Ray ray, RaytracingAccelerationStructure scene, StructuredBuffer<PrimitiveRecord> primitives, StructuredBuffer<VertexRecord> vertices, StructuredBuffer<TriangleRecord> triangles, float minDistance, float maxDistance)
{
    Hit l_Hit;
    l_Hit.Distance = k_Miss;
    l_Hit.Position = ray.Origin;
    l_Hit.Normal = float3(0.0, 0.0, 0.0);
    l_Hit.MaterialIndex = 0;

    RayDesc l_Ray;
    l_Ray.Origin = ray.Origin;
    l_Ray.Direction = ray.Direction;
    l_Ray.TMin = minDistance;
    l_Ray.TMax = maxDistance;

    RayQuery<RAY_FLAG_NONE> l_Query;
    l_Query.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, l_Ray);

    while (l_Query.Proceed())
    {
        if (l_Query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
        {
            // The same transform the record loop used: the direction is not normalized, so the object-space distance is the world-space one the query compares
            const PrimitiveRecord l_Record = primitives[l_Query.CandidateInstanceID()];
            const float3 l_Origin = TransformPoint(l_Record, ray.Origin);
            const float3 l_Direction = TransformDirection(l_Record, ray.Direction);

            const float l_Distance = l_Record.Type == k_PrimitiveSphere ? IntersectUnitSphere(l_Origin, l_Direction, minDistance) : IntersectUnitQuad(l_Origin, l_Direction, minDistance);

            // Only a hit nearer than the one committed so far replaces it, CommittedRayT is TMax until something commits
            if (l_Distance > 0.0 && l_Distance < l_Query.CommittedRayT())
            {
                l_Query.CommitProceduralPrimitiveHit(l_Distance);
            }
        }
        else
        {
            // Every triangle geometry is built opaque, so this never runs; committing keeps the result right if a geometry ever loses the flag
            l_Query.CommitNonOpaqueTriangleHit();
        }
    }

    const uint l_Status = l_Query.CommittedStatus();
    if (l_Status == COMMITTED_NOTHING)
    {
        return l_Hit;
    }

    const PrimitiveRecord l_Record = primitives[l_Query.CommittedInstanceID()];
    const float l_Distance = l_Query.CommittedRayT();

    float3 l_ObjectNormal = float3(0.0, 0.0, 1.0);
    if (l_Status == COMMITTED_TRIANGLE_HIT)
    {
        // The BLAS geometry starts at the mesh's first triangle, so the primitive index counts from there
        const TriangleRecord l_Triangle = triangles[l_Record.FirstTriangle + l_Query.CommittedPrimitiveIndex()];
        l_ObjectNormal = InterpolateMeshNormal(l_Triangle, vertices, l_Query.CommittedTriangleBarycentrics());
    }
    else if (l_Record.Type == k_PrimitiveSphere)
    {
        l_ObjectNormal = TransformPoint(l_Record, ray.Origin) + TransformDirection(l_Record, ray.Direction) * l_Distance;
    }

    l_Hit.Distance = l_Distance;
    l_Hit.Position = ray.Origin + ray.Direction * l_Distance;
    l_Hit.Normal = normalize(TransformNormal(l_Record, l_ObjectNormal));
    l_Hit.MaterialIndex = l_Record.MaterialIndex;

    // Two-sided: the normal always faces the incoming ray
    if (dot(l_Hit.Normal, ray.Direction) > 0.0)
    {
        l_Hit.Normal = -l_Hit.Normal;
    }

    return l_Hit;
}

// Secondary rays start a little off the surface along the face-forward normal, scaled with the magnitude of the position so far-away surfaces get a proportionally larger step
public float3 OffsetRayOrigin(float3 position, float3 normal)
{
	const float l_Magnitude = max(1.0, max(abs(position.x), max(abs(position.y), abs(position.z))));

	return position + normal * (k_MinRayDistance * l_Magnitude);
}