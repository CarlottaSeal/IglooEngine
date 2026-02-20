#pragma once
#include "Engine/Core/EngineCommon.hpp"
#include <string>

struct ID3D12Resource;

class SDFTexture3D
{
    friend class DX12Renderer;

public:
    int         GetResolution()     const { return m_resolution; }
    int         GetSRVDescriptorIndex()const { return m_srvHeapIndex; }
    int         GetSDFTextureIndex()const { return m_sdfTextureIndex; }
#ifdef ENGINE_DX12_RENDERER
    ID3D12Resource* GetResource()   const { return m_sdfTexture3D; }
#endif

    ~SDFTexture3D();
    
    SDFTexture3D(const SDFTexture3D&)            = delete;
    SDFTexture3D& operator=(const SDFTexture3D&) = delete;

private:
    SDFTexture3D(int resolution);

private:
#ifdef ENGINE_DX12_RENDERER
    ID3D12Resource* m_sdfTexture3D;
    std::vector<ID3D12Resource*> m_tempBuffers;
#endif
    int         m_srvHeapIndex  = -1;
    int         m_resolution    = 64;
    int         m_sdfTextureIndex = -1;
    std::string m_name;
};