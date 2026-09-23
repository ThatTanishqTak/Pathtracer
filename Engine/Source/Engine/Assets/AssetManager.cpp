#include "Engine/Assets/AssetManager.hpp"

#include "Engine/Assets/MeshGenerators.hpp"
#include "Engine/Assets/MeshImporter.hpp"
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

		// A built-in name is generated, anything else is a glTF path
		Mesh l_Mesh;
		const bool l_Loaded = source.starts_with(k_BuiltinPrefix) ? GenerateBuiltinMesh(source, l_Mesh, l_Error) : ImportMeshFile(source, l_Mesh, l_Error);
		if (!l_Loaded)
		{
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

	bool AssetManager::ImportMeshFile(std::string_view source, Mesh& mesh, std::string& error) const
	{
		// The source is UTF-8, as the scene file is; a relative path lives under the asset root and never under the working directory
		std::filesystem::path l_Path(std::u8string_view(reinterpret_cast<const char8_t*>(source.data()), source.size()));
		if (!l_Path.is_absolute())
		{
			if (m_AssetRoot.empty())
			{
				error = "a relative path needs an asset root and none is set";

				return false;
			}

			l_Path = m_AssetRoot / l_Path;
		}

		if (!ImportGltfMesh(l_Path, mesh, error))
		{
			error = std::format("{} ({})", error, l_Path.string());

			return false;
		}

		return true;
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