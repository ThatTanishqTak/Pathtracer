#include "Sandbox/MeshChecks.hpp"

#include "Engine/Engine.hpp"

#include <cmath>
#include <string>

namespace Sandbox
{
	namespace
	{
		using Engine::Math::Vector2;
		using Engine::Math::Vector3;

		constexpr float k_Tolerance = 1e-4f;

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

		// 7. The manager hands the same source the same Id, refuses a file source until the importer lands, and refuses an unknown built-in by name
		{
			AssetManager l_Assets;
			std::string l_Error;

			const MeshId l_First = l_Assets.LoadMesh(AssetManager::k_IcosphereSource);
			const MeshId l_Second = l_Assets.LoadMesh(AssetManager::k_IcosphereSource);
			const MeshId l_File = l_Assets.LoadMesh("Meshes/Missing.gltf", &l_Error);
			const bool l_FileRefused = l_File == MeshId::Invalid && !l_Error.empty();
			const MeshId l_Unknown = l_Assets.LoadMesh("builtin:teapot", &l_Error);

			l_AllPassed = Check(l_First != MeshId::Invalid && l_First == l_Second && l_Assets.GetMeshes().size() == 1 && l_FileRefused && l_Unknown == MeshId::Invalid, "the asset manager shares one mesh per source and refuses what it cannot load") && l_AllPassed;
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