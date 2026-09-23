#include "Engine/Assets/MeshImporter.hpp"

#include "Engine/Core/Log.hpp"

#include <cgltf.h>

#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <utility>

namespace Engine
{
	namespace
	{
		// Below this a normal or a transform has no direction left to normalize
		constexpr float k_DegenerateEpsilon = 1e-12f;

		// KHR_mesh_quantization only changes the component types of the vertex attributes, which cgltf converts on read, so a file that requires it imports right. Every other required extension changes what the geometry means
		constexpr const char* k_AllowedRequiredExtension = "KHR_mesh_quantization";

		const char* ResultName(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_data_too_short:
				{
					return "the data is too short to be a glTF document";
				}
				case cgltf_result_unknown_format:
				{
					return "not a glTF or GLB document";
				}
				case cgltf_result_invalid_json:
				{
					return "the JSON is malformed";
				}
				case cgltf_result_invalid_gltf:
				{
					return "the document is not valid glTF 2.0";
				}
				case cgltf_result_invalid_options:
				{
					return "invalid parser options";
				}
				case cgltf_result_file_not_found:
				{
					return "the file was not found";
				}
				case cgltf_result_io_error:
				{
					return "the file could not be read";
				}
				case cgltf_result_out_of_memory:
				{
					return "out of memory";
				}
				case cgltf_result_legacy_gltf:
				{
					return "glTF 1.0 is not supported, export glTF 2.0";
				}
				default:
				{
					return "unknown parser error";
				}
			}
		}

		const char* PrimitiveModeName(cgltf_primitive_type type)
		{
			switch (type)
			{
				case cgltf_primitive_type_points:
				{
					return "points";
				}
				case cgltf_primitive_type_lines:
				{
					return "lines";
				}
				case cgltf_primitive_type_line_loop:
				{
					return "line loop";
				}
				case cgltf_primitive_type_line_strip:
				{
					return "line strip";
				}
				case cgltf_primitive_type_triangles:
				{
					return "triangles";
				}
				case cgltf_primitive_type_triangle_strip:
				{
					return "triangle strip";
				}
				case cgltf_primitive_type_triangle_fan:
				{
					return "triangle fan";
				}
				default:
				{
					return "unknown";
				}
			}
		}

		// cgltf reads the document and its external buffers through these, so every path crosses as UTF-8 on every platform. The memory options may carry no allocator, cgltf's own default is malloc
		cgltf_result ReadFile(const cgltf_memory_options* memory, const cgltf_file_options*, const char* path, cgltf_size* size, void** data)
		{
			const std::filesystem::path l_Path(std::u8string_view(reinterpret_cast<const char8_t*>(path)));

			std::ifstream l_File(l_Path, std::ios::binary | std::ios::ate);
			if (!l_File.is_open())
			{
				return cgltf_result_file_not_found;
			}

			const std::streampos l_End = l_File.tellg();
			if (l_End < 0)
			{
				return cgltf_result_io_error;
			}

			const cgltf_size l_Size = static_cast<cgltf_size>(l_End);
			if (l_Size == 0)
			{
				return cgltf_result_data_too_short;
			}

			void* l_Buffer = memory->alloc_func != nullptr ? memory->alloc_func(memory->user_data, l_Size) : std::malloc(l_Size);
			if (l_Buffer == nullptr)
			{
				return cgltf_result_out_of_memory;
			}

			l_File.seekg(0, std::ios::beg);
			if (!l_File.read(static_cast<char*>(l_Buffer), static_cast<std::streamsize>(l_Size)))
			{
				if (memory->free_func != nullptr)
				{
					memory->free_func(memory->user_data, l_Buffer);
				}
				else
				{
					std::free(l_Buffer);
				}

				return cgltf_result_io_error;
			}

			*size = l_Size;
			*data = l_Buffer;

			return cgltf_result_success;
		}

		void ReleaseFile(const cgltf_memory_options* memory, const cgltf_file_options*, void* data, cgltf_size)
		{
			if (memory->free_func != nullptr)
			{
				memory->free_func(memory->user_data, data);
			}
			else
			{
				std::free(data);
			}
		}

		// Frees the parsed document and the buffers it loaded on every exit path
		struct Document
		{
			cgltf_data* Data = nullptr;

			~Document()
			{
				if (Data != nullptr)
				{
					cgltf_free(Data);
				}
			}
		};

		// A glTF matrix is sixteen floats in column-major order, the layout GLM uses
		Math::Matrix4 ToMatrix(const cgltf_float* values)
		{
			Math::Matrix4 l_Matrix;
			for (int i_Column = 0; i_Column < 4; ++i_Column)
			{
				for (int i_Row = 0; i_Row < 4; ++i_Row)
				{
					l_Matrix[i_Column][i_Row] = values[i_Column * 4 + i_Row];
				}
			}

			return l_Matrix;
		}

		std::string NodeName(const cgltf_data& data, const cgltf_node& node)
		{
			return node.name != nullptr ? std::format("node '{}'", node.name) : std::format("node {}", cgltf_node_index(&data, &node));
		}

		std::string MeshName(const cgltf_data& data, const cgltf_mesh& mesh)
		{
			return mesh.name != nullptr ? std::format("mesh '{}'", mesh.name) : std::format("mesh {}", cgltf_mesh_index(&data, &mesh));
		}

		const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type, cgltf_int index)
		{
			for (cgltf_size i_Attribute = 0; i_Attribute < primitive.attributes_count; ++i_Attribute)
			{
				const cgltf_attribute& l_Attribute = primitive.attributes[i_Attribute];
				if (l_Attribute.type == type && l_Attribute.index == index)
				{
					return l_Attribute.data;
				}
			}

			return nullptr;
		}

		// A vertex attribute the subset reads: dense, of the expected shape, and one per vertex. The accessor may be null when the attribute is optional
		bool CheckAttribute(const cgltf_accessor* accessor, const char* name, cgltf_type type, cgltf_size vertexCount, std::string_view context, std::string& error)
		{
			if (accessor == nullptr)
			{
				return true;
			}

			if (accessor->is_sparse)
			{
				error = std::format("{}: the {} accessor is sparse, which is not supported", context, name);

				return false;
			}

			if (accessor->type != type)
			{
				error = std::format("{}: {} is not a {}", context, name, type == cgltf_type_vec3 ? "vec3" : "vec2");

				return false;
			}

			if (accessor->count != vertexCount)
			{
				error = std::format("{}: {} has {} elements for {} vertices", context, name, accessor->count, vertexCount);

				return false;
			}

			return true;
		}

		// One triangle primitive appended in world space. Indexed geometry with normals keeps its vertices and offsets the indices; geometry without normals is unrolled to three vertices per triangle so the flat normal can sit on each
		bool AppendPrimitive(const cgltf_primitive& primitive, std::string_view context, const Math::Matrix4& localToWorld, const Math::Matrix3& normalMatrix, bool flipWinding, Mesh& mesh, std::string& error)
		{
			if (primitive.type != cgltf_primitive_type_triangles)
			{
				error = std::format("{} is drawn as {}, only triangles are supported", context, PrimitiveModeName(primitive.type));

				return false;
			}

			if (primitive.targets_count > 0)
			{
				error = std::format("{} has {} morph targets, which are not supported", context, primitive.targets_count);

				return false;
			}

			if (primitive.has_draco_mesh_compression)
			{
				error = std::format("{} uses KHR_draco_mesh_compression, which is not supported", context);

				return false;
			}

			const cgltf_accessor* l_Positions = FindAttribute(primitive, cgltf_attribute_type_position, 0);
			if (l_Positions == nullptr)
			{
				error = std::format("{} has no POSITION attribute", context);

				return false;
			}

			const cgltf_accessor* l_Normals = FindAttribute(primitive, cgltf_attribute_type_normal, 0);
			const cgltf_accessor* l_TexCoords = FindAttribute(primitive, cgltf_attribute_type_texcoord, 0);

			const cgltf_size l_VertexCount = l_Positions->count;
			if (!CheckAttribute(l_Positions, "POSITION", cgltf_type_vec3, l_VertexCount, context, error) || !CheckAttribute(l_Normals, "NORMAL", cgltf_type_vec3, l_VertexCount, context, error) || !CheckAttribute(l_TexCoords, "TEXCOORD_0", cgltf_type_vec2, l_VertexCount, context, error))
			{
				return false;
			}

			if (primitive.indices != nullptr && primitive.indices->is_sparse)
			{
				error = std::format("{}: the index accessor is sparse, which is not supported", context);

				return false;
			}

			const cgltf_size l_IndexCount = primitive.indices != nullptr ? primitive.indices->count : l_VertexCount;
			if (l_IndexCount % 3 != 0)
			{
				error = std::format("{} has {} indices, which is not a multiple of three", context, l_IndexCount);

				return false;
			}

			// The engine's indices are 32 bits, the appended vertices must stay addressable
			const cgltf_size l_AppendedVertices = l_Normals != nullptr ? l_VertexCount : l_IndexCount;
			if (mesh.Vertices.size() + l_AppendedVertices > std::numeric_limits<uint32_t>::max())
			{
				error = std::format("{} takes the mesh past {} vertices", context, std::numeric_limits<uint32_t>::max());

				return false;
			}

			const auto l_ReadIndex = [&](cgltf_size position) -> cgltf_size
			{
				return primitive.indices != nullptr ? cgltf_accessor_read_index(primitive.indices, position) : position;
			};

			// One vertex through the node's transform, the normal through the inverse transpose so non-uniform scale keeps it perpendicular
			const auto l_ReadVertex = [&](cgltf_size index, MeshVertex& vertex) -> bool
			{
				cgltf_float l_Position[3];
				if (!cgltf_accessor_read_float(l_Positions, index, l_Position, 3))
				{
					error = std::format("{}: vertex {} could not be read", context, index);

					return false;
				}

				vertex.Position = Math::Vector3(localToWorld * Math::Vector4(l_Position[0], l_Position[1], l_Position[2], 1.0f));

				if (l_Normals != nullptr)
				{
					cgltf_float l_Normal[3];
					if (!cgltf_accessor_read_float(l_Normals, index, l_Normal, 3))
					{
						error = std::format("{}: the normal of vertex {} could not be read", context, index);

						return false;
					}

					const Math::Vector3 l_World = normalMatrix * Math::Vector3(l_Normal[0], l_Normal[1], l_Normal[2]);
					const float l_Length = glm::length(l_World);
					if (!(l_Length > k_DegenerateEpsilon))
					{
						error = std::format("{}: vertex {} has a zero normal", context, index);

						return false;
					}

					vertex.Normal = l_World / l_Length;
				}

				if (l_TexCoords != nullptr)
				{
					cgltf_float l_TexCoord[2];
					if (!cgltf_accessor_read_float(l_TexCoords, index, l_TexCoord, 2))
					{
						error = std::format("{}: the texture coordinate of vertex {} could not be read", context, index);

						return false;
					}

					vertex.TexCoord = Math::Vector2(l_TexCoord[0], l_TexCoord[1]);
				}

				return true;
			};

			const uint32_t l_FirstVertex = static_cast<uint32_t>(mesh.Vertices.size());

			if (l_Normals != nullptr)
			{
				mesh.Vertices.reserve(mesh.Vertices.size() + l_VertexCount);
				for (cgltf_size i_Vertex = 0; i_Vertex < l_VertexCount; ++i_Vertex)
				{
					MeshVertex l_Vertex;
					if (!l_ReadVertex(i_Vertex, l_Vertex))
					{
						return false;
					}

					mesh.Vertices.push_back(l_Vertex);
				}

				mesh.Indices.reserve(mesh.Indices.size() + l_IndexCount);
				for (cgltf_size i_Index = 0; i_Index < l_IndexCount; i_Index += 3)
				{
					cgltf_size l_Triangle[3] = { l_ReadIndex(i_Index), l_ReadIndex(i_Index + 1), l_ReadIndex(i_Index + 2) };
					for (const cgltf_size l_Index : l_Triangle)
					{
						if (l_Index >= l_VertexCount)
						{
							error = std::format("{}: index {} is past the {} vertices", context, l_Index, l_VertexCount);

							return false;
						}
					}

					if (flipWinding)
					{
						std::swap(l_Triangle[1], l_Triangle[2]);
					}

					mesh.Indices.insert(mesh.Indices.end(), { l_FirstVertex + static_cast<uint32_t>(l_Triangle[0]), l_FirstVertex + static_cast<uint32_t>(l_Triangle[1]), l_FirstVertex + static_cast<uint32_t>(l_Triangle[2]) });
				}

				return true;
			}

			// No normals: the specification asks for flat shading, so every triangle gets its own three vertices carrying the face normal of its world-space, already reordered corners
			mesh.Vertices.reserve(mesh.Vertices.size() + l_IndexCount);
			mesh.Indices.reserve(mesh.Indices.size() + l_IndexCount);

			for (cgltf_size i_Index = 0; i_Index < l_IndexCount; i_Index += 3)
			{
				cgltf_size l_Triangle[3] = { l_ReadIndex(i_Index), l_ReadIndex(i_Index + 1), l_ReadIndex(i_Index + 2) };
				for (const cgltf_size l_Index : l_Triangle)
				{
					if (l_Index >= l_VertexCount)
					{
						error = std::format("{}: index {} is past the {} vertices", context, l_Index, l_VertexCount);

						return false;
					}
				}

				if (flipWinding)
				{
					std::swap(l_Triangle[1], l_Triangle[2]);
				}

				MeshVertex l_Corners[3];
				for (int i_Corner = 0; i_Corner < 3; ++i_Corner)
				{
					if (!l_ReadVertex(l_Triangle[i_Corner], l_Corners[i_Corner]))
					{
						return false;
					}
				}

				// A degenerate triangle keeps the default normal, it has no area to shade anyway
				const Math::Vector3 l_Cross = glm::cross(l_Corners[1].Position - l_Corners[0].Position, l_Corners[2].Position - l_Corners[0].Position);
				const float l_Length = glm::length(l_Cross);
				if (l_Length > k_DegenerateEpsilon)
				{
					const Math::Vector3 l_FaceNormal = l_Cross / l_Length;
					for (MeshVertex& l_Corner : l_Corners)
					{
						l_Corner.Normal = l_FaceNormal;
					}
				}

				const uint32_t l_Base = static_cast<uint32_t>(mesh.Vertices.size());
				mesh.Vertices.insert(mesh.Vertices.end(), { l_Corners[0], l_Corners[1], l_Corners[2] });
				mesh.Indices.insert(mesh.Indices.end(), { l_Base, l_Base + 1, l_Base + 2 });
			}

			return true;
		}

		// The node's mesh in world space, then its children. The visit count bounds a hierarchy that is not the tree the specification requires
		bool AppendNode(const cgltf_data& data, const cgltf_node& node, cgltf_size& visited, Mesh& mesh, std::string& error)
		{
			if (++visited > data.nodes_count)
			{
				error = "the node hierarchy is not a tree";

				return false;
			}

			if (node.mesh != nullptr)
			{
				const std::string l_NodeName = NodeName(data, node);

				if (node.skin != nullptr)
				{
					error = std::format("{} is skinned, skins are not supported", l_NodeName);

					return false;
				}

				if (node.has_mesh_gpu_instancing)
				{
					error = std::format("{} uses EXT_mesh_gpu_instancing, which is not supported", l_NodeName);

					return false;
				}

				if (node.weights_count > 0 || node.mesh->weights_count > 0)
				{
					error = std::format("{} carries morph target weights, which are not supported", l_NodeName);

					return false;
				}

				cgltf_float l_World[16];
				cgltf_node_transform_world(&node, l_World);

				const Math::Matrix4 l_LocalToWorld = ToMatrix(l_World);
				const Math::Matrix3 l_Linear(l_LocalToWorld);
				const float l_Determinant = glm::determinant(l_Linear);

				// A flattened node has no normals to speak of, the specification leaves them undefined; it is left out and said so, the rest of the file still imports
				if (!(std::abs(l_Determinant) > k_DegenerateEpsilon))
				{
					PT_CORE_WARN("glTF {} has a degenerate transform and was skipped", l_NodeName);
				}
				else
				{
					// A negative determinant mirrors the geometry, which turns counter-clockwise into clockwise: the triangles are reordered so outside stays outside
					const Math::Matrix3 l_NormalMatrix = glm::transpose(glm::inverse(l_Linear));
					const bool l_FlipWinding = l_Determinant < 0.0f;

					const std::string l_MeshName = MeshName(data, *node.mesh);
					for (cgltf_size i_Primitive = 0; i_Primitive < node.mesh->primitives_count; ++i_Primitive)
					{
						const std::string l_Context = std::format("primitive {} of {} on {}", i_Primitive, l_MeshName, l_NodeName);
						if (!AppendPrimitive(node.mesh->primitives[i_Primitive], l_Context, l_LocalToWorld, l_NormalMatrix, l_FlipWinding, mesh, error))
						{
							return false;
						}
					}
				}
			}

			for (cgltf_size i_Child = 0; i_Child < node.children_count; ++i_Child)
			{
				if (!AppendNode(data, *node.children[i_Child], visited, mesh, error))
				{
					return false;
				}
			}

			return true;
		}

		bool ImportDocument(cgltf_data& data, std::string_view name, Mesh& mesh, std::string& error)
		{
			for (cgltf_size i_Extension = 0; i_Extension < data.extensions_required_count; ++i_Extension)
			{
				const char* l_Extension = data.extensions_required[i_Extension];
				if (std::string_view(l_Extension) != k_AllowedRequiredExtension)
				{
					error = std::format("requires the extension {}, which is not supported", l_Extension);

					return false;
				}
			}

			const cgltf_result l_Validated = cgltf_validate(&data);
			if (l_Validated != cgltf_result_success)
			{
				error = std::format("validation failed: {}", ResultName(l_Validated));

				return false;
			}

			mesh = Mesh{};
			mesh.Name = std::string(name);

			// The default scene, the first when none is marked. A document without scenes is a library of nodes, every root is taken
			const cgltf_scene* l_Scene = data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? &data.scenes[0] : nullptr);

			cgltf_size l_Visited = 0;
			if (l_Scene != nullptr)
			{
				for (cgltf_size i_Node = 0; i_Node < l_Scene->nodes_count; ++i_Node)
				{
					if (!AppendNode(data, *l_Scene->nodes[i_Node], l_Visited, mesh, error))
					{
						return false;
					}
				}
			}
			else
			{
				for (cgltf_size i_Node = 0; i_Node < data.nodes_count; ++i_Node)
				{
					if (data.nodes[i_Node].parent == nullptr && !AppendNode(data, data.nodes[i_Node], l_Visited, mesh, error))
					{
						return false;
					}
				}
			}

			if (mesh.Indices.empty())
			{
				error = std::format("the scene has no triangles, the document holds {} nodes and {} meshes", data.nodes_count, data.meshes_count);

				return false;
			}

			mesh.Bounds = ComputeMeshBounds(mesh);

			PT_CORE_TRACE("glTF '{}' imported: {} nodes visited, {} vertices, {} triangles; {} materials, {} textures, {} animations, {} cameras and {} lights in the document were not read", name, l_Visited, mesh.Vertices.size(), mesh.GetTriangleCount(), data.materials_count, data.textures_count, data.animations_count, data.cameras_count, data.lights_count);

			return true;
		}
	}

	bool ImportGltfMesh(const std::filesystem::path& path, Mesh& mesh, std::string& error)
	{
		cgltf_options l_Options{};
		l_Options.file.read = &ReadFile;
		l_Options.file.release = &ReleaseFile;

		const std::u8string l_Utf8Path = path.u8string();
		const char* l_Path = reinterpret_cast<const char*>(l_Utf8Path.c_str());

		Document l_Document;
		cgltf_result l_Result = cgltf_parse_file(&l_Options, l_Path, &l_Document.Data);
		if (l_Result != cgltf_result_success)
		{
			error = ResultName(l_Result);

			return false;
		}

		// The external and embedded buffers, resolved next to the document
		l_Result = cgltf_load_buffers(&l_Options, l_Document.Data, l_Path);
		if (l_Result != cgltf_result_success)
		{
			error = std::format("a buffer could not be loaded: {}", ResultName(l_Result));

			return false;
		}

		return ImportDocument(*l_Document.Data, path.stem().string(), mesh, error);
	}

	bool ImportGltfMeshFromMemory(std::span<const std::byte> document, std::string_view name, Mesh& mesh, std::string& error)
	{
		cgltf_options l_Options{};

		Document l_Document;
		cgltf_result l_Result = cgltf_parse(&l_Options, document.data(), document.size(), &l_Document.Data);
		if (l_Result != cgltf_result_success)
		{
			error = ResultName(l_Result);

			return false;
		}

		// No path to resolve against, so only a data URI or the GLB chunk can be loaded and an external buffer is refused
		l_Result = cgltf_load_buffers(&l_Options, l_Document.Data, nullptr);
		if (l_Result != cgltf_result_success)
		{
			error = l_Result == cgltf_result_unknown_format ? std::string("a buffer is external, which cannot be loaded from memory") : std::format("a buffer could not be loaded: {}", ResultName(l_Result));

			return false;
		}

		return ImportDocument(*l_Document.Data, name, mesh, error);
	}
}