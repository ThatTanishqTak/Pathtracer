#pragma once

#include "Engine/Assets/Mesh.hpp"

#include <cstdint>

namespace Engine
{
	// The built-in meshes, generated in code so triangle intersection, normal interpolation and transforms are proven before any file is imported. Both fit the unit cube like the unit quad does, so a transform scale of one is one metre across

	// Six flat faces, four vertices each so the normals stay per face, twelve triangles, corners at ±0.5
	Mesh GenerateCubeMesh();

	// An icosahedron subdivided the given number of times and pushed onto the sphere of radius 0.5, smooth normals. Zero subdivisions is the icosahedron's 20 triangles, every subdivision multiplies the count by four
	Mesh GenerateIcosphereMesh(uint32_t subdivisions);
}