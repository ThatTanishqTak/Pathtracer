// The packed scene records the renderer uploads, and the object-space transform helpers that read them
module SceneRecords;

// Must match Engine::RenderPrimitiveRecord. The matrices are four columns each, so no matrix layout convention is involved
public struct PrimitiveRecord
{
    public float4 ObjectToWorld[4]; // world = C0 * x + C1 * y + C2 * z + C3
    public float4 WorldToObject[4];
    public uint Type;
    public uint MaterialIndex;
    public uint EntityIdLow;
    public uint EntityIdHigh;
    public uint FirstTriangle; // Mesh only: the range in the triangle buffer, where the BLAS's primitive indices start, zero triangles for the analytic shapes
    public uint TriangleCount;
    public uint MeshIndex; // Mesh only: which BLAS the instance references, the renderer's bookkeeping, unread here
    public uint Padding1;
    public float4 BoundsMin; // Mesh only: object-space bounds, CPU picking tests them, the BLAS replaced them here
    public float4 BoundsMax;
};

// Must match Engine::RenderVertexRecord. Object space, the texture coordinate in the two w components
public struct VertexRecord
{
    public float4 PositionU;
    public float4 NormalV;
};

// Must match Engine::RenderTriangleRecord, indices into the vertex buffer
public struct TriangleRecord
{
    public uint V0;
    public uint V1;
    public uint V2;
    public uint Padding0;
};

// Must match Engine::RenderMaterialRecord
public struct MaterialRecord
{
	public float4 BaseColor; // Linear RGB, w unused
	public float4 EmittedRadiance; // Linear RGB, w unused
	public uint Type;
	public uint Padding0;
	public uint Padding1;
	public uint Padding2;
};

// Must match Engine::RenderPrimitiveType
public static const uint k_PrimitiveSphere = 0;
public static const uint k_PrimitiveQuad = 1;
public static const uint k_PrimitiveMesh = 2;

// Must match Engine::MaterialType
public static const uint k_MaterialDiffuse = 0;
public static const uint k_MaterialEmissive = 1;

public float3 TransformPoint(PrimitiveRecord record, float3 point)
{
	return record.WorldToObject[0].xyz * point.x + record.WorldToObject[1].xyz * point.y + record.WorldToObject[2].xyz * point.z + record.WorldToObject[3].xyz;
}

// Not normalized on purpose: with the direction scaled by the same matrix as the origin, an object-space distance is also the world-space distance
public float3 TransformDirection(PrimitiveRecord record, float3 direction)
{
	return record.WorldToObject[0].xyz * direction.x + record.WorldToObject[1].xyz * direction.y + record.WorldToObject[2].xyz * direction.z;
}

// Normals transform by the inverse transpose of ObjectToWorld, which is the transpose of WorldToObject: each component is a dot with one column
public float3 TransformNormal(PrimitiveRecord record, float3 normal)
{
	return float3(dot(record.WorldToObject[0].xyz, normal), dot(record.WorldToObject[1].xyz, normal), dot(record.WorldToObject[2].xyz, normal));
}