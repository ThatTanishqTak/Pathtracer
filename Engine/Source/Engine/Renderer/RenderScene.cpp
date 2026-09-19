#include "Engine/Renderer/RenderScene.hpp"

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

		std::array<float, 4> ToFloat4(const Math::Vector3& value)
		{
			return { value.x, value.y, value.z, 0.0f };
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

		// Folds the geometry parameters into the object-space scale so the shader always intersects a unit shape
		bool GetGeometryScale(const Entity& entity, Math::Vector3& scale, RenderPrimitiveType& type)
		{
			switch (entity.Geometry.Type)
			{
				case GeometryType::Sphere:
				{
					type = RenderPrimitiveType::Sphere;
					scale = Math::Vector3(entity.Geometry.Radius);

					return std::isfinite(entity.Geometry.Radius);
				}
				case GeometryType::Quad:
				{
					type = RenderPrimitiveType::Quad;
					scale = Math::Vector3(entity.Geometry.Width, entity.Geometry.Height, 1.0f);

					return std::isfinite(entity.Geometry.Width) && std::isfinite(entity.Geometry.Height);
				}
				default:
				{
					return false;
				}
			}
		}
	}

	void BuildRenderScene(const Scene& scene, RenderScene& renderScene)
	{
		renderScene.Primitives.clear();
		renderScene.Materials.clear();

		// Index 0 is the fallback, so an entity without a material or with a dangling reference renders grey instead of reading past the array
		renderScene.Materials.push_back(ToRecord(Material{}));

		std::unordered_map<MaterialId, uint32_t> l_MaterialIndices;
		for (const Material& l_Material : scene.GetMaterials())
		{
			l_MaterialIndices.emplace(l_Material.Id, static_cast<uint32_t>(renderScene.Materials.size()));
			renderScene.Materials.push_back(ToRecord(l_Material));
		}

		uint32_t l_Skipped = 0;
		for (const Entity& l_Entity : scene.GetEntities())
		{
			if (!l_Entity.Visible || l_Entity.Geometry.Type == GeometryType::None)
			{
				continue;
			}

			Math::Vector3 l_GeometryScale{};
			RenderPrimitiveType l_Type = RenderPrimitiveType::Sphere;
			if (!GetGeometryScale(l_Entity, l_GeometryScale, l_Type))
			{
				PT_CORE_WARN("Entity '{}' ({}) has non-finite geometry parameters and is not rendered", l_Entity.Name, std::to_underlying(l_Entity.Id));
				l_Skipped += 1;

				continue;
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
				});
		}

		renderScene.EnvironmentRadiance = scene.GetEnvironment().Radiance;
		renderScene.Revision = scene.GetRadianceRevision();

		PT_CORE_TRACE("Render scene built for revision {}: {} primitives, {} materials, {} entities skipped", renderScene.Revision, renderScene.Primitives.size(), renderScene.Materials.size(), l_Skipped);
	}
}