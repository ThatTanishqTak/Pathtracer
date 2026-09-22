#include "Engine/Assets/AssetManager.hpp"

#include "Engine/Assets/MeshGenerators.hpp"
#include "Engine/Core/Log.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace Engine
{
	namespace
	{
		// Enough facets for a round silhouette at the sizes the scenes use, few enough that the prototype's full triangle loop stays interactive: 320 triangles
		constexpr uint32_t k_IcosphereSubdivisions = 2;
	}

	MeshId AssetManager::LoadMesh(std::string_view source, std::string* error)
	{
		std::string l_Error;

		const MeshId l_Existing = FindMesh(source);
		if (l_Existing != MeshId::Invalid)
		{
			return l_Existing;
		}

		Mesh l_Mesh;
		if (source.starts_with(k_BuiltinPrefix))
		{
			if (!GenerateBuiltinMesh(source, l_Mesh, l_Error))
			{
				if (error != nullptr)
				{
					*error = l_Error;
				}

				PT_CORE_ERROR("Cannot load mesh '{}': {}", source, l_Error);

				return MeshId::Invalid;
			}
		}
		else
		{
			// The glTF importer is the next part of this step, until then a file source is refused by name rather than read as nothing
			l_Error = "file sources are not supported yet, only the built-in meshes are";

			if (error != nullptr)
			{
				*error = l_Error;
			}

			PT_CORE_ERROR("Cannot load mesh '{}': {}", source, l_Error);

			return MeshId::Invalid;
		}

		l_Mesh.Source = std::string(source);

		const MeshId l_Id = AddMesh(std::move(l_Mesh), l_Error);
		if (l_Id == MeshId::Invalid)
		{
			if (error != nullptr)
			{
				*error = l_Error;
			}

			PT_CORE_ERROR("Cannot load mesh '{}': {}", source, l_Error);
		}

		return l_Id;
	}

	MeshId AssetManager::FindMesh(std::string_view source) const
	{
		const auto l_Found = m_MeshIds.find(std::string(source));

		return l_Found != m_MeshIds.end() ? l_Found->second : MeshId::Invalid;
	}

	const Mesh* AssetManager::FindMesh(MeshId id) const
	{
		const auto l_Found = std::find_if(m_Meshes.begin(), m_Meshes.end(), [id](const Mesh& mesh) { return mesh.Id == id; });

		return l_Found != m_Meshes.end() ? &*l_Found : nullptr;
	}

	bool AssetManager::GenerateBuiltinMesh(std::string_view source, Mesh& mesh, std::string& error) const
	{
		if (source == k_CubeSource)
		{
			mesh = GenerateCubeMesh();

			return true;
		}

		if (source == k_IcosphereSource)
		{
			mesh = GenerateIcosphereMesh(k_IcosphereSubdivisions);

			return true;
		}

		error = std::format("unknown built-in mesh, expected \"{}\" or \"{}\"", k_CubeSource, k_IcosphereSource);

		return false;
	}

	MeshId AssetManager::AddMesh(Mesh mesh, std::string& error)
	{
		// Validated once here, so the extraction and the picking loop index the arrays without checking again
		if (!ValidateMesh(mesh, error))
		{
			return MeshId::Invalid;
		}

		mesh.Id = static_cast<MeshId>(m_NextMeshId++);
		mesh.Bounds = ComputeMeshBounds(mesh);

		const MeshId l_Id = mesh.Id;
		m_MeshIds.emplace(mesh.Source, l_Id);

		const Mesh& l_Added = m_Meshes.emplace_back(std::move(mesh));
		m_Revision += 1;

		PT_CORE_TRACE("Mesh '{}' ({}) loaded from {}: {} vertices, {} triangles, bounds ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})", l_Added.Name, std::to_underlying(l_Id), l_Added.Source, l_Added.Vertices.size(), l_Added.GetTriangleCount(), l_Added.Bounds.Min.x, l_Added.Bounds.Min.y, l_Added.Bounds.Min.z, l_Added.Bounds.Max.x, l_Added.Bounds.Max.y, l_Added.Bounds.Max.z);

		return l_Id;
	}
}