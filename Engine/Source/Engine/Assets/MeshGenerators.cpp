#include "Engine/Assets/MeshGenerators.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <utility>

namespace Engine
{
	namespace
	{
		constexpr float k_HalfExtent = 0.5f;

		// Every triangle wound counter-clockwise seen from outside, tested against the direction from the origin, which is inside both built-in shapes. The generators produce that winding already, this makes it a checked property rather than a convention to remember
		void EnsureOutwardWinding(Mesh& mesh)
		{
			for (size_t i_Triangle = 0; i_Triangle + 2 < mesh.Indices.size(); i_Triangle += 3)
			{
				const Math::Vector3& l_P0 = mesh.Vertices[mesh.Indices[i_Triangle]].Position;
				const Math::Vector3& l_P1 = mesh.Vertices[mesh.Indices[i_Triangle + 1]].Position;
				const Math::Vector3& l_P2 = mesh.Vertices[mesh.Indices[i_Triangle + 2]].Position;

				const Math::Vector3 l_Centroid = (l_P0 + l_P1 + l_P2) / 3.0f;
				if (glm::dot(glm::cross(l_P1 - l_P0, l_P2 - l_P0), l_Centroid) < 0.0f)
				{
					std::swap(mesh.Indices[i_Triangle + 1], mesh.Indices[i_Triangle + 2]);
				}
			}
		}
	}

	Mesh GenerateCubeMesh()
	{
		Mesh l_Mesh;
		l_Mesh.Name = "Cube";

		// Per face: the outward normal and two in-plane axes with cross(U, V) = Normal, so the corners -U-V, +U-V, +U+V, -U+V go counter-clockwise seen from outside
		struct Face
		{
			Math::Vector3 Normal;
			Math::Vector3 U;
			Math::Vector3 V;
		};

		constexpr std::array<Face, 6> k_Faces
		{ {
			{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		} };

		constexpr std::array<Math::Vector2, 4> k_Corners
		{
			Math::Vector2(-1.0f, -1.0f),
			Math::Vector2(1.0f, -1.0f),
			Math::Vector2(1.0f, 1.0f),
			Math::Vector2(-1.0f, 1.0f),
		};

		l_Mesh.Vertices.reserve(k_Faces.size() * k_Corners.size());
		l_Mesh.Indices.reserve(k_Faces.size() * 6);

		for (const Face& l_Face : k_Faces)
		{
			const uint32_t l_First = static_cast<uint32_t>(l_Mesh.Vertices.size());

			for (const Math::Vector2& l_Corner : k_Corners)
			{
				MeshVertex l_Vertex;
				l_Vertex.Position = (l_Face.Normal + l_Face.U * l_Corner.x + l_Face.V * l_Corner.y) * k_HalfExtent;
				l_Vertex.Normal = l_Face.Normal;
				l_Vertex.TexCoord = l_Corner * 0.5f + 0.5f;

				l_Mesh.Vertices.push_back(l_Vertex);
			}

			l_Mesh.Indices.insert(l_Mesh.Indices.end(), { l_First, l_First + 1, l_First + 2, l_First, l_First + 2, l_First + 3 });
		}

		EnsureOutwardWinding(l_Mesh);
		l_Mesh.Bounds = ComputeMeshBounds(l_Mesh);

		return l_Mesh;
	}

	Mesh GenerateIcosphereMesh(uint32_t subdivisions)
	{
		Mesh l_Mesh;
		l_Mesh.Name = "Icosphere";

		// The icosahedron's twelve vertices lie on three orthogonal golden rectangles
		const float l_T = (1.0f + std::sqrt(5.0f)) * 0.5f;

		const std::array<Math::Vector3, 12> k_Positions
		{
			Math::Vector3(-1.0f, l_T, 0.0f), Math::Vector3(1.0f, l_T, 0.0f), Math::Vector3(-1.0f, -l_T, 0.0f), Math::Vector3(1.0f, -l_T, 0.0f),
			Math::Vector3(0.0f, -1.0f, l_T), Math::Vector3(0.0f, 1.0f, l_T), Math::Vector3(0.0f, -1.0f, -l_T), Math::Vector3(0.0f, 1.0f, -l_T),
			Math::Vector3(l_T, 0.0f, -1.0f), Math::Vector3(l_T, 0.0f, 1.0f), Math::Vector3(-l_T, 0.0f, -1.0f), Math::Vector3(-l_T, 0.0f, 1.0f),
		};

		constexpr std::array<uint32_t, 60> k_Indices
		{
			0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
			1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
			4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
		};

		// A vertex on the sphere: the position is the direction times the radius, and on a sphere the normal is that direction. The equirectangular texture coordinate is the one an import would carry, a seam and all
		const auto l_MakeVertex = [](const Math::Vector3& direction)
		{
			const Math::Vector3 l_Direction = glm::normalize(direction);

			MeshVertex l_Vertex;
			l_Vertex.Position = l_Direction * k_HalfExtent;
			l_Vertex.Normal = l_Direction;
			l_Vertex.TexCoord = Math::Vector2(0.5f + std::atan2(l_Direction.z, l_Direction.x) / Math::k_TwoPi, 0.5f - std::asin(glm::clamp(l_Direction.y, -1.0f, 1.0f)) / Math::k_Pi);

			return l_Vertex;
		};

		for (const Math::Vector3& l_Position : k_Positions)
		{
			l_Mesh.Vertices.push_back(l_MakeVertex(l_Position));
		}

		l_Mesh.Indices.assign(k_Indices.begin(), k_Indices.end());

		// Each pass splits every triangle into four through its edge midpoints. The midpoint cache is keyed on the ordered vertex pair so a shared edge gets one vertex and the surface stays closed
		for (uint32_t i_Pass = 0; i_Pass < subdivisions; ++i_Pass)
		{
			std::map<std::pair<uint32_t, uint32_t>, uint32_t> l_Midpoints;

			const auto l_Midpoint = [&](uint32_t a, uint32_t b)
			{
				const std::pair<uint32_t, uint32_t> l_Key = a < b ? std::pair(a, b) : std::pair(b, a);

				const auto l_Found = l_Midpoints.find(l_Key);
				if (l_Found != l_Midpoints.end())
				{
					return l_Found->second;
				}

				const uint32_t l_Index = static_cast<uint32_t>(l_Mesh.Vertices.size());
				l_Mesh.Vertices.push_back(l_MakeVertex(l_Mesh.Vertices[a].Position + l_Mesh.Vertices[b].Position));
				l_Midpoints.emplace(l_Key, l_Index);

				return l_Index;
			};

			std::vector<uint32_t> l_Refined;
			l_Refined.reserve(l_Mesh.Indices.size() * 4);

			for (size_t i_Triangle = 0; i_Triangle + 2 < l_Mesh.Indices.size(); i_Triangle += 3)
			{
				const uint32_t l_A = l_Mesh.Indices[i_Triangle];
				const uint32_t l_B = l_Mesh.Indices[i_Triangle + 1];
				const uint32_t l_C = l_Mesh.Indices[i_Triangle + 2];

				const uint32_t l_AB = l_Midpoint(l_A, l_B);
				const uint32_t l_BC = l_Midpoint(l_B, l_C);
				const uint32_t l_CA = l_Midpoint(l_C, l_A);

				l_Refined.insert(l_Refined.end(), { l_A, l_AB, l_CA, l_B, l_BC, l_AB, l_C, l_CA, l_BC, l_AB, l_BC, l_CA });
			}

			l_Mesh.Indices = std::move(l_Refined);
		}

		EnsureOutwardWinding(l_Mesh);
		l_Mesh.Bounds = ComputeMeshBounds(l_Mesh);

		return l_Mesh;
	}
}