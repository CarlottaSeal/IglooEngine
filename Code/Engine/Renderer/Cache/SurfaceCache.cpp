#include "SurfaceCache.h"
#include "Engine/Core/EngineCommon.hpp"

void SurfaceCache::Initialize(ID3D12Device* device, uint32_t atlasSize, uint32_t tileSize)
{
    if (m_initialized)
        return;

    if (m_initialized) return;
    
    m_atlasSize = atlasSize;
    m_tileSize = tileSize;
    m_tilesPerRow = atlasSize / tileSize;
    m_maxCards = m_tilesPerRow * m_tilesPerRow;
    
    CreateAtlasTexture(device);
    CreateCardMetadataBuffer(device);

    CreateTemporaryCaptureResources(device, 256); //TODO: hardcode max resolution
    
    m_initialized = true;
}

void SurfaceCache::Shutdown()
{
    if (!m_initialized)
        return;
    
    m_stats = {};
    m_initialized = false;
    
    DX_SAFE_RELEASE(m_atlasTexture)
    DX_SAFE_RELEASE(m_cardMetadataBuffer)
    DX_SAFE_RELEASE(m_tileAllocator)
    DX_SAFE_RELEASE(m_cardMetadataUploadBuffer)
    
    for (int j= 0; j < SURFACE_CACHE_LAYER_CAPTURE_COUNT; j++)
    {
        DX_SAFE_RELEASE(m_tempCapture.m_tempTextures[j])
    }
}

ID3D12Resource* SurfaceCache::GetAtlasTexture() const
{
    return m_atlasTexture;
}

ID3D12Resource* SurfaceCache::GetMetadataBuffer() const
{
    return m_cardMetadataBuffer;
}

ID3D12Resource* SurfaceCache::GetMetadataUploadBuffer() const
{
    return m_cardMetadataUploadBuffer;
}

void SurfaceCache::CreateAtlasTexture(ID3D12Device* device)
{
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = m_atlasSize;
		desc.Height = m_atlasSize;
		desc.DepthOrArraySize = SURFACE_CACHE_LAYER_COUNT;  // align with GBuffer ->? No
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&m_atlasTexture)
		);

		GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Surface Cache atlas!");
        std::wstring name = std::wstring(L"SurfaceCacheAtlas");
		m_atlasTexture->SetName(name.c_str());
}

void SurfaceCache::CreateCardMetadataBuffer(ID3D12Device* device)
{
    size_t bufferSize = sizeof(SurfaceCardMetadata) * m_maxCards;
    
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_cardMetadataBuffer)
        );

        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create card metadata DEFAULT buffer!");
		std::wstring name = std::wstring(L"SurfaceCardeMetadataDefaultHeap");
        m_cardMetadataBuffer->SetName(name.c_str());


        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

        device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_cardMetadataUploadBuffer)
        );

        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create card metadata UPLOAD buffer!");
		std::wstring uploadName = std::wstring(L"SurfaceCacheAtlas");
        m_cardMetadataUploadBuffer->SetName(name.c_str());
}

void SurfaceCache::CreateTemporaryCaptureResources(ID3D12Device* device, uint32_t maxResolution)
{
    m_tempCapture.maxResolution = maxResolution;
    
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = maxResolution;
    desc.Height = maxResolution;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    float clearColor[4] = { 0, 0, 0, 0 };
    CD3DX12_CLEAR_VALUE clearValue(DXGI_FORMAT_R16G16B16A16_FLOAT, clearColor);
    
    for (int i = 0; i < SURFACE_CACHE_LAYER_CAPTURE_COUNT; i++)
    {
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&m_tempCapture.m_tempTextures[i])
        );
        
        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create temp texture!");
        
        const wchar_t* names[4] = { 
            L"TempAlbedo", 
            L"TempNormal", 
            L"TempMaterial", 
            L"TempDirectLight" 
        };
        m_tempCapture.m_tempTextures[i]->SetName(names[i]);
    }
}

void SurfaceCache::ReleaseTemporaryCaptureResources()
{
    for (int i = 0; i < 4; i++)
    {
        DX_SAFE_RELEASE(m_tempCapture.m_tempTextures[i]);
    }
}
