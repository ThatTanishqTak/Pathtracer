// Random numbers and the hemisphere sampling the integrator draws from
module Sampling;

public static const float k_Pi = 3.14159265358979323846;
public static const float k_InversePi = 0.31830988618379067154;

// One PCG stream per pixel and sample, every draw advances it so each path dimension and bounce reads a different value
public struct RandomState
{
	public uint State;
};

uint HashUint(uint value)
{
	// PCG hash, one round is enough to decorrelate neighbouring pixels and consecutive sample indices
	const uint l_State = value * 747796405u + 2891336453u;
	const uint l_Word = ((l_State >> ((l_State >> 28u) + 4u)) ^ l_State) * 277803737u;

	return (l_Word >> 22u) ^ l_Word;
}

// The stream is a function of the pixel, the sample index and the seed only, so the same frame always produces the same image
public RandomState InitializeRandom(uint2 pixel, uint sampleIndex, uint seed)
{
	RandomState l_State;
	l_State.State = HashUint(pixel.x + HashUint(pixel.y + HashUint(sampleIndex + HashUint(seed))));

	return l_State;
}

public uint NextUint(inout RandomState state)
{
	state.State = state.State * 747796405u + 2891336453u;
	const uint l_Word = ((state.State >> ((state.State >> 28u) + 4u)) ^ state.State) * 277803737u;

	return (l_Word >> 22u) ^ l_Word;
}

// Uniform in [0, 1), the top 24 bits so the float mantissa is filled exactly
public float NextFloat(inout RandomState state)
{
	return float(NextUint(state) >> 8u) * (1.0 / 16777216.0);
}

public float2 NextFloat2(inout RandomState state)
{
	const float l_X = NextFloat(state);
	const float l_Y = NextFloat(state);

	return float2(l_X, l_Y);
}

// Right-handed orthonormal basis around a unit normal without branches, Duff et al. 2017
public void BuildOrthonormalBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
	const float l_Sign = normal.z >= 0.0 ? 1.0 : -1.0;
	const float l_A = -1.0 / (l_Sign + normal.z);
	const float l_B = normal.x * normal.y * l_A;

	tangent = float3(1.0 + l_Sign * normal.x * normal.x * l_A, l_Sign * l_B, -l_Sign * normal.x);
	bitangent = float3(l_B, l_Sign + normal.y * normal.y * l_A, -normal.y);
}

// Cosine-weighted direction on the hemisphere around the normal, Malley's method, pdf = cos(theta) / pi
public float3 SampleCosineHemisphere(float3 normal, float2 u)
{
	const float l_Radius = sqrt(u.x);
	const float l_Phi = 2.0 * k_Pi * u.y;

	const float l_X = l_Radius * cos(l_Phi);
	const float l_Y = l_Radius * sin(l_Phi);
	const float l_Z = sqrt(max(0.0, 1.0 - u.x));

	float3 l_Tangent;
	float3 l_Bitangent;
	BuildOrthonormalBasis(normal, l_Tangent, l_Bitangent);

	return normalize(l_Tangent * l_X + l_Bitangent * l_Y + normal * l_Z);
}

public float CosineHemispherePdf(float cosTheta)
{
	return cosTheta > 0.0 ? cosTheta * k_InversePi : 0.0;
}