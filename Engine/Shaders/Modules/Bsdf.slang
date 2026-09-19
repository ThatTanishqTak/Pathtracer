// Surface scattering. Every material goes through Evaluate, Pdf and Sample so Step 14 can add materials without touching the integrator
module Bsdf;

import SceneRecords;
import Sampling;

public struct BsdfSample
{
	public float3 Direction; // Unit length, world space
	public float3 Weight; // f * cos(theta) / pdf, what the path throughput is multiplied by
	public float Pdf; // Solid angle density of Direction, zero means the sample is unusable
};

// Lambertian: f = albedo / pi on the hemisphere of the face-forward normal, zero below it
public float3 EvaluateBsdf(MaterialRecord material, float3 normal, float3 outgoing, float3 incoming)
{
	if (dot(normal, incoming) <= 0.0 || dot(normal, outgoing) <= 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}

	return material.BaseColor.xyz * k_InversePi;
}

public float BsdfPdf(MaterialRecord material, float3 normal, float3 outgoing, float3 incoming)
{
	if (dot(normal, outgoing) <= 0.0)
	{
		return 0.0;
	}

	return CosineHemispherePdf(dot(normal, incoming));
}

// Cosine-weighted sampling makes the Lambertian weight reduce to the albedo, the division is kept so the interface stays honest for later materials
public BsdfSample SampleBsdf(MaterialRecord material, float3 normal, float3 outgoing, float2 u)
{
	BsdfSample l_Sample;
	l_Sample.Direction = SampleCosineHemisphere(normal, u);
	l_Sample.Pdf = BsdfPdf(material, normal, outgoing, l_Sample.Direction);
	l_Sample.Weight = float3(0.0, 0.0, 0.0);

	if (l_Sample.Pdf > 0.0)
	{
		const float l_CosTheta = dot(normal, l_Sample.Direction);
		l_Sample.Weight = EvaluateBsdf(material, normal, outgoing, l_Sample.Direction) * l_CosTheta / l_Sample.Pdf;
	}

	return l_Sample;
}