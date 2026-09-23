#include "Sandbox/MeshChecks.hpp"

#include "Engine/Engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Sandbox
{
	namespace
	{
		using Engine::Math::Vector2;
		using Engine::Math::Vector3;

		constexpr float k_Tolerance = 1e-4f;

		// A glTF buffer is little-endian bytes, which is the byte order of every platform this builds on
		void AppendFloats(std::vector<std::byte>& bytes, std::span<const float> values)
		{
			for (const float l_Value : values)
			{
				const auto l_Bytes = std::bit_cast<std::array<std::byte, 4>>(l_Value);
				bytes.insert(bytes.end(), l_Bytes.begin(), l_Bytes.end());
			}
		}

		void AppendIndices(std::vector<std::byte>& bytes, std::span<const uint16_t> values)
		{
			for (const uint16_t l_Value : values)
			{
				const auto l_Bytes = std::bit_cast<std::array<std::byte, 2>>(l_Value);
				bytes.insert(bytes.end(), l_Bytes.begin(), l_Bytes.end());
			}
		}

		std::string EncodeBase64(std::span<const std::byte> bytes)
		{
			constexpr std::string_view k_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

			std::string l_Encoded;
			for (size_t i_Byte = 0; i_Byte < bytes.size(); i_Byte += 3)
			{
				const size_t l_Count = std::min<size_t>(3, bytes.size() - i_Byte);

				uint32_t l_Chunk = 0;
				for (size_t i_Offset = 0; i_Offset < 3; ++i_Offset)
				{
					l_Chunk = (l_Chunk << 8) | (i_Offset < l_Count ? static_cast<uint32_t>(bytes[i_Byte + i_Offset]) : 0u);
				}

				l_Encoded += k_Alphabet[(l_Chunk >> 18) & 63];
				l_Encoded += k_Alphabet[(l_Chunk >> 12) & 63];
				l_Encoded += l_Count > 1 ? k_Alphabet[(l_Chunk >> 6) & 63] : '=';
				l_Encoded += l_Count > 2 ? k_Alphabet[l_Chunk & 63] : '=';
			}

			return l_Encoded;
		}

		// A unit quad in the XY plane, ±0.5, normals along +Z, wound counter-clockwise seen from +Z, as one embedded glTF document: positions at buffer view 0, normals at 1, texture coordinates at 2, six 16-bit indices at 3. The caller writes the nodes, one flat object each so the brace count is the node count, and may drop the normals or add to the primitive and the document
		std::string BuildQuadDocument(std::string_view nodes, bool withNormals, std::string_view primitiveExtra, std::string_view documentExtra)
		{
			constexpr std::array<float, 12> k_Positions{ -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f };
			constexpr std::array<float, 12> k_Normals{ 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
			constexpr std::array<float, 8> k_TexCoords{ 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
			constexpr std::array<uint16_t, 6> k_Indices{ 0, 1, 2, 0, 2, 3 };

			std::vector<std::byte> l_Bytes;
			AppendFloats(l_Bytes, k_Positions);
			AppendFloats(l_Bytes, k_Normals);
			AppendFloats(l_Bytes, k_TexCoords);
			AppendIndices(l_Bytes, k_Indices);

			std::string l_Document = "{\"asset\":{\"version\":\"2.0\"},";
			l_Document += documentExtra;
			l_Document += "\"scene\":0,\"scenes\":[{\"nodes\":[";
			for (size_t i_Node = 0; i_Node < static_cast<size_t>(std::count(nodes.begin(), nodes.end(), '{')); ++i_Node)
			{
				l_Document += i_Node == 0 ? std::to_string(i_Node) : "," + std::to_string(i_Node);
			}

			l_Document += "]}],\"nodes\":[";
			l_Document += nodes;
			l_Document += "],\"meshes\":[{\"name\":\"Quad\",\"primitives\":[{\"attributes\":{\"POSITION\":0,";
			if (withNormals)
			{
				l_Document += "\"NORMAL\":1,";
			}

			l_Document += "\"TEXCOORD_0\":2},\"indices\":3";
			l_Document += primitiveExtra;
			l_Document += "}]}],";
			l_Document += "\"accessors\":[";
			l_Document += "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[-0.5,-0.5,0.0],\"max\":[0.5,0.5,0.0]},";
			l_Document += "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},";
			l_Document += "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},";
			l_Document += "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],";
			l_Document += "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}],";
			l_Document += "\"buffers\":[{\"byteLength\":" + std::to_string(l_Bytes.size()) + ",\"uri\":\"data:application/octet-stream;base64," + EncodeBase64(l_Bytes) + "\"}]}";

			return l_Document;
		}

		bool ImportDocument(const std::string& document, Engine::Mesh& mesh, std::string& error)
		{
			return Engine::ImportGltfMeshFromMemory(std::as_bytes(std::span(document)), "Check", mesh, error);
		}

		bool Check(bool passed, const char* name)
		{
			if (passed)
			{
				PT_APP_TRACE("Mesh check passed: {}", name);
			}
			else
			{
				PT_APP_ERROR("Mesh check FAILED: {}", name);
			}

			return passed;
		}

		bool Near(const Vector3& a, const Vector3& b, float tolerance = k_Tolerance)
		{
			return Engine::Math::NearlyEqual(a, b, tolerance);
		}

		// Every triangle wound counter-clockwise seen from outside, so the geometric normal points away from the centre
		bool HasOutwardWinding(const Engine::Mesh& mesh)
		{
			for (size_t i_Index = 0; i_Index + 2 < mesh.Indices.size(); i_Index += 3)
			{
				const Vector3& l_P0 = mesh.Vertices[mesh.Indices[i_Index]].Position;
				const Vector3& l_P1 = mesh.Vertices[mesh.Indices[i_Index + 1]].Position;
				const Vector3& l_P2 = mesh.Vertices[mesh.Indices[i_Index + 2]].Position;

				if (glm::dot(glm::cross(l_P1 - l_P0, l_P2 - l_P0), (l_P0 + l_P1 + l_P2) / 3.0f) <= 0.0f)
				{
					return false;
				}
			}

			return true;
		}

		// The closest triangle of a mesh along an object-space ray with its interpolated normal, the same steps the shader takes so a change in either shows up here
		float IntersectMesh(const Engine::Mesh& mesh, const Vector3& origin, const Vector3& direction, Vector3& normal)
		{
			float l_Closest = -1.0f;
			for (size_t i_Index = 0; i_Index + 2 < mesh.Indices.size(); i_Index += 3)
			{
				const Engine::MeshVertex& l_V0 = mesh.Vertices[mesh.Indices[i_Index]];
				const Engine::MeshVertex& l_V1 = mesh.Vertices[mesh.Indices[i_Index + 1]];
				const Engine::MeshVertex& l_V2 = mesh.Vertices[mesh.Indices[i_Index + 2]];

				Vector2 l_Barycentrics;
				const float l_Distance = Engine::IntersectTriangle(origin, direction, l_V0.Position, l_V1.Position, l_V2.Position, 0.0f, l_Barycentrics);
				if (l_Distance > 0.0f && (l_Closest < 0.0f || l_Distance < l_Closest))
				{
					l_Closest = l_Distance;
					normal = glm::normalize(l_V0.Normal * (1.0f - l_Barycentrics.x - l_Barycentrics.y) + l_V1.Normal * l_Barycentrics.x + l_V2.Normal * l_Barycentrics.y);
				}
			}

			return l_Closest;
		}
	}

	bool RunMeshChecks()
	{
		using namespace Engine;

		bool l_AllPassed = true;

		const Mesh l_Cube = GenerateCubeMesh();
		const Mesh l_Icosphere = GenerateIcosphereMesh(2);

		// 1. The cube is 24 vertices and 12 outward-wound triangles inside ±0.5, the icosphere 320 triangles on the sphere of radius 0.5 with normals along its radii, and both pass the validation the AssetManager applies
		{
			std::string l_Error;
			const bool l_CubeValid = ValidateMesh(l_Cube, l_Error) && l_Cube.Vertices.size() == 24 && l_Cube.GetTriangleCount() == 12 && Near(l_Cube.Bounds.Min, Vector3(-0.5f)) && Near(l_Cube.Bounds.Max, Vector3(0.5f)) && HasOutwardWinding(l_Cube);

			bool l_OnSphere = ValidateMesh(l_Icosphere, l_Error) && l_Icosphere.GetTriangleCount() == 320 && HasOutwardWinding(l_Icosphere);
			for (const MeshVertex& l_Vertex : l_Icosphere.Vertices)
			{
				l_OnSphere = l_OnSphere && Math::NearlyEqual(glm::length(l_Vertex.Position), 0.5f) && Near(l_Vertex.Normal, glm::normalize(l_Vertex.Position));
			}

			l_AllPassed = Check(l_CubeValid && l_OnSphere, "built-in meshes have the expected size, bounds, winding and normals") && l_AllPassed;
		}

		// 2. Validation refuses the mistakes an importer could make: an index past the vertices and a count that is not a multiple of three
		{
			std::string l_Error;

			Mesh l_BadIndex = l_Cube;
			l_BadIndex.Indices[5] = 24;

			Mesh l_BadCount = l_Cube;
			l_BadCount.Indices.pop_back();

			l_AllPassed = Check(!ValidateMesh(l_BadIndex, l_Error) && !ValidateMesh(l_BadCount, l_Error), "validation refuses an out-of-range index and a dangling index count") && l_AllPassed;
		}

		// 3. Möller-Trumbore on one triangle: the hit at its centroid carries barycentrics of a third each, the sign of the normal does not matter, and a ray past an edge misses
		{
			const Vector3 l_P0(-1.0f, -1.0f, 0.0f);
			const Vector3 l_P1(1.0f, -1.0f, 0.0f);
			const Vector3 l_P2(0.0f, 1.0f, 0.0f);
			const Vector3 l_Centroid = (l_P0 + l_P1 + l_P2) / 3.0f;

			Vector2 l_Front;
			const float l_FrontDistance = IntersectTriangle(l_Centroid + Vector3(0.0f, 0.0f, 2.0f), Vector3(0.0f, 0.0f, -1.0f), l_P0, l_P1, l_P2, 0.0f, l_Front);

			Vector2 l_Back;
			const float l_BackDistance = IntersectTriangle(l_Centroid - Vector3(0.0f, 0.0f, 3.0f), Vector3(0.0f, 0.0f, 1.0f), l_P0, l_P1, l_P2, 0.0f, l_Back);

			Vector2 l_Outside;
			const float l_Miss = IntersectTriangle(Vector3(2.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f), l_P0, l_P1, l_P2, 0.0f, l_Outside);

			Vector2 l_Behind;
			const float l_BehindMiss = IntersectTriangle(l_Centroid + Vector3(0.0f, 0.0f, 2.0f), Vector3(0.0f, 0.0f, 1.0f), l_P0, l_P1, l_P2, 0.0f, l_Behind);

			const bool l_Hits = Math::NearlyEqual(l_FrontDistance, 2.0f) && Math::NearlyEqual(l_BackDistance, 3.0f) && Math::NearlyEqual(l_Front.x, 1.0f / 3.0f) && Math::NearlyEqual(l_Front.y, 1.0f / 3.0f);

			l_AllPassed = Check(l_Hits && l_Miss < 0.0f && l_BehindMiss < 0.0f, "triangle intersection is two-sided, reports centroid barycentrics and misses past the edges and behind the origin") && l_AllPassed;
		}

		// 4. The bounds test: a ray through the box, one that misses it beside a face, one parallel to a face inside its slab, and one whose overlap lies past the distance limit
		{
			const Vector3 l_Min(-0.5f);
			const Vector3 l_Max(0.5f);

			const bool l_Through = IntersectBounds(Vector3(0.0f, 0.0f, 4.0f), Vector3(0.0f, 0.0f, -1.0f), l_Min, l_Max, 0.0f, 1e30f);
			const bool l_Beside = !IntersectBounds(Vector3(0.0f, 1.0f, 4.0f), Vector3(0.0f, 0.0f, -1.0f), l_Min, l_Max, 0.0f, 1e30f);
			const bool l_Parallel = IntersectBounds(Vector3(-4.0f, 0.25f, 0.25f), Vector3(1.0f, 0.0f, 0.0f), l_Min, l_Max, 0.0f, 1e30f);
			const bool l_ParallelOutside = !IntersectBounds(Vector3(-4.0f, 0.75f, 0.25f), Vector3(1.0f, 0.0f, 0.0f), l_Min, l_Max, 0.0f, 1e30f);
			const bool l_TooFar = !IntersectBounds(Vector3(0.0f, 0.0f, 4.0f), Vector3(0.0f, 0.0f, -1.0f), l_Min, l_Max, 0.0f, 3.0f);

			l_AllPassed = Check(l_Through && l_Beside && l_Parallel && l_ParallelOutside && l_TooFar, "bounds test accepts rays through the box and rejects the ones beside it, parallel outside it or past the limit") && l_AllPassed;
		}

		// 5. The cube is hit on its +Z face at distance 3.5 with the flat normal, and the icosphere at its radius with the smooth normal along the ray
		{
			Vector3 l_CubeNormal;
			const float l_CubeDistance = IntersectMesh(l_Cube, Vector3(0.1f, -0.2f, 4.0f), Vector3(0.0f, 0.0f, -1.0f), l_CubeNormal);

			Vector3 l_SphereNormal;
			const float l_SphereDistance = IntersectMesh(l_Icosphere, Vector3(0.0f, 0.0f, 4.0f), Vector3(0.0f, 0.0f, -1.0f), l_SphereNormal);

			const bool l_CubeHit = Math::NearlyEqual(l_CubeDistance, 3.5f) && Near(l_CubeNormal, Vector3(0.0f, 0.0f, 1.0f));

			// The facets lie a little inside the sphere, so the distance is near 3.5 and the interpolated normal near the radius, both within the facet's sag at 320 triangles
			const bool l_SphereHit = Math::NearlyEqual(l_SphereDistance, 3.5f, 0.02f) && Near(l_SphereNormal, Vector3(0.0f, 0.0f, 1.0f), 0.05f);

			l_AllPassed = Check(l_CubeHit && l_SphereHit, "cube and icosphere are hit at the expected distance with the flat and the interpolated normal") && l_AllPassed;
		}

		// 6. Through the scene: a cube scaled by two, turned a quarter about Y and moved along -Z is picked at the distance its transformed face sits at, and a second entity sharing the mesh in front of it wins the pick
		{
			AssetManager l_Assets;
			const MeshId l_CubeId = l_Assets.LoadMesh(AssetManager::k_CubeSource);

			Scene l_Scene;

			Entity& l_Far = l_Scene.CreateEntity("Far cube");
			l_Far.Geometry.Type = GeometryType::Mesh;
			l_Far.Geometry.Mesh = l_CubeId;
			l_Far.Transform.Translation = Vector3(0.0f, 0.0f, -6.0f);
			l_Far.Transform.Rotation = glm::angleAxis(Math::k_HalfPi, Math::k_Up);
			l_Far.Transform.Scale = Vector3(2.0f, 2.0f, 2.0f);
			const EntityId l_FarId = l_Far.Id;

			const Ray l_Ray{ .Origin = Vector3(0.0f, 0.0f, 4.0f), .Direction = Vector3(0.0f, 0.0f, -1.0f) };

			// The near face of a 2 m cube centred 10 m ahead is 9 m away
			const ScenePick l_Alone = PickClosest(l_Scene, &l_Assets, l_Ray);
			const bool l_TransformedHit = l_Alone.Entity == l_FarId && Math::NearlyEqual(l_Alone.Distance, 9.0f) && Near(l_Alone.Position, Vector3(0.0f, 0.0f, -5.0f));

			Entity& l_Near = l_Scene.CreateEntity("Near cube");
			l_Near.Geometry.Type = GeometryType::Mesh;
			l_Near.Geometry.Mesh = l_CubeId;
			l_Near.Transform.Translation = Vector3(0.0f, 0.0f, -1.0f);
			const EntityId l_NearId = l_Near.Id;

			const ScenePick l_Both = PickClosest(l_Scene, &l_Assets, l_Ray);
			const bool l_NearestWins = l_Both.Entity == l_NearId && Math::NearlyEqual(l_Both.Distance, 4.5f);

			// Without the assets a mesh entity is not pickable, as it is not rendered
			const ScenePick l_NoAssets = PickClosest(l_Scene, nullptr, l_Ray);

			l_AllPassed = Check(l_TransformedHit && l_NearestWins && l_NoAssets.Entity == EntityId::Invalid, "a transformed cube is picked at its face, the nearer of two entities sharing the mesh wins, and no assets means no pick") && l_AllPassed;
		}

		// 7. The manager hands the same source the same Id, refuses a relative file source while no asset root is set rather than looking in the working directory, and refuses an unknown built-in by name
		{
			AssetManager l_Assets;
			std::string l_Error;

			const MeshId l_First = l_Assets.LoadMesh(AssetManager::k_IcosphereSource);
			const MeshId l_Second = l_Assets.LoadMesh(AssetManager::k_IcosphereSource);
			const MeshId l_File = l_Assets.LoadMesh("Meshes/Missing.gltf", &l_Error);
			const bool l_FileRefused = l_File == MeshId::Invalid && l_Error.contains("asset root");
			const MeshId l_Unknown = l_Assets.LoadMesh("builtin:teapot", &l_Error);

			l_AllPassed = Check(l_First != MeshId::Invalid && l_First == l_Second && l_Assets.GetMeshes().size() == 1 && l_FileRefused && l_Unknown == MeshId::Invalid, "the asset manager shares one mesh per source and refuses what it cannot load") && l_AllPassed;
		}

		// 8. The importer folds the node transforms in: the quad under two nodes, one moved to +Z and one mirrored in X and moved to -Z, is one mesh of eight vertices and four triangles whose normals all still point +Z, whose mirrored triangles were rewound so their geometric normal agrees, and whose texture coordinates came through
		{
			Mesh l_Imported;
			std::string l_Error;
			const bool l_Loaded = ImportDocument(BuildQuadDocument("{\"mesh\":0,\"translation\":[0,0,1]},{\"mesh\":0,\"scale\":[-1,1,1],\"translation\":[0,0,-1]}", true, "", ""), l_Imported, l_Error);

			bool l_Correct = l_Loaded && ValidateMesh(l_Imported, l_Error) && l_Imported.Vertices.size() == 8 && l_Imported.GetTriangleCount() == 4 && Near(l_Imported.Bounds.Min, Vector3(-0.5f, -0.5f, -1.0f)) && Near(l_Imported.Bounds.Max, Vector3(0.5f, 0.5f, 1.0f));
			for (size_t i_Vertex = 0; l_Correct && i_Vertex < 4; ++i_Vertex)
			{
				const MeshVertex& l_Moved = l_Imported.Vertices[i_Vertex];
				const MeshVertex& l_Mirrored = l_Imported.Vertices[i_Vertex + 4];

				l_Correct = Near(l_Moved.Normal, Vector3(0.0f, 0.0f, 1.0f)) && Near(l_Mirrored.Normal, Vector3(0.0f, 0.0f, 1.0f)) && Math::NearlyEqual(l_Mirrored.Position.x, -l_Moved.Position.x) && l_Moved.TexCoord == l_Mirrored.TexCoord;
			}

			for (size_t i_Index = 0; l_Correct && i_Index + 2 < l_Imported.Indices.size(); i_Index += 3)
			{
				const MeshVertex& l_V0 = l_Imported.Vertices[l_Imported.Indices[i_Index]];
				const MeshVertex& l_V1 = l_Imported.Vertices[l_Imported.Indices[i_Index + 1]];
				const MeshVertex& l_V2 = l_Imported.Vertices[l_Imported.Indices[i_Index + 2]];

				l_Correct = glm::dot(glm::cross(l_V1.Position - l_V0.Position, l_V2.Position - l_V0.Position), l_V0.Normal) > 0.0f;
			}

			if (!l_Loaded)
			{
				PT_APP_ERROR("Import failed: {}", l_Error);
			}

			l_AllPassed = Check(l_Correct, "an imported glTF folds the node transforms into one mesh, rewinds a mirrored node and keeps the normals and texture coordinates") && l_AllPassed;
		}

		// 9. A primitive without normals gets flat ones: the quad turned a quarter about X so its face looks along -Y, unrolled to six vertices carrying that normal
		{
			Mesh l_Imported;
			std::string l_Error;
			const bool l_Loaded = ImportDocument(BuildQuadDocument("{\"mesh\":0,\"rotation\":[0.7071068,0,0,0.7071068]}", false, "", ""), l_Imported, l_Error);

			bool l_Flat = l_Loaded && ValidateMesh(l_Imported, l_Error) && l_Imported.Vertices.size() == 6 && l_Imported.GetTriangleCount() == 2;
			for (size_t i_Vertex = 0; l_Flat && i_Vertex < l_Imported.Vertices.size(); ++i_Vertex)
			{
				l_Flat = Near(l_Imported.Vertices[i_Vertex].Normal, Vector3(0.0f, -1.0f, 0.0f));
			}

			if (!l_Loaded)
			{
				PT_APP_ERROR("Import failed: {}", l_Error);
			}

			l_AllPassed = Check(l_Flat, "an imported primitive without normals is unrolled with flat normals through the node's rotation") && l_AllPassed;
		}

		// 10. What the subset does not cover is refused by name: a points primitive, a required extension, and a morph target
		{
			Mesh l_Unused;
			std::string l_ModeError;
			std::string l_ExtensionError;
			std::string l_TargetError;

			const bool l_ModeRefused = !ImportDocument(BuildQuadDocument("{\"mesh\":0}", true, ",\"mode\":0", ""), l_Unused, l_ModeError) && l_ModeError.contains("points");
			const bool l_ExtensionRefused = !ImportDocument(BuildQuadDocument("{\"mesh\":0}", true, "", "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],"), l_Unused, l_ExtensionError) && l_ExtensionError.contains("KHR_draco_mesh_compression");
			const bool l_TargetRefused = !ImportDocument(BuildQuadDocument("{\"mesh\":0}", true, ",\"targets\":[{\"POSITION\":0}]", ""), l_Unused, l_TargetError) && l_TargetError.contains("morph");

			l_AllPassed = Check(l_ModeRefused && l_ExtensionRefused && l_TargetRefused, "the importer refuses a points primitive, a required extension and a morph target by name") && l_AllPassed;
		}

		if (l_AllPassed)
		{
			PT_APP_INFO("Mesh checks passed");
		}
		else
		{
			PT_APP_ERROR("One or more mesh checks failed, see the messages above");
		}

		return l_AllPassed;
	}
}