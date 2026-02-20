#pragma once
#include "Engine/Save/ISerializable.h"
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/Mat44.hpp"
#include <vector>
#include <string>

class StaticMesh;
class SDFTexture3D;
class DX12Renderer;
class Renderer; 

#pragma pack(push, 1)
struct MeshCacheHeader : public SaveFileHeader
{
    uint32_t m_vertexCount;
    uint32_t m_indexCount;
    float m_unitsPerMeter;
    float m_modelRelativeScale;
    AABB3 m_bounds;
    
    bool m_hasSDFData;
    uint32_t m_sdfResolution;
    float m_sdfScale;  
    
    bool m_hasCardTemplates;
    uint32_t m_cardTemplateCount;
    
    MeshCacheHeader() 
        : SaveFileHeader("MESH", 1)
        , m_vertexCount(0)
        , m_indexCount(0)
        , m_unitsPerMeter(1.0f)
        , m_modelRelativeScale(1.0f)
        , m_hasSDFData(false)
        , m_sdfResolution(0)
        , m_sdfScale(1.0f)
        , m_hasCardTemplates(false)
        , m_cardTemplateCount(0)
    {}
};
#pragma pack(pop)

class StaticMeshCache : public ISerializable
{
public:
    StaticMeshCache() = default;
    ~StaticMeshCache() override = default;

    void CaptureFromMesh(const StaticMesh* mesh, float scale = 1.0f);
    
    bool ApplyToMesh(StaticMesh* mesh, int meshID) const;

    void SaveToBinary(std::vector<uint8_t>& buffer) const override;
    bool LoadFromBinary(const std::vector<uint8_t>& buffer, size_t& offset) override;
    std::string GetSaveIdentifier() const override { return "StaticMeshCache_v1"; }

    bool IsValid() const { return !m_vertices.empty() && !m_indices.empty(); }
    
    static std::string GetCacheFileName(const std::string& originalPath);
    
    static bool IsCacheValid(const std::string& cachePath, const std::string& sourcePath);

private:
    MeshCacheHeader m_header;
    
    std::vector<Vertex_PCUTBN> m_vertices;
    std::vector<unsigned int> m_indices;
    
    std::vector<float> m_sdfData;  
    
    Mat44 m_transform;
    Mat44 m_transformWithoutAxisTransform;
    
    std::string m_x, m_y, m_z;
    
    std::string m_xmlPath;
    
    struct CardTemplateData
    {
        uint8_t m_direction;
        Vec2 m_localSize;
        IntVec2 m_recommendedResolution;
        uint32_t m_templateID;
    };
    std::vector<CardTemplateData> m_cardTemplates;
};