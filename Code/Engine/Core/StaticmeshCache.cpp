#include "StaticmeshCache.h"
#include "Engine/Core/StaticMesh.h"
#include "Engine/Renderer/SDFTexture3D.h"
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Engine/Renderer/Cache/SurfaceCard.h"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/XmlUtils.hpp"
#include "Engine/Core/StaticMeshUtils.h"
#include "Engine/Core/Image.hpp"
#include <filesystem>

extern Renderer* g_theRenderer;

#ifdef ENGINE_DX12_RENDERER
void StaticMeshCache::CaptureFromMesh(const StaticMesh* mesh, float sdfScale)
{
    if (!mesh)
    {
        ERROR_RECOVERABLE("CaptureFromMesh: mesh is null");
        return;
    }
    m_header.m_vertexCount = static_cast<uint32_t>(mesh->m_verts.size());
    m_header.m_indexCount = static_cast<uint32_t>(mesh->m_indices.size());
    m_header.m_unitsPerMeter = mesh->m_unitsPerMeter;
    m_header.m_modelRelativeScale = mesh->m_modelRelativeScale;
    m_header.m_bounds = mesh->GetAABB3Bounds();

    m_vertices = mesh->m_verts;
    m_indices = mesh->m_indices;
    
    m_transform = mesh->m_transform;
    m_transformWithoutAxisTransform = mesh->m_transformWithoutAxisTransform;
    
    m_x = mesh->m_x;
    m_y = mesh->m_y;
    m_z = mesh->m_z;
    
    m_xmlPath = mesh->m_filePath;

    if (mesh->m_hasCardTemplates)
    {
        m_header.m_hasCardTemplates = true;
        m_header.m_cardTemplateCount = static_cast<uint32_t>(mesh->m_cardTemplates.size());
        
        m_cardTemplates.clear();
        m_cardTemplates.reserve(mesh->m_cardTemplates.size());
        
        for (const auto& templ : mesh->m_cardTemplates)
        {
            CardTemplateData data;
            data.m_direction = templ.m_direction;
            data.m_localSize = templ.m_localSize;
            data.m_recommendedResolution = templ.m_recommendedResolution;
            data.m_templateID = templ.m_templateID;
            m_cardTemplates.push_back(data);
        }
    }
    
    SDFTexture3D* sdf = mesh->GetSDF(sdfScale);
    if (sdf && g_theRenderer)
    {
        m_sdfData = g_theRenderer->GetSubRenderer()->ReadbackSDF3DData(sdf);
        
        if (!m_sdfData.empty())
        {
            m_header.m_hasSDFData = true;
            m_header.m_sdfResolution = sdf->GetResolution();
            m_header.m_sdfScale = sdfScale;
        }
    }
    else
    {
        m_header.m_hasSDFData = false;
        m_sdfData.clear();
    }
    // DebuggerPrintf("[MeshCache] Capture complete: %u vertices, %u indices, %s SDF, xmlPath=%s\n",
    //                m_header.m_vertexCount, m_header.m_indexCount,
    //                m_header.m_hasSDFData ? "with" : "without", m_xmlPath.c_str());
}

bool StaticMeshCache::ApplyToMesh(StaticMesh* mesh, int meshID) const
{
    if (!mesh)
    {
        ERROR_RECOVERABLE("ApplyToMesh: mesh is null");
        return false;
    }
    if (!IsValid())
    {
        ERROR_RECOVERABLE("ApplyToMesh: cache is invalid");
        return false;
    }
    mesh->m_verts = m_vertices;
    mesh->m_indices = m_indices;
    
    mesh->m_transform = m_transform;
    mesh->m_transformWithoutAxisTransform = m_transformWithoutAxisTransform;
    mesh->m_unitsPerMeter = m_header.m_unitsPerMeter;
    mesh->m_modelRelativeScale = m_header.m_modelRelativeScale;
    
    mesh->m_x = m_x;
    mesh->m_y = m_y;
    mesh->m_z = m_z;
    
    mesh->m_filePath = m_xmlPath;

    if (m_header.m_hasCardTemplates && !m_cardTemplates.empty())
    {
        mesh->m_cardTemplates.clear();
        mesh->m_cardTemplates.reserve(m_cardTemplates.size());
        
        for (const auto& data : m_cardTemplates)
        {
            SurfaceCardTemplate templ;
            templ.m_direction = data.m_direction;
            templ.m_localSize = data.m_localSize;
            templ.m_recommendedResolution = data.m_recommendedResolution;
            templ.m_templateID = data.m_templateID;
            mesh->m_cardTemplates.push_back(templ);
        }
        
        mesh->m_hasCardTemplates = true;
        DebuggerPrintf("[MeshCache] Restored %zu card templates\n", m_cardTemplates.size());
    }

    // 创建GPU缓冲区
    if (g_theRenderer && g_theRenderer->GetSubRenderer())
    {
        DX12Renderer* dx12Renderer = g_theRenderer->GetSubRenderer();
        
        mesh->m_vertexBuffer = dx12Renderer->CreateVertexBuffer(
            static_cast<unsigned int>(m_vertices.size() * sizeof(Vertex_PCUTBN)),
            sizeof(Vertex_PCUTBN)
        );
        dx12Renderer->CopyCPUToGPU(
            m_vertices.data(),
            static_cast<unsigned int>(m_vertices.size() * sizeof(Vertex_PCUTBN)),
            mesh->m_vertexBuffer
        );
        
        mesh->m_indexBuffer = dx12Renderer->CreateIndexBuffer(
            static_cast<unsigned int>(m_indices.size() * sizeof(unsigned int)),
            sizeof(unsigned int)
        );
        dx12Renderer->CopyCPUToGPU(
            m_indices.data(),
            static_cast<unsigned int>(m_indices.size() * sizeof(unsigned int)),
            mesh->m_indexBuffer
        );
        
        if (m_header.m_hasSDFData && !m_sdfData.empty())
        {
            SDFTexture3D* sdf = dx12Renderer->CreateSDFTextureFromData(m_sdfData, m_header.m_sdfResolution);
            
            if (sdf)
            {
                mesh->SetSDF(m_header.m_sdfScale, sdf, meshID);
                // DebuggerPrintf("[MeshCache] Restored SDF: resolution=%d, scale=%.2f\n",
                //                m_header.m_sdfResolution, m_header.m_sdfScale);
            }
            else
            {
                //DebuggerPrintf("[MeshCache] Warning: Failed to create SDF from cached data\n");
            }
        }
    }
    if (g_theRenderer && !m_xmlPath.empty())
    {
        std::string xmlPathNoExtensions = m_xmlPath;
        size_t lastDot = xmlPathNoExtensions.find_last_of('.');
        if (lastDot != std::string::npos)
        {
            xmlPathNoExtensions = xmlPathNoExtensions.substr(0, lastDot);
        }
        XmlDocument meshDefDoc;
        std::string xmlFile = xmlPathNoExtensions + ".xml";
        XmlResult result = meshDefDoc.LoadFile(xmlFile.c_str());
        
        if (result == XmlResult::XML_SUCCESS)
        {
            XmlElement* meshElement = meshDefDoc.RootElement();
            
            std::string normalTexture = ParseXmlAttribute(*meshElement, "normalMap", "");
            std::string diffuseTexture = ParseXmlAttribute(*meshElement, "diffuseMap", "");
            std::string specularTexture = ParseXmlAttribute(*meshElement, "specGlossEmitMap", "");
            std::string shaderName = ParseXmlAttribute(*meshElement, "shader", "");
            std::string mtlPath = ParseXmlAttribute(*meshElement, "mtllib", "");
            
            if (diffuseTexture.empty() && !mtlPath.empty())
            {
                std::map<std::string, MtlInfo> materials;
                if (LoadOBJMaterial(mtlPath, materials))
                {
                    std::string materialName = ParseXmlAttribute(*meshElement, "material", "");
                    
                    MtlInfo* mtlInfo = nullptr;
                    if (!materialName.empty())
                    {
                        auto it = materials.find(materialName);
                        if (it != materials.end())
                        {
                            mtlInfo = &it->second;
                        }
                        else if (!materials.empty())
                        {
                            mtlInfo = &materials.begin()->second;
                        }
                    }
                    else if (!materials.empty())
                    {
                        mtlInfo = &materials.begin()->second;
                    }
                    
                    if (mtlInfo)
                    {
                        if (diffuseTexture.empty() && !mtlInfo->m_diffuseMap.empty())
                            diffuseTexture = mtlInfo->m_diffuseMap;
                        if (normalTexture.empty() && !mtlInfo->m_normalMap.empty())
                            normalTexture = mtlInfo->m_normalMap;
                        if (specularTexture.empty() && !mtlInfo->m_specularMap.empty())
                            specularTexture = mtlInfo->m_specularMap;
                    }
                }
            }
            
            if (!normalTexture.empty())
                mesh->m_normalTexture = g_theRenderer->CreateTextureFromFile(normalTexture.c_str());
            if (!diffuseTexture.empty())
                mesh->m_diffuseTexture = g_theRenderer->CreateTextureFromFile(diffuseTexture.c_str());
            if (!specularTexture.empty())
                mesh->m_specularTexture = g_theRenderer->CreateTextureFromFile(specularTexture.c_str());
            if (!shaderName.empty())
                mesh->m_shader = g_theRenderer->CreateOrGetShader(shaderName.c_str(), VertexType::VERTEX_PCUTBN);

            if (EndsWith(m_xmlPath, ".glb"))
            {
                Image* diffuseI = LoadImageDueToGLTFData(LoadGLTFDataFromFile(m_xmlPath), GLBChannel::Albedo, m_xmlPath);
                if (diffuseI)
                {
                    diffuseI->SetName(xmlPathNoExtensions + "_diffuse");
                    mesh->m_diffuseTexture = g_theRenderer->CreateTextureFromImage(*diffuseI);
#ifdef ENGINE_DX12_RENDERER
                    g_theRenderer->GetSubRenderer()->PushBackNewTextureManually(mesh->m_diffuseTexture);
#endif
                }
                Image* normalI = LoadImageDueToGLTFData(LoadGLTFDataFromFile(m_xmlPath), GLBChannel::Normal, m_xmlPath);
                if (normalI)
                {
                    normalI->SetName(xmlPathNoExtensions + "_normal");
                    mesh->m_normalTexture = g_theRenderer->CreateTextureFromImage(*normalI);
#ifdef ENGINE_DX12_RENDERER
                    g_theRenderer->GetSubRenderer()->PushBackNewTextureManually(mesh->m_normalTexture);
#endif
                }
                Image* specI = LoadImageDueToGLTFData(LoadGLTFDataFromFile(m_xmlPath), GLBChannel::AO, m_xmlPath);
                if (specI)
                {
                    specI->SetName(xmlPathNoExtensions + "_ao");
                    mesh->m_specularTexture = g_theRenderer->CreateTextureFromImage(*specI);
#ifdef ENGINE_DX12_RENDERER
                    g_theRenderer->GetSubRenderer()->PushBackNewTextureManually(mesh->m_specularTexture);
#endif
                }
            }
        }
    }
    return true;
}

void StaticMeshCache::SaveToBinary(std::vector<uint8_t>& buffer) const
{
    //size_t startSize = buffer.size();

    size_t headerOffset = buffer.size();
    buffer.resize(buffer.size() + sizeof(MeshCacheHeader));
    memcpy(buffer.data() + headerOffset, &m_header, sizeof(MeshCacheHeader));

    SaveToBinaryWithRLE(buffer, 
        reinterpret_cast<const uint8_t*>(m_vertices.data()),
        m_vertices.size() * sizeof(Vertex_PCUTBN));

    SaveToBinaryWithRLE(buffer,
        reinterpret_cast<const uint8_t*>(m_indices.data()),
        m_indices.size() * sizeof(unsigned int));

    size_t transformOffset = buffer.size();
    buffer.resize(buffer.size() + sizeof(Mat44) * 2);
    memcpy(buffer.data() + transformOffset, &m_transform, sizeof(Mat44));
    memcpy(buffer.data() + transformOffset + sizeof(Mat44), 
           &m_transformWithoutAxisTransform, sizeof(Mat44));

    auto writeString = [&buffer](const std::string& str) {
        uint32_t len = static_cast<uint32_t>(str.length());
        size_t offset = buffer.size();
        buffer.resize(buffer.size() + sizeof(uint32_t) + len);
        memcpy(buffer.data() + offset, &len, sizeof(uint32_t));
        if (len > 0)
            memcpy(buffer.data() + offset + sizeof(uint32_t), str.data(), len);
    };
    
    writeString(m_x);
    writeString(m_y);
    writeString(m_z);
    
    writeString(m_xmlPath);

    if (m_header.m_hasCardTemplates && !m_cardTemplates.empty())
    {
        size_t cardOffset = buffer.size();
        size_t cardDataSize = m_cardTemplates.size() * sizeof(CardTemplateData);
        buffer.resize(buffer.size() + cardDataSize);
        memcpy(buffer.data() + cardOffset, m_cardTemplates.data(), cardDataSize);
    }

    if (m_header.m_hasSDFData && !m_sdfData.empty())
    {
        SaveToBinaryWithRLE(buffer,
            reinterpret_cast<const uint8_t*>(m_sdfData.data()),
            m_sdfData.size() * sizeof(float));
    }
    //meshcache debug log
    // size_t compressedSize = buffer.size() - startSize;
    // size_t uncompressedSize = m_vertices.size() * sizeof(Vertex_PCUTBN) + 
    //                           m_indices.size() * sizeof(unsigned int) +
    //                           m_sdfData.size() * sizeof(float);
    // DebuggerPrintf("[MeshCache] Saved %zu bytes (original ~%zu bytes, %.1f%% compression)\n",
    //     compressedSize, uncompressedSize, 
    //     uncompressedSize > 0 ? (100.0f * compressedSize / uncompressedSize) : 0.0f);
}

bool StaticMeshCache::LoadFromBinary(const std::vector<uint8_t>& buffer, size_t& offset)
{
    if (offset + sizeof(MeshCacheHeader) > buffer.size())
    {
        ERROR_RECOVERABLE("LoadFromBinary: Buffer too small for header");
        return false;
    }

    memcpy(&m_header, buffer.data() + offset, sizeof(MeshCacheHeader));
    offset += sizeof(MeshCacheHeader);

    if (!m_header.Validate("MESH", 1))
    {
        ERROR_RECOVERABLE("LoadFromBinary: Invalid mesh cache header");
        return false;
    }

    m_vertices.resize(m_header.m_vertexCount);
    if (!LoadFromBinaryWithRLE(buffer, offset,
        reinterpret_cast<uint8_t*>(m_vertices.data()),
        m_vertices.size() * sizeof(Vertex_PCUTBN)))
    {
        ERROR_RECOVERABLE("LoadFromBinary: Failed to load vertex data");
        return false;
    }

    m_indices.resize(m_header.m_indexCount);
    if (!LoadFromBinaryWithRLE(buffer, offset,
        reinterpret_cast<uint8_t*>(m_indices.data()),
        m_indices.size() * sizeof(unsigned int)))
    {
        ERROR_RECOVERABLE("LoadFromBinary: Failed to load index data");
        return false;
    }

    if (offset + sizeof(Mat44) * 2 > buffer.size())
    {
        ERROR_RECOVERABLE("LoadFromBinary: Buffer too small for transforms");
        return false;
    }

    memcpy(&m_transform, buffer.data() + offset, sizeof(Mat44));
    offset += sizeof(Mat44);
    memcpy(&m_transformWithoutAxisTransform, buffer.data() + offset, sizeof(Mat44));
    offset += sizeof(Mat44);

    auto readString = [&buffer, &offset](std::string& str) -> bool {
        if (offset + sizeof(uint32_t) > buffer.size())
            return false;
        
        uint32_t len;
        memcpy(&len, buffer.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        if (len > 0)
        {
            if (offset + len > buffer.size())
                return false;
            str.resize(len);
            memcpy(&str[0], buffer.data() + offset, len);
            offset += len;
        }
        else
        {
            str.clear();
        }
        return true;
    };
    
    if (!readString(m_x) || !readString(m_y) || !readString(m_z))
    {
        ERROR_RECOVERABLE("LoadFromBinary: Failed to load axis info");
        return false;
    }
    
    if (!readString(m_xmlPath))
    {
        ERROR_RECOVERABLE("LoadFromBinary: Failed to load xmlPath");
        return false;
    }

    if (m_header.m_hasCardTemplates && m_header.m_cardTemplateCount > 0)
    {
        m_cardTemplates.resize(m_header.m_cardTemplateCount);
        size_t cardDataSize = m_cardTemplates.size() * sizeof(CardTemplateData);
        
        if (offset + cardDataSize > buffer.size())
        {
            ERROR_RECOVERABLE("LoadFromBinary: Buffer too small for card templates");
            return false;
        }
        
        memcpy(m_cardTemplates.data(), buffer.data() + offset, cardDataSize);
        offset += cardDataSize;
    }

    if (m_header.m_hasSDFData && m_header.m_sdfResolution > 0)
    {
        int res = m_header.m_sdfResolution;
        m_sdfData.resize(res * res * res);
        
        if (!LoadFromBinaryWithRLE(buffer, offset,
            reinterpret_cast<uint8_t*>(m_sdfData.data()),
            m_sdfData.size() * sizeof(float)))
        {
            ERROR_RECOVERABLE("LoadFromBinary: Failed to load SDF data");
            m_sdfData.clear();
            m_header.m_hasSDFData = false;
        }
    }
    
    // DebuggerPrintf("[MeshCache] Loaded: %u vertices, %u indices, %u templates, %s SDF, xmlPath=%s\n",
    //     m_header.m_vertexCount, m_header.m_indexCount, m_header.m_cardTemplateCount,
    //     m_header.m_hasSDFData ? "with" : "without", m_xmlPath.c_str());
    return true;
}

std::string StaticMeshCache::GetCacheFileName(const std::string& originalPath)
{
    std::string filename = originalPath;
    
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        filename = filename.substr(lastSlash + 1);
    }
    
    for (char& c : filename)
    {
        if (c == '.')
            c = '_';
    }
    
    return filename + ".mcache";
}

bool StaticMeshCache::IsCacheValid(const std::string& cachePath, const std::string& sourcePath)
{
    namespace fs = std::filesystem;
    
    if (!fs::exists(cachePath))
        return false;
    
    if (!fs::exists(sourcePath))
        return false;
    
    auto cacheTime = fs::last_write_time(cachePath);
    auto sourceTime = fs::last_write_time(sourcePath);
    
    return cacheTime >= sourceTime;
}

#endif