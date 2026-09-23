#include "Engine/Renderer/RenderScene.hpp"

#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Core/Log.hpp"

#include <cmath>
#include <unordered_map>
#include <utility>

namespace Engine
{
	namespace
	{
		// A scale below this has no usable inverse, the object would be invisible or blow up the normal transform
		constexpr float k_MinimumScale = 1e-6f;

		std::array<float, 16> ToColumns(const Math::Matrix4& matrix)
		{
			std::array<float, 16> l_Columns{};
			for (int i_Column = 0; i_Column < 4; ++i_Column)
			{
				for (int i_Row = 0; i_Row < 4; ++i_Row)
				{
					l_Columns[static_cast<size_t>(i_Column * 4 + i_Row)] = matrix[i_Column][i_Row];
				}
			}

			return l_Columns;
		}

		std::array<float, 4> ToFloat4(const Math::Vector3& value, float w = 0.0f)
		{
			return { value.x, value.y, value.z, w };
		}

		bool IsFinite(const Math::Matrix4& matrix)
		{
			for (int i_Column = 0; i_Column < 4; ++i_Column)
			{
				for (int i_Row = 0; i_Row < 4; ++i_Row)
				{
					if (!std::isfinite(matrix[i_Column][i_Row]))
					{
						return false;
					}
				}
			}

			return true;
		}

		RenderMaterialRecord ToRecord(const Material& material)
		{
			return RenderMaterialRecord
			{
				.BaseColor = ToFloat4(material.BaseColor),
				.EmittedRadiance = ToFloat4(GetEmittedRadiance(material)),
				.Type = static_cast<uint32_t>(material.Type),
			};
		}

		// The shared geometry scale folds the parameters into the object-space scale so the shader always intersects a unit shape, the record type is the renderer's own
		bool GetPrimitiveScale(const Entity& entity, Math::Vector3& scale, RenderPrimitiveType& type)
		{
			if (!GetGeometryScale(entity.Geometry, scale))
			{
				return false;
			}

			switch (entity.Geometry.Type)
			{
				case GeometryType::Quad:
				{
					type = RenderPrimitiveType::Quad;
					break;
				}
				case GeometryType::Mesh:
				{
					type = RenderPrimitiveType::Mesh;
					break;
				}
				default:
				{
					type = RenderPrimitiveType::Sphere;
					break;
				}
			}

			return true;
		}

		// Where one mesh landed in the packed arrays, so every entity that shares it points at the same triangles and the same BLAS
		struct MeshRange
		{
			uint32_t MeshIndex = 0; // Into RenderScene::Meshes
			uint32_t FirstTriangle = 0;
			uint32_t TriangleCount = 0;
			MeshBounds Bounds;
		};

		// Appends the mesh once. The AssetManager validated it, so the indices are trusted here and in the shader
		MeshRange AppendMesh(const Mesh& mesh, RenderScene& renderScene)
		{
			const uint32_t l_FirstVertex = static_cast<uint32_t>(renderScene.Vertices.size());
			for (const MeshVertex& l_Vertex : mesh.Vertices)
			{
				renderScene.Vertices.push_back(RenderVertexRecord
					{
						.PositionU = ToFloat4(l_Vertex.Position, l_Vertex.TexCoord.x),
						.NormalV = ToFloat4(l_Vertex.Normal, l_Vertex.TexCoord.y),
					});
			}

			const MeshRange l_Range
			{
				.MeshIndex = static_cast<uint32_t>(renderScene.Meshes.size()),
				.FirstTriangle = static_cast<uint32_t>(renderScene.Triangles.size()),
				.TriangleCount = mesh.GetTriangleCount(),
				.Bounds = mesh.Bounds,
			};

			for (size_t i_Index = 0; i_Index + 2 < mesh.Indices.size(); i_Index += 3)
			{
				const RenderTriangleRecord l_Triangle
				{
					.V0 = l_FirstVertex + mesh.Indices[i_Index],
					.V1 = l_FirstVertex + mesh.Indices[i_Index + 1],
					.V2 = l_FirstVertex + mesh.Indices[i_Index + 2],
				};

				renderScene.Triangles.push_back(l_Triangle);
				renderScene.Indices.insert(renderScene.Indices.end(), { l_Triangle.V0, l_Triangle.V1, l_Triangle.V2 });
			}

			renderScene.Meshes.push_back(RenderMeshRange
				{
					.Id = mesh.Id,
					.FirstTriangle = l_Range.FirstTriangle,
					.TriangleCount = l_Range.TriangleCount,
				});

			return l_Range;
		}
	}

	void BuildRenderScene(const Scene& scene, const AssetManager* assets, RenderScene& renderScene)
	{
		renderScene.Primitives.clear();
		renderScene.Materials.clear();
		renderScene.Vertices.clear();
		renderScene.Triangles.clear();
		renderScene.Indices.clear();
		renderScene.Meshes.clear();

		// Index 0 is the fallback, so an entity without a material or with a dangling reference renders grey instead of reading past the array
		renderScene.Materials.push_back(ToRecord(Material{}));

		std::unordered_map<MaterialId, uint32_t> l_MaterialIndices;
		for (const Material& l_Material : scene.GetMaterials())
		{
			l_MaterialIndices.emplace(l_Material.Id, static_cast<uint32_t>(renderScene.Materials.size()));
			renderScene.Materials.push_back(ToRecord(l_Material));
		}

		std::unordered_map<MeshId, MeshRange> l_MeshRanges;

		uint32_t l_Skipped = 0;
		for (const Entity& l_Entity : scene.GetEntities())
		{
			if (!l_Entity.Visible || l_Entity.Geometry.Type == GeometryType::None)
			{
				continue;
			}

			Math::Vector3 l_GeometryScale{};
			RenderPrimitiveType l_Type = RenderPrimitiveType::Sphere;
			if (!GetPrimitiveScale(l_Entity, l_GeometryScale, l_Type))
			{
				PT_CORE_WARN("Entity '{}' ({}) has non-finite geometry parameters and is not rendered", l_Entity.Name, std::to_underlying(l_Entity.Id));
				l_Skipped += 1;

				continue;
			}

			// A mesh entity needs its mesh: appended on the first entity that uses it, found again for every other
			MeshRange l_MeshRange;
			if (l_Type == RenderPrimitiveType::Mesh)
			{
				const auto l_Found = l_MeshRanges.find(l_Entity.Geometry.Mesh);
				if (l_Found != l_MeshRanges.end())
				{
					l_MeshRange = l_Found->second;
				}
				else
				{
					const Mesh* l_Mesh = assets != nullptr ? assets->FindMesh(l_Entity.Geometry.Mesh) : nullptr;
					if (l_Mesh == nullptr)
					{
						PT_CORE_WARN("Entity '{}' ({}) references mesh {} which is not loaded and is not rendered", l_Entity.Name, std::to_underlying(l_Entity.Id), std::to_underlying(l_Entity.Geometry.Mesh));
						l_Skipped += 1;

						continue;
					}

					l_MeshRange = AppendMesh(*l_Mesh, renderScene);
					l_MeshRanges.emplace(l_Entity.Geometry.Mesh, l_MeshRange);
				}
			}

			const Math::Vector3 l_TotalScale = l_Entity.Transform.Scale * l_GeometryScale;
			if (!Math::IsFinite(l_Entity.Transform.Translation) || !Math::IsFinite(l_TotalScale) || std::abs(l_TotalScale.x) < k_MinimumScale || std::abs(l_TotalScale.y) < k_MinimumScale || std::abs(l_TotalScale.z) < k_MinimumScale)
			{
				PT_CORE_WARN("Entity '{}' ({}) has a degenerate transform and is not rendered", l_Entity.Name, std::to_underlying(l_Entity.Id));
				l_Skipped += 1;

				continue;
			}

			const Math::Matrix4 l_ObjectToWorld = GetLocalToWorld(l_Entity.Transform) * glm::scale(Math::Matrix4(1.0f), l_GeometryScale);
			const Math::Matrix4 l_WorldToObject = glm::inverse(l_ObjectToWorld);
			if (!IsFinite(l_ObjectToWorld) || !IsFinite(l_WorldToObject))
			{
				PT_CORE_WARN("Entity '{}' ({}) has a non-invertible transform and is not rendered", l_Entity.Name, std::to_underlying(l_Entity.Id));
				l_Skipped += 1;

				continue;
			}

			uint32_t l_MaterialIndex = 0;
			if (l_Entity.Material != MaterialId::Invalid)
			{
				const auto l_Found = l_MaterialIndices.find(l_Entity.Material);
				if (l_Found != l_MaterialIndices.end())
				{
					l_MaterialIndex = l_Found->second;
				}
				else
				{
					PT_CORE_WARN("Entity '{}' ({}) references missing material {}, using the fallback material", l_Entity.Name, std::to_underlying(l_Entity.Id), std::to_underlying(l_Entity.Material));
				}
			}

			const uint64_t l_EntityId = std::to_underlying(l_Entity.Id);

			renderScene.Primitives.push_back(RenderPrimitiveRecord
				{
					.ObjectToWorld = ToColumns(l_ObjectToWorld),
					.WorldToObject = ToColumns(l_WorldToObject),
					.Type = static_cast<uint32_t>(l_Type),
					.MaterialIndex = l_MaterialIndex,
					.EntityIdLow = static_cast<uint32_t>(l_EntityId & 0xFFFFFFFFu),
					.EntityIdHigh = static_cast<uint32_t>(l_EntityId >> 32),
					.FirstTriangle = l_MeshRange.FirstTriangle,
					.TriangleCount = l_MeshRange.TriangleCount,
					.MeshIndex = l_MeshRange.MeshIndex,
					.BoundsMin = ToFloat4(l_MeshRange.Bounds.Min),
					.BoundsMax = ToFloat4(l_MeshRange.Bounds.Max),
				});
		}

		renderScene.EnvironmentRadiance = scene.GetEnvironment().Radiance;
		renderScene.Revision = scene.GetRadianceRevision();

		PT_CORE_TRACE("Render scene built for revision {}: {} primitives, {} materials, {} meshes with {} vertices and {} triangles, {} entities skipped", renderScene.Revision, renderScene.Primitives.size(), renderScene.Materials.size(), l_MeshRanges.size(), renderScene.Vertices.size(), renderScene.Triangles.size(), l_Skipped);
	}
}