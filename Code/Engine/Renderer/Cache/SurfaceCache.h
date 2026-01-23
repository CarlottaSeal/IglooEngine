#pragma once
#include <cstdint>
#include <d3d12.h>
#include "ThirdParty/d3dx12/d3dx12.h"

#include "Engine/Renderer/RenderCommon.h"
//#include "Engine/Renderer/DX12Renderer.hpp"

struct GBufferData;

struct TemporaryCaptureResources
{
	ID3D12Resource* m_tempTextures[SURFACE_CACHE_LAYER_CAPTURE_COUNT];      // 4个临时纹理
	D3D12_CPU_DESCRIPTOR_HANDLE m_tempRtvs[SURFACE_CACHE_LAYER_CAPTURE_COUNT];  // 对应的 RTV 在dx12中管理->我不知道好不好
	uint32_t maxResolution;               // 最大分辨率
};

class SurfaceCache
{
    friend class GISystem;
public:
    SurfaceCache() = default;
    ~SurfaceCache() = default;

    static constexpr uint32_t INVALID_TILE_INDEX = 0xFFFFFFFF;
    
    void Initialize(ID3D12Device* device, uint32_t atlasSize, uint32_t tileSize);
    void Shutdown();
	
	ID3D12Resource* GetAtlasTexture() const;
	ID3D12Resource* GetMetadataBuffer() const;
	ID3D12Resource* GetMetadataUploadBuffer() const;

    const SurfaceCacheStats& GetStats() const { return m_stats; }
    void ResetStats() { m_stats = {}; }

private:
    void CreateAtlasTexture(ID3D12Device* device);
    void CreateCardMetadataBuffer(ID3D12Device* device);
    void CreateCardAllocator(ID3D12Device* device);
	
	void CreateTemporaryCaptureResources(ID3D12Device* device, uint32_t maxResolution);
	void ReleaseTemporaryCaptureResources();

public:
	TemporaryCaptureResources m_tempCapture;
    ID3D12Resource* m_atlasTexture;
    ID3D12Resource* m_cardMetadataBuffer;
    ID3D12Resource* m_cardMetadataUploadBuffer; // UPLOAD heap（CPU 写入的中转
    
    ID3D12Resource* m_tileAllocator;

    uint32_t m_atlasSize = 0;
    uint32_t m_tileSize = 0;
    uint32_t m_tilesPerRow = 0;
    uint32_t m_maxCards = 0;
    
    SurfaceCacheStats m_stats;
    
    bool m_initialized = false;
};

