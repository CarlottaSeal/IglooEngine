#pragma once
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include <vector>
#include <string>

#include "StaticMesh.h"

// struct MtlInfo
// {
//     Rgba8 diffuseColor = Rgba8(255, 255, 255, 255);       // Kd
//     std::string diffuseMap;   // map_Kd
//     std::string normalMap;    // map_Bump / bump
//     std::string specularMap;  // map_Ks
//     float shininess = 0.f;    // Ns
// };

struct cgltf_data;
struct cgltf_material;

bool LoadStaticMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& filePath, bool flipUV = false, std::string const& mtlPath = "");
bool LoadOBJMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& filePath, bool flipUV = false, std::string const& mtlPath = "");
void ComputeMissingNormals(std::vector<Vertex_PCUTBN>& verts);
void ComputeMissingUVs(std::vector<Vertex_PCUTBN>& verts, bool flipUV = false);
void ComputeMissingTangentsBitangents(std::vector<Vertex_PCUTBN>& verts);
void ComputeMissingUVsNormalsTangentsBitangents(std::vector<Vertex_PCUTBN>& verts, bool flipUV = false);
void ComputeTangentsBitangentsIndexed(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices);

bool LoadGLBMeshFile(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices, std::string const& path, bool flipUV = false);
cgltf_data* LoadGLTFDataFromFile(const std::string& path);
Image* LoadImageDueToGLTFData(cgltf_data* data, GLBChannel channelType = GLBChannel::Albedo, std::string glbPath = "");

bool LoadOBJMaterial(std::string const& path, std::map<std::string, Rgba8>& outMap) noexcept;
bool LoadOBJMaterial(std::string const& path, std::map<std::string, MtlInfo>& materialMap) noexcept;
std::string ConstructTexturePath(const std::string& mtlDirectory, const std::string& texturePath);

struct VertexKey
{
    Vec3 m_pos;
    Vec2 m_uv;
    Vec3 m_normal;
    Rgba8 m_color;

    bool operator==(const VertexKey& anotherVk) const
    {
        return m_pos == anotherVk.m_pos && m_uv == anotherVk.m_uv && m_normal == anotherVk.m_normal
        && anotherVk.m_color == m_color;
    }
};

struct VertexKeyHasher
{
    size_t operator()(const VertexKey& k) const
    {
        size_t h = 0;
        h ^= std::hash<float>()(k.m_pos.x);
        h ^= std::hash<float>()(k.m_pos.y) << 1;
        h ^= std::hash<float>()(k.m_pos.z) << 2;
        h ^= std::hash<float>()(k.m_uv.x) << 3;
        h ^= std::hash<float>()(k.m_uv.y) << 4;
        h ^= std::hash<float>()(k.m_normal.x) << 5;
        h ^= std::hash<float>()(k.m_normal.y) << 6;
        h ^= std::hash<float>()(k.m_normal.z) << 7;

        std::hash<uint32_t> hi;
        uint32_t c = (uint32_t)k.m_color.r
        | ((uint32_t)k.m_color.g << 8 )
        | ((uint32_t)k.m_color.b << 16)
        | ((uint32_t)k.m_color.a << 24);
        h^=hi(c);
        
        return h;
    }
};

