#include "Engine/Scene/SceneSerializer.hpp"

#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Scene/Camera.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace Engine
{
	namespace
	{
		// Keys keep the order they were inserted in, so a saved file reads top down the way the schema comment lists it
		using Json = nlohmann::ordered_json;

		// Thrown by the readers below and caught once at the Parse boundary, so every reader names its field without threading a result through
		class SceneFormatError final : public std::runtime_error
		{
		public:
			using std::runtime_error::runtime_error;
		};

		// A quaternion within float rounding of unit length is kept bit for bit, so a saved rotation loads back exactly. Further off it is normalized, and beyond the second tolerance that is worth a warning: float printing round trips exactly, so drift means a hand edit
		constexpr float k_ExactUnitTolerance = 1e-6f;
		constexpr float k_UnitTolerance = 1e-3f;

		// A spawn whose orientation differs from its own heading by more than this carries roll the controller will drop
		constexpr float k_RollTolerance = 1e-3f;

		constexpr float k_MinimumQuaternionLength = 1e-6f;

		[[noreturn]] void Fail(std::string_view context, std::string_view message)
		{
			throw SceneFormatError(std::format("{}: {}", context, message));
		}

		// Writers --------------

		// A float is written as the shortest decimal that reads back as the same float, so the file says 0.05 and not 0.05000000074505806. The reader narrows the parsed double to a float, which lands on the same value
		double ToJsonNumber(float value)
		{
			std::array<char, 32> l_Buffer{};
			const std::to_chars_result l_Written = std::to_chars(l_Buffer.data(), l_Buffer.data() + l_Buffer.size(), value);

			double l_Number = static_cast<double>(value);
			if (l_Written.ec == std::errc{})
			{
				std::from_chars(l_Buffer.data(), l_Written.ptr, l_Number);
			}

			return l_Number;
		}

		Json ToJson(const Math::Vector3& value)
		{
			return Json::array({ ToJsonNumber(value.x), ToJsonNumber(value.y), ToJsonNumber(value.z) });
		}

		Json ToJson(const Math::Quaternion& value)
		{
			return Json::array({ ToJsonNumber(value.x), ToJsonNumber(value.y), ToJsonNumber(value.z), ToJsonNumber(value.w) });
		}

		std::string ToJsonId(uint64_t id)
		{
			return std::to_string(id);
		}

		const char* ToString(MaterialType type)
		{
			switch (type)
			{
				case MaterialType::Diffuse:
				{
					return "diffuse";
				}
				case MaterialType::Emissive:
				{
					return "emissive";
				}
			}

			return "diffuse";
		}

		const char* ToString(GeometryType type)
		{
			switch (type)
			{
				case GeometryType::None:
				{
					return "none";
				}
				case GeometryType::Sphere:
				{
					return "sphere";
				}
				case GeometryType::Quad:
				{
					return "quad";
				}
				case GeometryType::Mesh:
				{
					return "mesh";
				}
			}

			return "none";
		}

		Json ToJson(const Material& material)
		{
			Json l_Object = Json::object();
			l_Object["id"] = ToJsonId(std::to_underlying(material.Id));
			l_Object["name"] = material.Name;
			l_Object["type"] = ToString(material.Type);
			l_Object["baseColor"] = ToJson(material.BaseColor);
			l_Object["emissionColor"] = ToJson(material.EmissionColor);
			l_Object["emissionStrength"] = ToJsonNumber(material.EmissionStrength);

			return l_Object;
		}

		Json ToJson(const Entity& entity, const AssetManager& assets)
		{
			// Every geometry parameter is written whatever the type, so a round trip is exact and a type change in a text editor needs no new fields
			Json l_Geometry = Json::object();
			l_Geometry["type"] = ToString(entity.Geometry.Type);
			l_Geometry["radius"] = ToJsonNumber(entity.Geometry.Radius);
			l_Geometry["width"] = ToJsonNumber(entity.Geometry.Width);
			l_Geometry["height"] = ToJsonNumber(entity.Geometry.Height);

			// The mesh by its source, the one name that means the same thing in another process. An entity that chose no mesh writes none, an Id the assets do not hold is a bug in whoever set it
			if (entity.Geometry.Mesh != MeshId::Invalid)
			{
				const Mesh* l_Mesh = assets.FindMesh(entity.Geometry.Mesh);
				if (l_Mesh == nullptr)
				{
					Fail(std::format("entity '{}'", entity.Name), std::format("references mesh {}, which the asset manager does not hold", std::to_underlying(entity.Geometry.Mesh)));
				}

				l_Geometry["mesh"] = l_Mesh->Source;
			}

			Json l_Transform = Json::object();
			l_Transform["translation"] = ToJson(entity.Transform.Translation);
			l_Transform["rotation"] = ToJson(entity.Transform.Rotation);
			l_Transform["scale"] = ToJson(entity.Transform.Scale);

			Json l_Object = Json::object();
			l_Object["id"] = ToJsonId(std::to_underlying(entity.Id));
			l_Object["name"] = entity.Name;
			l_Object["visible"] = entity.Visible;
			l_Object["transform"] = std::move(l_Transform);
			l_Object["geometry"] = std::move(l_Geometry);
			l_Object["material"] = ToJsonId(std::to_underlying(entity.Material));

			return l_Object;
		}

		Json ToJson(const Scene& scene, const AssetManager& assets)
		{
			Json l_Environment = Json::object();
			l_Environment["radiance"] = ToJson(scene.GetEnvironment().Radiance);

			Json l_Spawn = Json::object();
			l_Spawn["position"] = ToJson(scene.GetPlayerSpawn().Position);
			l_Spawn["orientation"] = ToJson(scene.GetPlayerSpawn().Orientation);

			Json l_Materials = Json::array();
			for (const Material& l_Material : scene.GetMaterials())
			{
				l_Materials.push_back(ToJson(l_Material));
			}

			Json l_Entities = Json::array();
			for (const Entity& l_Entity : scene.GetEntities())
			{
				l_Entities.push_back(ToJson(l_Entity, assets));
			}

			Json l_Document = Json::object();
			l_Document["schemaVersion"] = SceneSerializer::k_SchemaVersion;
			l_Document["name"] = scene.GetName();
			l_Document["nextEntityId"] = ToJsonId(scene.GetNextEntityId());
			l_Document["nextMaterialId"] = ToJsonId(scene.GetNextMaterialId());
			l_Document["environment"] = std::move(l_Environment);
			l_Document["playerSpawn"] = std::move(l_Spawn);
			l_Document["materials"] = std::move(l_Materials);
			l_Document["entities"] = std::move(l_Entities);

			return l_Document;
		}

		// Readers --------------

		const Json& RequireField(const Json& object, std::string_view context, const char* key)
		{
			if (!object.is_object())
			{
				Fail(context, "expected an object");
			}

			const auto l_Found = object.find(key);
			if (l_Found == object.end() || l_Found->is_null())
			{
				Fail(context, std::format("missing field '{}'", key));
			}

			return *l_Found;
		}

		// Null for a missing or null field, so optional fields read the same way required ones do
		const Json* OptionalField(const Json& object, std::string_view context, const char* key)
		{
			if (!object.is_object())
			{
				Fail(context, "expected an object");
			}

			const auto l_Found = object.find(key);
			if (l_Found == object.end() || l_Found->is_null())
			{
				return nullptr;
			}

			return &*l_Found;
		}

		float ReadFloat(const Json& value, std::string_view context)
		{
			if (!value.is_number())
			{
				Fail(context, "expected a number");
			}

			const double l_Value = value.get<double>();
			if (!std::isfinite(l_Value))
			{
				Fail(context, "expected a finite number");
			}

			const float l_Float = static_cast<float>(l_Value);
			if (!std::isfinite(l_Float))
			{
				Fail(context, "number is outside the float range");
			}

			return l_Float;
		}

		bool ReadBool(const Json& value, std::string_view context)
		{
			if (!value.is_boolean())
			{
				Fail(context, "expected true or false");
			}

			return value.get<bool>();
		}

		std::string ReadString(const Json& value, std::string_view context)
		{
			if (!value.is_string())
			{
				Fail(context, "expected a string");
			}

			return value.get<std::string>();
		}

		// A decimal string, zero included so a material reference can say "none"
		uint64_t ReadId(const Json& value, std::string_view context)
		{
			if (!value.is_string())
			{
				Fail(context, "expected an id as a decimal string");
			}

			const std::string l_Text = value.get<std::string>();

			uint64_t l_Id = 0;
			const std::from_chars_result l_Parsed = std::from_chars(l_Text.data(), l_Text.data() + l_Text.size(), l_Id);
			if (l_Parsed.ec != std::errc{} || l_Parsed.ptr != l_Text.data() + l_Text.size() || l_Text.empty())
			{
				Fail(context, std::format("'{}' is not a decimal id", l_Text));
			}

			return l_Id;
		}

		Math::Vector3 ReadVector3(const Json& value, std::string_view context)
		{
			if (!value.is_array() || value.size() != 3)
			{
				Fail(context, "expected an array of three numbers");
			}

			return Math::Vector3(ReadFloat(value[0], std::format("{}[0]", context)), ReadFloat(value[1], std::format("{}[1]", context)), ReadFloat(value[2], std::format("{}[2]", context)));
		}

		Math::Vector3 ReadNonNegativeVector3(const Json& value, std::string_view context)
		{
			const Math::Vector3 l_Value = ReadVector3(value, context);
			if (l_Value.x < 0.0f || l_Value.y < 0.0f || l_Value.z < 0.0f)
			{
				Fail(context, "components must not be negative");
			}

			return l_Value;
		}

		Math::Vector3 ReadColor(const Json& value, std::string_view context)
		{
			const Math::Vector3 l_Value = ReadVector3(value, context);
			if (l_Value.x < 0.0f || l_Value.y < 0.0f || l_Value.z < 0.0f || l_Value.x > 1.0f || l_Value.y > 1.0f || l_Value.z > 1.0f)
			{
				// An albedo above one gains energy on every bounce and the path tracer never converges, so it is refused rather than clamped
				Fail(context, "components must be in [0, 1]");
			}

			return l_Value;
		}

		float ReadNonNegativeFloat(const Json& value, std::string_view context)
		{
			const float l_Value = ReadFloat(value, context);
			if (l_Value < 0.0f)
			{
				Fail(context, "must not be negative");
			}

			return l_Value;
		}

		// [x, y, z, w], returned unit length. A zero or tiny quaternion has no direction and is refused
		Math::Quaternion ReadQuaternion(const Json& value, std::string_view context, std::vector<std::string>& warnings)
		{
			if (!value.is_array() || value.size() != 4)
			{
				Fail(context, "expected an array of four numbers [x, y, z, w]");
			}

			const float l_X = ReadFloat(value[0], std::format("{}[0]", context));
			const float l_Y = ReadFloat(value[1], std::format("{}[1]", context));
			const float l_Z = ReadFloat(value[2], std::format("{}[2]", context));
			const float l_W = ReadFloat(value[3], std::format("{}[3]", context));

			// glm's constructor order is w, x, y, z, the file order is x, y, z, w
			const Math::Quaternion l_Quaternion(l_W, l_X, l_Y, l_Z);

			const float l_Length = glm::length(l_Quaternion);
			if (!std::isfinite(l_Length) || l_Length < k_MinimumQuaternionLength)
			{
				Fail(context, "quaternion has no direction");
			}

			if (std::abs(l_Length - 1.0f) <= k_ExactUnitTolerance)
			{
				return l_Quaternion;
			}

			if (std::abs(l_Length - 1.0f) > k_UnitTolerance)
			{
				warnings.push_back(std::format("{}: quaternion length {:.4f} is not one, normalized", context, l_Length));
			}

			return l_Quaternion / l_Length;
		}

		MaterialType ReadMaterialType(const Json& value, std::string_view context)
		{
			const std::string l_Text = ReadString(value, context);
			if (l_Text == "diffuse")
			{
				return MaterialType::Diffuse;
			}

			if (l_Text == "emissive")
			{
				return MaterialType::Emissive;
			}

			Fail(context, std::format("unknown material type '{}', expected \"diffuse\" or \"emissive\"", l_Text));
		}

		GeometryType ReadGeometryType(const Json& value, std::string_view context)
		{
			const std::string l_Text = ReadString(value, context);
			if (l_Text == "none")
			{
				return GeometryType::None;
			}

			if (l_Text == "sphere")
			{
				return GeometryType::Sphere;
			}

			if (l_Text == "quad")
			{
				return GeometryType::Quad;
			}

			if (l_Text == "mesh")
			{
				return GeometryType::Mesh;
			}

			Fail(context, std::format("unknown geometry type '{}', expected \"none\", \"sphere\", \"quad\" or \"mesh\"", l_Text));
		}

		Material ReadMaterial(const Json& object, std::string_view context)
		{
			Material l_Material;
			l_Material.Id = static_cast<MaterialId>(ReadId(RequireField(object, context, "id"), std::format("{}.id", context)));
			if (l_Material.Id == MaterialId::Invalid)
			{
				Fail(std::format("{}.id", context), "a material id must not be zero");
			}

			l_Material.Name = ReadString(RequireField(object, context, "name"), std::format("{}.name", context));
			l_Material.Type = ReadMaterialType(RequireField(object, context, "type"), std::format("{}.type", context));
			l_Material.BaseColor = ReadColor(RequireField(object, context, "baseColor"), std::format("{}.baseColor", context));
			l_Material.EmissionColor = ReadNonNegativeVector3(RequireField(object, context, "emissionColor"), std::format("{}.emissionColor", context));
			l_Material.EmissionStrength = ReadNonNegativeFloat(RequireField(object, context, "emissionStrength"), std::format("{}.emissionStrength", context));

			return l_Material;
		}

		Entity ReadEntity(const Json& object, std::string_view context, AssetManager& assets, std::vector<std::string>& warnings)
		{
			Entity l_Entity;
			l_Entity.Id = static_cast<EntityId>(ReadId(RequireField(object, context, "id"), std::format("{}.id", context)));
			if (l_Entity.Id == EntityId::Invalid)
			{
				Fail(std::format("{}.id", context), "an entity id must not be zero");
			}

			l_Entity.Name = ReadString(RequireField(object, context, "name"), std::format("{}.name", context));
			l_Entity.Visible = ReadBool(RequireField(object, context, "visible"), std::format("{}.visible", context));

			{
				const std::string l_Context = std::format("{}.transform", context);
				const Json& l_Transform = RequireField(object, context, "transform");

				l_Entity.Transform.Translation = ReadVector3(RequireField(l_Transform, l_Context, "translation"), std::format("{}.translation", l_Context));
				l_Entity.Transform.Rotation = ReadQuaternion(RequireField(l_Transform, l_Context, "rotation"), std::format("{}.rotation", l_Context), warnings);
				l_Entity.Transform.Scale = ReadVector3(RequireField(l_Transform, l_Context, "scale"), std::format("{}.scale", l_Context));
			}

			{
				const std::string l_Context = std::format("{}.geometry", context);
				const Json& l_Geometry = RequireField(object, context, "geometry");

				l_Entity.Geometry.Type = ReadGeometryType(RequireField(l_Geometry, l_Context, "type"), std::format("{}.type", l_Context));

				// The parameters the type does not use keep their defaults when they are absent, so a hand-written sphere needs only its radius
				if (const Json* l_Radius = OptionalField(l_Geometry, l_Context, "radius"))
				{
					l_Entity.Geometry.Radius = ReadFloat(*l_Radius, std::format("{}.radius", l_Context));
				}

				if (const Json* l_Width = OptionalField(l_Geometry, l_Context, "width"))
				{
					l_Entity.Geometry.Width = ReadFloat(*l_Width, std::format("{}.width", l_Context));
				}

				if (const Json* l_Height = OptionalField(l_Geometry, l_Context, "height"))
				{
					l_Entity.Geometry.Height = ReadFloat(*l_Height, std::format("{}.height", l_Context));
				}

				// The mesh is loaded through the assets by its source. A mesh that cannot be loaded, or a mesh entity that names none, is a warning: the entity renders nothing and the rest of the scene opens, the way a missing material falls back rather than failing the file
				if (const Json* l_Mesh = OptionalField(l_Geometry, l_Context, "mesh"))
				{
					const std::string l_MeshContext = std::format("{}.mesh", l_Context);
					const std::string l_Source = ReadString(*l_Mesh, l_MeshContext);

					std::string l_Error;
					l_Entity.Geometry.Mesh = assets.LoadMesh(l_Source, &l_Error);
					if (l_Entity.Geometry.Mesh == MeshId::Invalid)
					{
						warnings.push_back(std::format("{}: cannot load '{}': {}, the entity renders nothing", l_MeshContext, l_Source, l_Error));
					}
				}
				else if (l_Entity.Geometry.Type == GeometryType::Mesh)
				{
					warnings.push_back(std::format("{}: a mesh entity without a mesh renders nothing", l_Context));
				}

				// The renderer skips a degenerate shape with its own warning every extraction, the file says so once up front
				const bool l_Degenerate = (l_Entity.Geometry.Type == GeometryType::Sphere && l_Entity.Geometry.Radius <= 0.0f) || (l_Entity.Geometry.Type == GeometryType::Quad && (l_Entity.Geometry.Width <= 0.0f || l_Entity.Geometry.Height <= 0.0f));
				if (l_Degenerate)
				{
					warnings.push_back(std::format("{}: the shape has a zero or negative size and will not render", l_Context));
				}
			}

			if (const Json* l_Material = OptionalField(object, context, "material"))
			{
				l_Entity.Material = static_cast<MaterialId>(ReadId(*l_Material, std::format("{}.material", context)));
			}

			return l_Entity;
		}

		// The saved counter, raised to one past the largest Id present when it is lower or absent
		uint64_t ReadCounter(const Json& document, std::string_view context, const char* key, uint64_t largestId, std::vector<std::string>& warnings)
		{
			const uint64_t l_Minimum = largestId + 1;

			const Json* l_Value = OptionalField(document, context, key);
			if (l_Value == nullptr)
			{
				return l_Minimum;
			}

			const uint64_t l_Counter = ReadId(*l_Value, std::format("{}.{}", context, key));
			if (l_Counter < l_Minimum)
			{
				warnings.push_back(std::format("{}.{}: {} is not past the largest id present, using {}", context, key, l_Counter, l_Minimum));

				return l_Minimum;
			}

			return l_Counter;
		}

		void ReadDocument(const Json& document, std::string_view context, Scene& scene, AssetManager& assets, std::vector<std::string>& warnings)
		{
			const Json& l_Version = RequireField(document, context, "schemaVersion");
			if (!l_Version.is_number_unsigned() || l_Version.get<uint64_t>() != SceneSerializer::k_SchemaVersion)
			{
				Fail(std::format("{}.schemaVersion", context), std::format("expected {}, this build reads no other version", SceneSerializer::k_SchemaVersion));
			}

			scene.SetName(ReadString(RequireField(document, context, "name"), std::format("{}.name", context)));

			{
				const std::string l_Context = std::format("{}.environment", context);
				const Json& l_Environment = RequireField(document, context, "environment");

				scene.GetEnvironment().Radiance = ReadNonNegativeVector3(RequireField(l_Environment, l_Context, "radiance"), std::format("{}.radiance", l_Context));
			}

			{
				const std::string l_Context = std::format("{}.playerSpawn", context);
				const Json& l_Spawn = RequireField(document, context, "playerSpawn");

				PlayerSpawn& l_PlayerSpawn = scene.GetPlayerSpawn();
				l_PlayerSpawn.Position = ReadVector3(RequireField(l_Spawn, l_Context, "position"), std::format("{}.position", l_Context));
				l_PlayerSpawn.Orientation = ReadQuaternion(RequireField(l_Spawn, l_Context, "orientation"), std::format("{}.orientation", l_Context), warnings);

				// The spawn is a heading. FirstPersonController::Reset rebuilds the orientation from yaw and pitch, so roll in the file is silently lost there and loudly reported here
				const Math::Quaternion l_Heading = OrientationFromYawPitch(YawPitchFromOrientation(l_PlayerSpawn.Orientation));
				if (std::abs(glm::dot(l_Heading, l_PlayerSpawn.Orientation)) < 1.0f - k_RollTolerance)
				{
					warnings.push_back(std::format("{}.orientation: carries roll, the first-person controller keeps only its yaw and pitch", l_Context));
				}
			}

			uint64_t l_LargestMaterialId = 0;
			{
				const std::string l_Context = std::format("{}.materials", context);
				const Json& l_Materials = RequireField(document, context, "materials");
				if (!l_Materials.is_array())
				{
					Fail(l_Context, "expected an array");
				}

				for (size_t i_Material = 0; i_Material < l_Materials.size(); ++i_Material)
				{
					const std::string l_ItemContext = std::format("{}[{}]", l_Context, i_Material);

					Material l_Material = ReadMaterial(l_Materials[i_Material], l_ItemContext);
					const uint64_t l_Id = std::to_underlying(l_Material.Id);

					if (scene.RestoreMaterial(std::move(l_Material)) == nullptr)
					{
						Fail(std::format("{}.id", l_ItemContext), std::format("material id {} is a duplicate or out of range", l_Id));
					}

					l_LargestMaterialId = std::max(l_LargestMaterialId, l_Id);
				}
			}

			uint64_t l_LargestEntityId = 0;
			{
				const std::string l_Context = std::format("{}.entities", context);
				const Json& l_Entities = RequireField(document, context, "entities");
				if (!l_Entities.is_array())
				{
					Fail(l_Context, "expected an array");
				}

				for (size_t i_Entity = 0; i_Entity < l_Entities.size(); ++i_Entity)
				{
					const std::string l_ItemContext = std::format("{}[{}]", l_Context, i_Entity);

					Entity l_Entity = ReadEntity(l_Entities[i_Entity], l_ItemContext, assets, warnings);
					const uint64_t l_Id = std::to_underlying(l_Entity.Id);

					// A reference is validated against the materials of this document, the renderer's fallback material is for deletions at runtime and not for files
					if (l_Entity.Material != MaterialId::Invalid && scene.FindMaterial(l_Entity.Material) == nullptr)
					{
						Fail(std::format("{}.material", l_ItemContext), std::format("entity '{}' references material {}, which is not in the document", l_Entity.Name, std::to_underlying(l_Entity.Material)));
					}

					if (scene.RestoreEntity(std::move(l_Entity)) == nullptr)
					{
						Fail(std::format("{}.id", l_ItemContext), std::format("entity id {} is a duplicate or out of range", l_Id));
					}

					l_LargestEntityId = std::max(l_LargestEntityId, l_Id);
				}
			}

			scene.ReserveEntityIds(ReadCounter(document, context, "nextEntityId", l_LargestEntityId, warnings));
			scene.ReserveMaterialIds(ReadCounter(document, context, "nextMaterialId", l_LargestMaterialId, warnings));

			// The environment and the spawn were edited through references after the restores advanced the revision
			scene.MarkRadianceChanged();
		}

		void LogResult(const SceneFileResult& result, std::string_view operation)
		{
			for (const std::string& l_Warning : result.Warnings)
			{
				PT_CORE_WARN("Scene {}: {}", operation, l_Warning);
			}

			if (!result.Succeeded)
			{
				PT_CORE_ERROR("Scene {} failed: {}", operation, result.Error);
			}
		}
	}

	SceneFileResult SceneSerializer::Write(const Scene& scene, std::string& text, const AssetManager& assets)
	{
		SceneFileResult l_Result;

		try
		{
			text = ToJson(scene, assets).dump(1, '\t');
			text.push_back('\n');

			l_Result.Succeeded = true;
		}
		catch (const std::exception& exception)
		{
			text.clear();
			l_Result.Error = std::format("scene '{}': {}", scene.GetName(), exception.what());
		}

		return l_Result;
	}

	SceneFileResult SceneSerializer::Parse(std::string_view text, Scene& scene, AssetManager& assets, std::string_view sourceName)
	{
		SceneFileResult l_Result;

		// Everything lands in a fresh scene first, so a failure anywhere below leaves the caller's scene untouched. Meshes loaded on the way stay in the assets either way, a mesh is not scene content
		Scene l_Loaded;

		try
		{
			const Json l_Document = Json::parse(text);

			ReadDocument(l_Document, sourceName, l_Loaded, assets, l_Result.Warnings);
		}
		catch (const SceneFormatError& error)
		{
			l_Result.Error = error.what();

			return l_Result;
		}
		catch (const std::exception& exception)
		{
			// The JSON reader's own errors carry the byte offset, anything else is an allocation failure
			l_Result.Error = std::format("{}: {}", sourceName, exception.what());

			return l_Result;
		}

		// The loaded scene carries its own radiance revision, so the renderer re-extracts on the next frame
		scene = std::move(l_Loaded);

		l_Result.Succeeded = true;

		return l_Result;
	}

	SceneFileResult SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path, const AssetManager& assets)
	{
		std::string l_Text;
		SceneFileResult l_Result = Write(scene, l_Text, assets);
		if (!l_Result.Succeeded)
		{
			LogResult(l_Result, "save");

			return l_Result;
		}

		// The text is ready, the file is not: every early return below is a failure until the rename went through
		l_Result.Succeeded = false;

		std::error_code l_Error;

		const std::filesystem::path l_Directory = path.parent_path();
		if (!l_Directory.empty())
		{
			std::filesystem::create_directories(l_Directory, l_Error);
			if (l_Error)
			{
				l_Result.Error = std::format("{}: cannot create directory: {}", path.string(), l_Error.message());
				LogResult(l_Result, "save");

				return l_Result;
			}
		}

		// The temporary lives next to the destination, so the rename below stays inside one filesystem and replaces the old file in one step instead of a copy that can stop halfway
		std::filesystem::path l_Temporary = path;
		l_Temporary += ".tmp";

		{
			std::ofstream l_File(l_Temporary, std::ios::binary | std::ios::trunc);
			if (!l_File.is_open())
			{
				l_Result.Error = std::format("{}: cannot create the temporary file {}", path.string(), l_Temporary.string());
				LogResult(l_Result, "save");

				return l_Result;
			}

			l_File.write(l_Text.data(), static_cast<std::streamsize>(l_Text.size()));
			l_File.flush();

			if (!l_File)
			{
				l_File.close();
				std::filesystem::remove(l_Temporary, l_Error);

				l_Result.Error = std::format("{}: writing the temporary file failed, the previous file is untouched", path.string());
				LogResult(l_Result, "save");

				return l_Result;
			}
		}

		// POSIX rename replaces atomically, the Windows implementation replaces an existing destination as well
		std::filesystem::rename(l_Temporary, path, l_Error);
		if (l_Error)
		{
			std::filesystem::remove(l_Temporary, l_Error);

			l_Result.Error = std::format("{}: cannot replace the file: {}", path.string(), l_Error.message());
			LogResult(l_Result, "save");

			return l_Result;
		}

		l_Result.Succeeded = true;

		PT_CORE_TRACE("Scene '{}' saved to {}: {} entities, {} materials, {} bytes", scene.GetName(), path.string(), scene.GetEntities().size(), scene.GetMaterials().size(), l_Text.size());

		return l_Result;
	}

	SceneFileResult SceneSerializer::Load(const std::filesystem::path& path, Scene& scene, AssetManager& assets)
	{
		SceneFileResult l_Result;

		std::ifstream l_File(path, std::ios::binary);
		if (!l_File.is_open())
		{
			l_Result.Error = std::format("{}: cannot open the file", path.string());
			LogResult(l_Result, "load");

			return l_Result;
		}

		std::string l_Text;

		try
		{
			l_Text.assign(std::istreambuf_iterator<char>(l_File), std::istreambuf_iterator<char>());
		}
		catch (const std::exception& exception)
		{
			l_Result.Error = std::format("{}: cannot read the file: {}", path.string(), exception.what());
			LogResult(l_Result, "load");

			return l_Result;
		}

		if (l_File.bad())
		{
			l_Result.Error = std::format("{}: cannot read the file", path.string());
			LogResult(l_Result, "load");

			return l_Result;
		}

		l_Result = Parse(l_Text, scene, assets, path.string());
		LogResult(l_Result, "load");

		if (l_Result.Succeeded)
		{
			PT_CORE_TRACE("Scene '{}' loaded from {}: {} entities, {} materials, revision {}", scene.GetName(), path.string(), scene.GetEntities().size(), scene.GetMaterials().size(), scene.GetRadianceRevision());
		}

		return l_Result;
	}
}