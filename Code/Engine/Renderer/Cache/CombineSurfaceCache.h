#pragma once
#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_DX12_RENDERER
#include <cstdint>
#include <d3d12.h>
#include "ThirdParty/d3dx12/d3dx12.h"
#include "Engine/Renderer/RenderCommon.h"

class SurfaceCache;


// 合并 Surface Cache 的 Direct Light 和 Indirect Light 层到 Combined 层
// Combined = Direct + Indirect * Albedo
//   - SURFCACHE_PRIMARY_META_SRV  (220) : CardMetadata buffer
//   - SURFCACHE_PRIMARY_ATLAS_UAV (221) : Atlas (读取和写入)
//=============================================================================
class CombineSurfaceCache
{
public:
    CombineSurfaceCache() = default;
    ~CombineSurfaceCache();
    
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap);
    void Shutdown();
    void Execute(ID3D12GraphicsCommandList* cmdList, SurfaceCache* surfaceCache, int globalActiveCardCount, uint32_t maxCardSize = 0);
    
    bool IsInitialized() const { return m_initialized; }
    
private:
    void CreateRootSignature();
    void CreatePipelineState();
    
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(int index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = 
            m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * m_descriptorSize;
        return handle;
    }
    
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(int index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = 
            m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += index * m_descriptorSize;
        return handle;
    }
    
private:
    ID3D12Device*              m_device = nullptr;
    ID3D12DescriptorHeap*      m_descriptorHeap = nullptr;
    ID3D12RootSignature*       m_rootSignature = nullptr;
    ID3D12PipelineState*       m_pso = nullptr;
    
    UINT m_descriptorSize = 0;
    bool m_initialized = false;
};

#endif // ENGINE_DX12_RENDERER