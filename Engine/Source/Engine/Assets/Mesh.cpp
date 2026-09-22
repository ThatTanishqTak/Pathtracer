#include "Engine/Assets/Mesh.hpp"

#include <cmath>
#include <format>

namespace Engine
{
	MeshBounds ComputeMeshBounds(const Mesh& mesh)
	{
		MeshBounds l_Bounds;
		if (mesh.Vertices.empty())
		{
			return l_Bounds;
		}

		l_Bounds.Min = mesh.Vertices.front().Position;
		l_Bounds.Max = mesh.Vertices.front().Position;
		for (const MeshVertex& l_Vertex : mesh.Vertices)
		{
			l_Bounds.Min = glm::min(l_Bounds.Min, l_Vertex.Position);
			l_Bounds.Max = glm::max(l_Bounds.Max, l_Vertex.Position);
		}

		return l_Bounds;
	}

	bool ValidateMesh(const Mesh& mesh, std::string& error)
	{
		if (mesh.Vertices.empty() || mesh.Indices.empty())
		{
			error = "the mesh has no triangles";

			return false;
		}

		if (mesh.Indices.size() % 3 != 0)
		{
			error = std::format("{} indices is not a multiple of three", mesh.Indices.size());

			return false;
		}

		for (size_t i_Index = 0; i_Index < mesh.Indices.size(); ++i_Index)
		{
			if (mesh.Indices[i_Index] >= mesh.Vertices.size())
			{
				error = std::format("index {} at position {} is past the {} vertices", mesh.Indices[i_Index], i_Index, mesh.Vertices.size());

				return false;
			}
		}

		for (size_t i_Vertex = 0; i_Vertex < mesh.Vertices.size(); ++i_Vertex)
		{
			const MeshVertex& l_Vertex = mesh.Vertices[i_Vertex];
			if (!Math::IsFinite(l_Vertex.Position) || !Math::IsFinite(l_Vertex.Normal) || !std::isfinite(l_Vertex.TexCoord.x) || !std::isfinite(l_Vertex.TexCoord.y))
			{
				error = std::format("vertex {} is not finite", i_Vertex);

				return false;
			}
		}

		return true;
	}
}