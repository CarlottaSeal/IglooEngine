//#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <complex>
#include "StaticMeshUtils.h"
#include "FileUtils.hpp"
#include "StringUtils.hpp"
#include "EngineCommon.hpp"
#include "Image.hpp"
#include "ThirdParty/stb/stb_image.h"

#define CGLTF_IMPLEMENTATION
#include <algorithm>

#include "ThirdParty/cgltf/cgltf.h"
//#include "ThirdParty/cgltf/cgltf_write.h"

bool LoadStaticMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& filePath, bool flipUV, std::string const& mtlPath)
{
    if (EndsWith(filePath, ".obj"))
    {
        return LoadOBJMeshFile(verts, indices, filePath, flipUV, mtlPath);
    }
    if (EndsWith(filePath, ".glb"))
    {
        return LoadGLBMeshFile(verts, indices, filePath, flipUV);
    }
    return false;
}

bool LoadOBJMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& filePath, bool flipUV, std::string const& mtlPath)
{
    std::string readObj;
    if (FileReadToString(readObj, filePath) < 0)
    {
        DebuggerPrintf("Warning: Cannot read OBJ file: %s\n", filePath.c_str());
        return false;
    }
    
    Strings lines = SplitStringOnDelimiter(readObj, '\n');

    std::map<std::string, MtlInfo> materials;
    Rgba8 currentColor = Rgba8::WHITE; 

    if (!mtlPath.empty())
    {
        if (!LoadOBJMaterial(mtlPath, materials))
        {
            DebuggerPrintf("Warning: Failed to load MTL file: %s, using default materials\n", mtlPath.c_str());
            // 不返回false，继续使用默认材质
        }
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    //Rgba8 currentColor = Rgba8::WHITE;

    bool hasUV = false;
    bool hasNormal = false;

    bool hasFaceIndexing = false; 
    
    for (const std::string& line : lines)
    {
        Strings tokens = SplitStringOnDelimiter(line, ' ', true);
        if (tokens.empty()) continue;

        if (tokens[0] == "f" && tokens.size() >= 4)
        {
            for (int j = 1; j < (int)tokens.size(); ++j)
            {
                if (tokens[j].find('/') != std::string::npos)
                {
                    hasFaceIndexing = true;
                    break;
                }
            }
            if (hasFaceIndexing) break;
        }
    }

    verts.clear();
    indices.clear();

    if (hasFaceIndexing)
    {
        std::unordered_map<VertexKey, unsigned int, VertexKeyHasher> vertexMap;

        for (std::string& line : lines)
        {
            Strings tokens = SplitStringOnDelimiter(line, ' ', true);
            
            if (tokens.empty()) continue;

            if (tokens[0] == "v")
            {
                //EraseEmptyStrings(tokens);
                if (tokens.size() < 4) continue;
                positions.emplace_back(Vec3(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])));
            }
            else if (tokens[0] == "vt")
            {
                if (tokens.size() < 3) continue;
                float u = std::stof(tokens[1]);
                float v = std::stof(tokens[2]);
                if (flipUV) v = 1.f - v;      
                texCoords.emplace_back(Vec2(u, v));
                //texCoords.emplace_back(Vec2(std::stof(tokens[1]), std::stof(tokens[2])));
            }
            else if (tokens[0] == "vn")
            {
                if (tokens.size() < 4) continue;
                normals.emplace_back(Vec3(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])).GetNormalized());
            }
            else if (tokens[0] == "usemtl")
            {
                if (tokens.size() >= 2 && !materials.empty())
                {
                    auto it = materials.find(tokens[1]);
                    if (it != materials.end())
                    {
                        currentColor = it->second.m_diffuseColor;
                    }
                    else
                    {
                        DebuggerPrintf("Warning: Material '%s' not found in MTL file\n", tokens[1].c_str());
                    }
                }
            }
            else if (tokens[0] == "f")
            {
                for (int j = 1; j <= 3; ++j)
                {
                    Strings parts = SplitStringOnDelimiter(tokens[j], '/');
                    int vi = std::stoi(parts[0]) - 1;
                    int ti = (parts.size() > 1 && !parts[1].empty()) ? std::stoi(parts[1]) - 1 : -1;
                    int ni = (parts.size() > 2 && !parts[2].empty()) ? std::stoi(parts[2]) - 1 : -1;

                    Vec3 pos = positions[vi];
                    Vec2 uv = (ti >= 0 && ti < (int)texCoords.size()) ? texCoords[ti] : Vec2(0.5f, 0.5f);
                    Vec3 norm = (ni >= 0 && ni < (int)normals.size()) ? normals[ni] : Vec3(0.f,0.f,1.f);

                    if (ti >= 0) hasUV = true;
                    if (ni >= 0) hasNormal = true;

                    VertexKey key{ pos, uv, norm, currentColor};
                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end())
                    {
                        indices.push_back(it->second);
                    }
                    else
                    {
                        Vertex_PCUTBN vert;
                        vert.m_position = pos;
                        vert.m_uvTexCoords = uv;
                        vert.m_normal = norm.GetLengthSquared() > 0.f ? norm.GetNormalized() : Vec3(0.f, 0.f, 1.f);;
                        vert.m_color = currentColor;
                        vert.m_tangent = Vec3(1.f, 0, 0);
                        vert.m_bitangent = Vec3(0, 1.f, 0);

                        unsigned int newIndex = (unsigned int)verts.size();
                        verts.push_back(vert);
                        vertexMap[key] = newIndex;
                        indices.push_back(newIndex);
                    }
                }
            }
        }
    }
    else
    {
        for (std::string& line : lines)
        {
            Strings tokens = SplitStringOnDelimiter(line, ' ', true);
            if (tokens.empty()) continue;

            if (tokens[0] == "v")
            {
                if (tokens.size() < 4) continue;
                positions.emplace_back(Vec3(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3])));
            }
            else if (tokens[0] == "usemtl")
            {
                if (tokens.size() >= 2 && !materials.empty())
                {
                    auto it = materials.find(tokens[1]);
                    if (it != materials.end())
                    {
                        currentColor = it->second.m_diffuseColor;
                    }
                    else
                    {
                        DebuggerPrintf("Warning: Material '%s' not found in MTL file\n", tokens[1].c_str());
                    }
                }
            }
            else if (tokens[0] == "f")
            {
                if (tokens.size() < 4) continue;

                for (int j = 1; j <= 3; ++j)
                {
                    int vi = std::stoi(tokens[j]) - 1;
                    if (vi < 0 || vi >= (int)positions.size()) continue;

                    Vertex_PCUTBN vert;
                    vert.m_position = positions[vi];
                    vert.m_color = currentColor;
                    vert.m_uvTexCoords = Vec2(0.f, 0.f);
                    vert.m_normal = Vec3();
                    vert.m_tangent = Vec3(1, 0, 0);
                    vert.m_bitangent = Vec3(0, 1, 0);

                    verts.push_back(vert);
                    indices.push_back((unsigned int)(verts.size() - 1));
                }
            }
        }
    }
    if (hasUV && !hasNormal)
    {
        ComputeMissingNormals(verts);
        ComputeTangentsBitangentsIndexed(verts, indices);
    }
    else if (!hasNormal && !hasUV)
    {
		ComputeMissingUVs(verts, flipUV);
        ComputeMissingNormals(verts);
		ComputeTangentsBitangentsIndexed(verts, indices);
        //ComputeMissingUVsNormalsTangentsBitangents(verts, flipUV);
    }
	else if (!hasUV && hasNormal)
	{
        ComputeMissingUVs(verts, flipUV);
        ComputeTangentsBitangentsIndexed(verts, indices);
	}
    else
    {
        ComputeTangentsBitangentsIndexed(verts, indices);
    }
    return true;
}

bool LoadOBJMaterial(std::string const& path, std::map<std::string, Rgba8>& materialMap) noexcept
{
    materialMap.clear();
    std::string rawMtlFile;
    FileReadToString( rawMtlFile, path );
    Strings mtlLines = SplitStringOnDelimiter( rawMtlFile, '\n' );

    std::string mtlName;
    for (auto& line : mtlLines)
    {
        Strings lineElements = SplitStringOnDelimiter(line, ' ', true);
        if ((int)lineElements.size() > 0)
        {
            if (lineElements[0] == "newmtl")
            {
                mtlName = lineElements[1];
            }
            else if (lineElements[0] == "Kd")
            {
                materialMap[mtlName] = Rgba8();
                materialMap[mtlName].r = DenormalizeByte( stof( lineElements[1] ) );
                materialMap[mtlName].g = DenormalizeByte( stof( lineElements[2] ) );
                materialMap[mtlName].b = DenormalizeByte( stof( lineElements[3] ) );
            }
        }
    }
    return true;
}

void ComputeMissingNormals(std::vector<Vertex_PCUTBN>& verts) //有TB
{
    /*if (verts.size() % 3 != 0)
    {
        ERROR_AND_DIE("Cannot construct triangles")
    }*/
    for (int i = 0; i + 2 < verts.size(); i += 3)
    {
        Vertex_PCUTBN& v0 = verts[i];
        Vertex_PCUTBN& v1 = verts[i + 1];
        Vertex_PCUTBN& v2 = verts[i + 2];

        v0.m_normal = CrossProduct3D(v0.m_tangent, v0.m_bitangent).GetNormalized();
        v1.m_normal = CrossProduct3D(v1.m_tangent, v1.m_bitangent).GetNormalized();
        v2.m_normal = CrossProduct3D(v2.m_tangent, v1.m_bitangent).GetNormalized();
    }
}

void ComputeMissingUVs(std::vector<Vertex_PCUTBN>& verts, bool flipUV)
{
	if (verts.empty()) 
        return;

	Vec3 bmin(+FLT_MAX, +FLT_MAX, +FLT_MAX);
	Vec3 bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const auto& v : verts) 
    {
		const Vec3& p = v.m_position;
		bmin.x = MinF(bmin.x, p.x); bmax.x = MaxF(bmax.x, p.x);
		bmin.y = MinF(bmin.y, p.y); bmax.y = MaxF(bmax.y, p.y);
		bmin.z = MinF(bmin.z, p.z); bmax.z = MaxF(bmax.z, p.z);
	}
	auto remap01 = [](float x, float a, float b) 
    {
		float d = b - a;
		return (fabsf(d) < 1e-6f) ? 0.5f : (x - a) / d;
    };

	for (Vertex_PCUTBN& v : verts)
	{
		Vec3 N = v.m_normal;
		if (N.GetLengthSquared() < 1e-12f) 
            N = Vec3(0, 0, 1);
		N = N.GetNormalized();

		const Vec3& P = v.m_position;
		float ax = fabsf(N.x), ay = fabsf(N.y), az = fabsf(N.z);
		Vec2 uv;

		if (ax >= ay && ax >= az) 
        {      
			uv.x = remap01(P.z, bmin.z, bmax.z);
			uv.y = remap01(P.y, bmin.y, bmax.y);
		}
		else if (ay >= ax && ay >= az) 
        {     
			uv.x = remap01(P.x, bmin.x, bmax.x);
			uv.y = remap01(P.z, bmin.z, bmax.z);
		}
		else 
        {                               
			uv.x = remap01(P.x, bmin.x, bmax.x);
			uv.y = remap01(P.y, bmin.y, bmax.y);
		}

		if (flipUV) 
            uv.y = 1.0f - uv.y;
		v.m_uvTexCoords = uv;
	}
}

void ComputeMissingTangentsBitangents(std::vector<Vertex_PCUTBN>& verts)
{
   /* if (verts.size() % 3 != 0)
    {
        ERROR_AND_DIE("Cannot construct triangles")
    }*/
    for (int i = 0; i + 2 < verts.size(); i += 3)
    {

        Vertex_PCUTBN& v0 = verts[i];
        Vertex_PCUTBN& v1 = verts[i + 1];
        Vertex_PCUTBN& v2 = verts[i + 2];

        Vec3 p01 = v1.m_position - v0.m_position;
        Vec3 p02 = v2.m_position - v0.m_position;
        Vec2 uv01 = v1.m_uvTexCoords - v0.m_uvTexCoords;
        Vec2 uv02 = v2.m_uvTexCoords - v0.m_uvTexCoords;

        //based on ccw
        float d = uv01.x * uv01.y - uv01.y * uv01.x;
        if (d<1e-6)
        {
            v0.m_tangent = Vec3(1.f, 0, 0);
            v1.m_tangent = Vec3(1.f, 0, 0);
            v2.m_tangent = Vec3(1.f, 0, 0);
            v0.m_bitangent = Vec3(0, 1.f, 0);
            v1.m_bitangent = Vec3(0, 1.f, 0);
            v2.m_bitangent = Vec3(0, 1.f, 0);
            
            continue;
        }
        Vec3 t = ((p01 * uv02.y - p02 * uv01.y)/d).GetNormalized();
        Vec3 b = ((p02 * uv01.x - p01 * uv02.x)/d).GetNormalized();
        v0.m_tangent = t;
        v1.m_tangent = t;
        v2.m_tangent = t;
        v0.m_bitangent = b;
        v1.m_bitangent = b;
        v2.m_bitangent = b;
    }
}

void ComputeMissingUVsNormalsTangentsBitangents(std::vector<Vertex_PCUTBN>& verts, bool flipUV)
{
    /*if (verts.size() % 3 != 0)
    {
        ERROR_AND_DIE("Cannot construct triangles")
    }*/
    for (int i = 0; i + 2 < verts.size(); i += 3)
    {
        Vertex_PCUTBN& v0 = verts[i];
        Vertex_PCUTBN& v1 = verts[i + 1];
        Vertex_PCUTBN& v2 = verts[i + 2];

        Vec3 p01 = v1.m_position - v0.m_position;
        Vec3 p02 = v2.m_position - v0.m_position;
        Vec3 p12 = v2.m_position - v1.m_position;
        Vec3 p10 = v0.m_position - v1.m_position;
        Vec3 p20 = v0.m_position - v2.m_position;
        Vec3 p21 = v1.m_position - v2.m_position;

        v0.m_normal = CrossProduct3D(p01, p02).GetNormalized();
        v1.m_normal = CrossProduct3D(p12, p10).GetNormalized();
        v2.m_normal = CrossProduct3D(p20, p21).GetNormalized();

        v0.m_uvTexCoords = Vec2(v0.m_position.x, v0.m_position.y);
        v1.m_uvTexCoords = Vec2(v1.m_position.x, v1.m_position.y);
        v2.m_uvTexCoords = Vec2(v2.m_position.x, v2.m_position.y);
        if (flipUV)
        {
			v0.m_uvTexCoords.y = 1.f - v0.m_uvTexCoords.y;
			v1.m_uvTexCoords.y = 1.f - v1.m_uvTexCoords.y;
			v2.m_uvTexCoords.y = 1.f - v2.m_uvTexCoords.y;
        }

        Vec3 t;
        Vec3 b;
        if (fabs(v0.m_normal.y)<0.99f)
        {
            t = (CrossProduct3D(v0.m_normal, Vec3(0.f,1.f,0.f))).GetNormalized();
        }
        else
        {
            t = (CrossProduct3D(v0.m_normal, Vec3(1.f,0.f,0.f))).GetNormalized();
        }
        b = CrossProduct3D(v0.m_normal, t).GetNormalized();

        v0.m_tangent = t;
        v1.m_tangent = t;
        v2.m_tangent = t;
        v0.m_bitangent = b;
        v1.m_bitangent = b;
        v2.m_bitangent = b;
    }
}
//T和B反映了UV轴方向对应了空间中哪条向量
void ComputeTangentsBitangentsIndexed(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices)
{
    for (Vertex_PCUTBN& v : verts)
    {
        v.m_tangent = Vec3();
        v.m_bitangent = Vec3();
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        Vertex_PCUTBN& v0 = verts[i0];
        Vertex_PCUTBN& v1 = verts[i1];
        Vertex_PCUTBN& v2 = verts[i2];

        const Vec3& p0 = v0.m_position;
        const Vec3& p1 = v1.m_position;
        const Vec3& p2 = v2.m_position;

        const Vec2& uv0 = v0.m_uvTexCoords;
        const Vec2& uv1 = v1.m_uvTexCoords;
        const Vec2& uv2 = v2.m_uvTexCoords;

        Vec3 edge1 = p1 - p0;
        Vec3 edge2 = p2 - p0;
        Vec2 deltaUV1 = uv1 - uv0;
        Vec2 deltaUV2 = uv2 - uv0;

        float f = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        if (fabs(f) < 1e-6f)
            continue; 

        float invF = 1.0f / f;

        Vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invF;
        Vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * invF;

        // 累加到三个顶点
        v0.m_tangent += tangent;
        v1.m_tangent += tangent;
        v2.m_tangent += tangent;

        v0.m_bitangent += bitangent;
        v1.m_bitangent += bitangent;
        v2.m_bitangent += bitangent;
    }

    for (Vertex_PCUTBN& v : verts)
    {
        v.m_tangent = v.m_tangent.GetNormalized();
        v.m_bitangent = v.m_bitangent.GetNormalized();
    }
}

bool LoadGLBMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& path, bool flipUV)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success)
        return false;
    
    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success)
    {
        cgltf_free(data);
        return false;
    }
    
    for (cgltf_size i = 0; i < data->meshes_count; ++i)
    {
        const cgltf_mesh& mesh = data->meshes[i];
        for (cgltf_size j = 0; j < mesh.primitives_count; ++j)
        {
            const cgltf_primitive& prim = mesh.primitives[j];
            
            const cgltf_accessor* pos_accessor = nullptr;
            const cgltf_accessor* norm_accessor = nullptr;
            const cgltf_accessor* uv_accessor = nullptr;
            
            for (cgltf_size k = 0; k < prim.attributes_count; ++k)
            {
                switch (prim.attributes[k].type)
                {
                case cgltf_attribute_type_position:
                    pos_accessor = prim.attributes[k].data;
                    break;
                case cgltf_attribute_type_normal:
                    norm_accessor = prim.attributes[k].data;
                    break;
                case cgltf_attribute_type_texcoord:
                    uv_accessor = prim.attributes[k].data;
                    break;
                default: break;
                }
            }
            
            if (!pos_accessor || !prim.indices) continue;
            
            cgltf_size vertex_count = pos_accessor->count;
            size_t base_vertex_index = verts.size();
            
            for (cgltf_size v = 0; v < vertex_count; ++v)
            {
                Vertex_PCUTBN vertex = {};
                
                float pos[3] = {};
                cgltf_accessor_read_float(pos_accessor, v, pos, 3);
                vertex.m_position = Vec3(pos[0], pos[1], pos[2]);
                if (norm_accessor)
                {
                    float norm[3] = {};
                    cgltf_accessor_read_float(norm_accessor, v, norm, 3);
                    vertex.m_normal = Vec3(norm[0], norm[1], norm[2]);
                }
                if (uv_accessor)
                {
                    float uv[2] = {};
                    cgltf_accessor_read_float(uv_accessor, v, uv, 2);
                    if (flipUV == true)
                    {
                        vertex.m_uvTexCoords = Vec2(uv[0], 1.f - uv[1]);
                    }
                    else
                    {
                        vertex.m_uvTexCoords = Vec2(uv[0], uv[1]);
                    }
                }
                
                vertex.m_color = Rgba8::WHITE;
                vertex.m_tangent = Vec3();
                vertex.m_bitangent = Vec3();
                
                verts.push_back(vertex);
            }
            
            // now, read the index
            cgltf_size index_count = prim.indices->count;
            for (cgltf_size idx = 0; idx < index_count; ++idx)
            {
                cgltf_uint vertex_index = (cgltf_uint)cgltf_accessor_read_index(prim.indices, idx);
                indices.push_back((unsigned int)(base_vertex_index + vertex_index));
            }
        }
    }
    
    ComputeTangentsBitangentsIndexed(verts, indices);
    cgltf_free(data);
    return true;
}

cgltf_data* LoadGLTFDataFromFile(const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) return nullptr;

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success)
    {
        cgltf_free(data);
        return nullptr;
    }

    return data;
}

Image* LoadImageDueToGLTFData(cgltf_data* in_data, GLBChannel channelType /*= GLBChannel::Albedo*/, std::string gltfFilePath /*= ""*/)
{
    const cgltf_material* material = in_data->materials;
    const cgltf_texture* texture = nullptr;

    switch (channelType)
    {
    case GLBChannel::Albedo:
        if (material->has_pbr_metallic_roughness)
            texture = material->pbr_metallic_roughness.base_color_texture.texture;
        break;

    case GLBChannel::Normal:
        texture = material->normal_texture.texture;
        break;

    case GLBChannel::AO:
        texture = material->occlusion_texture.texture;
        break;

    case GLBChannel::Metallic:
    case GLBChannel::Roughness:
        if (material->has_pbr_metallic_roughness)
            texture = material->pbr_metallic_roughness.metallic_roughness_texture.texture;
        break;

    default:
        break;
    }

    if (!texture || !texture->image) return nullptr;

    const cgltf_image* image = texture->image;

    std::vector<uint8_t> imageData;
    if (image->uri)
    {
		size_t lastSlash = gltfFilePath.find_last_of("/\\");
		std::string directory = (lastSlash != std::string::npos)
			? gltfFilePath.substr(0, lastSlash)
			: ".";

		std::string fullPath = directory + "/" + image->uri;
        return new Image(fullPath.c_str());
        //return new Image(image->uri);
    }
    else if (image->buffer_view)
    {
        const uint8_t* data = (const uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
        size_t size = image->buffer_view->size;
        imageData.assign(data, data + size);
    }
    else
        {
        return nullptr;
    }

    if (imageData.empty()) return nullptr;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(imageData.data(), (int)imageData.size(), &width, &height, &channels, 4);
    if (!pixels)
        return nullptr;

    Image* img = new Image(pixels, width, height, 4); 
    stbi_image_free(pixels);
    
    cgltf_free(in_data);
    return img;
}

bool LoadOBJMaterial(std::string const& path, std::map<std::string, MtlInfo>& materialMap) noexcept
{
    materialMap.clear();
    std::string rawMtlFile;
    if (FileReadToString(rawMtlFile, path) < 0)
    {
        DebuggerPrintf("Warning: Cannot read MTL file: %s\n", path.c_str());
        return false;
    }
    
    // 获取MTL文件的目录路径（用于相对路径处理）
    size_t lastSlash = path.find_last_of("/\\");
    std::string mtlDirectory = (lastSlash != std::string::npos) 
        ? path.substr(0, lastSlash) : ".";
    
    Strings mtlLines = SplitStringOnDelimiter(rawMtlFile, '\n');

    std::string currentMtlName;
    MtlInfo* currentMtl = nullptr;
    
    for (auto& line : mtlLines)
    {
        // 移除行尾的\r（Windows换行符）
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
            
        Strings tokens = SplitStringOnDelimiter(line, ' ', true);
        if (tokens.empty()) continue;

        try
        {
            if (tokens[0] == "newmtl")
            {
                if (tokens.size() >= 2)
                {
                    currentMtlName = tokens[1];
                    materialMap[currentMtlName] = MtlInfo();
                    currentMtl = &materialMap[currentMtlName];
                }
            }
            else if (currentMtl) // 确保有当前材质
            {
                if (tokens[0] == "Ka" && tokens.size() >= 4)
                {
                    currentMtl->m_ambientColor.r = DenormalizeByte(std::stof(tokens[1]));
                    currentMtl->m_ambientColor.g = DenormalizeByte(std::stof(tokens[2]));
                    currentMtl->m_ambientColor.b = DenormalizeByte(std::stof(tokens[3]));
                }
                else if (tokens[0] == "Kd" && tokens.size() >= 4)
                {
                    currentMtl->m_diffuseColor.r = DenormalizeByte(std::stof(tokens[1]));
                    currentMtl->m_diffuseColor.g = DenormalizeByte(std::stof(tokens[2]));
                    currentMtl->m_diffuseColor.b = DenormalizeByte(std::stof(tokens[3]));
                }
                else if (tokens[0] == "Ks" && tokens.size() >= 4)
                {
                    currentMtl->m_specularColor.r = DenormalizeByte(std::stof(tokens[1]));
                    currentMtl->m_specularColor.g = DenormalizeByte(std::stof(tokens[2]));
                    currentMtl->m_specularColor.b = DenormalizeByte(std::stof(tokens[3]));
                }
                else if (tokens[0] == "Ke" && tokens.size() >= 4)
                {
                    currentMtl->m_emissiveColor.r = DenormalizeByte(std::stof(tokens[1]));
                    currentMtl->m_emissiveColor.g = DenormalizeByte(std::stof(tokens[2]));
                    currentMtl->m_emissiveColor.b = DenormalizeByte(std::stof(tokens[3]));
                }
                else if (tokens[0] == "Ns" && tokens.size() >= 2)
                {
                    currentMtl->m_shininess = std::stof(tokens[1]);
                }
                else if ((tokens[0] == "d" || tokens[0] == "Tr") && tokens.size() >= 2)
                {
                    currentMtl->m_transparency = std::stof(tokens[1]);
                }
                else if (tokens[0] == "Ni" && tokens.size() >= 2)
                {
                    currentMtl->m_refractiveIndex = std::stof(tokens[1]);
                }
                else if (tokens[0] == "illum" && tokens.size() >= 2)
                {
                    currentMtl->m_illumModel = std::stoi(tokens[1]);
                }
                // 纹理贴图处理
                else if (tokens[0] == "map_Kd" && tokens.size() >= 2)
                {
                    currentMtl->m_diffuseMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1)); // 处理带空格的路径
                }
                else if ((tokens[0] == "map_Bump" || tokens[0] == "bump") && tokens.size() >= 2)
                {
                    currentMtl->m_normalMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1));
                }
                else if (tokens[0] == "map_Ks" && tokens.size() >= 2)
                {
                    currentMtl->m_specularMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1));
                }
                else if (tokens[0] == "map_Ns" && tokens.size() >= 2)
                {
                    currentMtl->m_roughnessMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1));
                }
                else if (tokens[0] == "map_Ka" && tokens.size() >= 2)
                {
                    currentMtl->m_ambientMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1));
                }
                else if (tokens[0] == "map_d" && tokens.size() >= 2)
                {
                    currentMtl->m_opacityMap = ConstructTexturePath(mtlDirectory, 
                        JoinStrings(tokens, 1));
                }
            }
        }
        catch (const std::exception& e)
        {
            UNUSED(e)
            DebuggerPrintf("Warning: Error parsing MTL line: %s\n", line.c_str());
            continue; // 跳过错误的行，继续解析
        }
    }
    
    return !materialMap.empty();
}

std::string ConstructTexturePath(const std::string& mtlDirectory, const std::string& texturePath)
{
    // 移除路径中的引号（如果有）
    std::string cleanPath = texturePath;
    if (!cleanPath.empty() && cleanPath.front() == '"')
        cleanPath = cleanPath.substr(1);
    if (!cleanPath.empty() && cleanPath.back() == '"')
        cleanPath.pop_back();
    
    // 将反斜杠替换为正斜杠
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
    
    // 如果是绝对路径，直接返回
    if (cleanPath.size() > 1 && cleanPath[1] == ':') // Windows绝对路径
        return cleanPath;
    if (!cleanPath.empty() && cleanPath[0] == '/') // Unix绝对路径
        return cleanPath;
    
    return mtlDirectory + "/" + cleanPath;
}


