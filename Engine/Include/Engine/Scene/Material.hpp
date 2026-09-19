#pragma once

#include "Engine/Math/Math.hpp"

#include <cstdint>
#include <string>

namespace Engine
{
	// Stable material identity, unique inside one scene for its whole life. Never an index into a packed GPU array
	enum class MaterialId : uint64_t
	{
		Invalid = 0,
	};

	// Values match the constants in Modules/SceneRecords.slang
	enum class MaterialType : uint8_t
	{
		Diffuse = 0, // Lambertian, BaseColor is the albedo
		Emissive = 1, // Emits EmissionColor * EmissionStrength, BaseColor is still reflected
	};

	// Area lights are emissive geometry: an entity whose material emits is a light, there is no separate light component
	struct Material
	{
		MaterialId Id = MaterialId::Invalid;
		std::string Name;

		MaterialType Type = MaterialType::Diffuse;

		Math::Vector3 BaseColor{ 0.8f, 0.8f, 0.8f }; // Linear, [0, 1]
		Math::Vector3 EmissionColor{ 1.0f, 1.0f, 1.0f }; // Linear tint of the emitted radiance
		float EmissionStrength = 0.0f; // Radiance scale, zero emits nothing
	};

	// The one source of truth for emitted radiance, in linear radiometric units
	inline Math::Vector3 GetEmittedRadiance(const Material& material)
	{
		return material.EmissionColor * material.EmissionStrength;
	}
}