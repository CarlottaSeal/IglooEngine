#pragma once

#include "Engine/Core/EngineCommon.hpp"
#ifdef ENGINE_DX12_RENDERER
#include <cstdint>
#include <d3d12.h>

class SurfaceCache;
struct SurfaceRadiosityConstants;

class SurfaceRadiosity
{
public:
    SurfaceRadiosity() = default;
    ~SurfaceRadiosity() = default;
    
    void Initialize(
        ID3D12Device* device,
        ID3D12DescriptorHeap* descriptorHeap,
        uint32_t atlasWidth,
        uint32_t atlasHeight);
    void Execute(
        ID3D12GraphicsCommandList* cmdList,
        ConstantBuffer* constantBuffer,
        const SurfaceRadiosityConstants& constants, SurfaceCache* surfaceCache);

    void Shutdown();
    
    ID3D12Resource* GetRadiosityTraceResult() const { return m_radiosityTraceResult; }
    ID3D12Resource* GetRadiosityHistory() const { return m_radiosityHistory; }
    ID3D12Resource* GetRadiosityFiltered() const { return m_radiosityFiltered; }
    ID3D12Resource* GetRadiositySH_R() const { return m_radiositySH_R; }
    ID3D12Resource* GetRadiositySH_G() const { return m_radiositySH_G; }
    ID3D12Resource* GetRadiositySH_B() const { return m_radiositySH_B; }
    
private:
    ID3D12Device* m_device = nullptr;
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;
    UINT m_scuDescriptorSize;
    uint32_t m_atlasWidth = 4096;
    uint32_t m_atlasHeight = 4096;
    uint32_t m_probeGridWidth = 1024;
    uint32_t m_probeGridHeight = 1024;

    ID3D12GraphicsCommandList* m_cmdList = nullptr;
    ConstantBuffer* m_constantBuffer = nullptr;
    
    ID3D12RootSignature* m_rootSignature = nullptr;
    ID3D12PipelineState* m_radiosityTracePSO = nullptr;
    ID3D12PipelineState* m_radiosityFilterPSO = nullptr;
    ID3D12PipelineState* m_convertToSHPSO = nullptr;
    ID3D12PipelineState* m_integrateSHPSO = nullptr;
    ID3D12PipelineState* m_combineLightPSO = nullptr;  // Multi-bounce: Direct + Indirect → Combined
    
    ID3D12Resource* m_radiosityTraceResult = nullptr;   // Pass 5.1 输出
    ID3D12Resource* m_radiosityHistory = nullptr;       // 时间累积
    ID3D12Resource* m_radiosityFiltered = nullptr;      // Pass 5.2 输出
    
    ID3D12Resource* m_radiositySH_R = nullptr;          // Pass 5.3 输出 (SH R 通道)
    ID3D12Resource* m_radiositySH_G = nullptr;          // Pass 5.3 输出 (SH G 通道)
    ID3D12Resource* m_radiositySH_B = nullptr;          // Pass 5.3 输出 (SH B 通道)
    
    ID3D12Resource* m_probeDepth = nullptr;             // 降采样的深度
    ID3D12Resource* m_probeNormal = nullptr;            // 降采样的法线
    
    bool m_firstFrame = true;
    
private:
    void Pass_RadiosityTrace(const SurfaceRadiosityConstants& constants);
    void Pass_RadiosityFilter(const SurfaceRadiosityConstants& constants);
    void Pass_ConvertToSH(const SurfaceRadiosityConstants& constants);
    void ExecuteIntegrateSH(const SurfaceRadiosityConstants& constants);
    void Pass_CombineLight(const SurfaceRadiosityConstants& constants, SurfaceCache* surfaceCache);  // Multi-bounce
    
    void CreateRadiosityBuffers();
    void CreateRootSignature();
    void CreatePipelineStates();
    
    void CreateTexture2D(
        ID3D12Resource*& resource,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format);
    
    void CreateTexture2DUAV(ID3D12Resource* resource, int descriptorIndex);
    void CreateTexture2DSRV(ID3D12Resource* resource, int descriptorIndex);
};

#endif