#pragma once
#include "SurfaceCache.h"
#include "Engine/Core/EngineCommon.hpp"
#ifdef ENGINE_DX12_RENDERER
#include <cstdint>
#include <d3d12.h>

struct GBufferData;
static constexpr uint32_t SCREEN_PROBE_SPACING = 8;         // 每 8×8 屏幕像素一个 Probe
static constexpr uint32_t SCREEN_PROBE_RAYS = 64;           // 每 Probe 64 条光线
static constexpr float MESH_SDF_TRACE_DISTANCE = 100.0f;    // Mesh SDF 追踪距离
static constexpr float VOXEL_TRACE_DISTANCE = 500.0f;       // Voxel 追踪距离

static constexpr uint32_t OCTAHEDRON_SIZE = 8;              // 8×8 八面体贴图
static constexpr uint32_t OCTAHEDRON_BORDER = 1;            // 1 像素边界
static constexpr uint32_t BORDERED_OCTAHEDRON_SIZE = OCTAHEDRON_SIZE + OCTAHEDRON_BORDER * 2; // 10×10

class ScreenProbeFinalGather
{
public:
    ScreenProbeFinalGather() = default;
    ~ScreenProbeFinalGather();
    
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap, uint32_t screenWidth, uint32_t screenHeight);
    
    void Execute(ID3D12GraphicsCommandList* cmdList, ConstantBuffer* constantBuffer, const ScreenProbeConstants& constants
                 , D3D12_GPU_DESCRIPTOR_HANDLE sdfTexturesHandle, GBufferData* gBuffer, SurfaceCache* surfaceCache);
    
    void Shutdown();
    
    // Getters
    ID3D12Resource* GetIndirectLightingTexture() const { return m_screenIndirectLighting; }
    ID3D12Resource* GetFilteredIndirectLightingTexture() const { return m_prevScreenRadiance; }  // 时间滤波后的结果
    bool IsInitialized() const { return m_initialized; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(int index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = 
            m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * m_scuDescriptorSize;
        return handle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(int index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = 
            m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += index * m_scuDescriptorSize;
        return handle;
    }
    bool GetUseHistoryB() {return m_useHistoryB;}
    
private:
    void CreateResources();
    void CreateRootSignature(); 
    //  创建 + SRV + UAV
    void CreateBuffer(
        ID3D12Resource** outResource,
        uint32_t elementCount,
        uint32_t elementSize,
        int srvSlot,
        int uavSlot,
        const wchar_t* debugName
    );
    // Texture2D 创建 + SRV + UAV
    void CreateTexture2D(
        ID3D12Resource** outResource,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format,
        int srvSlot,
        int uavSlot,
        const wchar_t* debugName
    );
    ID3D12PipelineState* GetOrCreatePSO(
        ID3D12PipelineState** psoPtr,
        const char* shaderFile,
        const char* entryPoint,
        const wchar_t* debugName
    );
    void BindGBufferToDescriptorHeap(GBufferData* gBuffer);
    
    void Pass01_ProbePlacement(const ScreenProbeConstants& c);
    void Pass02_BRDFPDFGeneration(const ScreenProbeConstants& c);
    void Pass03_LightingPDFGeneration(const ScreenProbeConstants& c);
    void Pass04_GenerateSampleDirections(const ScreenProbeConstants& c);
    void Pass05_MeshSDFTrace(const ScreenProbeConstants& c);
    void Pass06_VoxelSDFTrace(const ScreenProbeConstants& c);
    void Pass07_RadianceComposite(const ScreenProbeConstants& c);
    void Pass08_TemporalAccumulation(const ScreenProbeConstants& c);
    void Pass09_SpatialFilter(const ScreenProbeConstants& c);
    void Pass10_FinalGather(const ScreenProbeConstants& c);
    void Pass11_ScreenSpaceTemporalFilter(const ScreenProbeConstants& c);  // 屏幕空间时间重投影
    
private:
    ID3D12Device* m_device = nullptr;
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;
    ID3D12RootSignature* m_computeRootSignature = nullptr;
    UINT m_scuDescriptorSize;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_probeGridWidth = 0;
    uint32_t m_probeGridHeight = 0;
    bool m_initialized = false;

    // 执行时的临时引用（只在 Execute 期间有效）
    ID3D12GraphicsCommandList* m_commandList = nullptr;
    ConstantBuffer* m_constantBuffer = nullptr;
     
    ID3D12Resource* m_probeBuffer = nullptr;
    ID3D12Resource* m_brdfPdfBuffer = nullptr;
    ID3D12Resource* m_lightingPdfBuffer = nullptr;
    ID3D12Resource* m_sampleDirectionsBuffer = nullptr;
    ID3D12Resource* m_meshTraceResultBuffer = nullptr;
    ID3D12Resource* m_voxelTraceResultBuffer = nullptr;
    
    ID3D12Resource* m_probeRadiance = nullptr;
    
    ID3D12Resource* m_probeRadianceHistoryA = nullptr;
    ID3D12Resource* m_probeRadianceHistoryB = nullptr;
    bool m_useHistoryB = false; // false = 使用A, true = 使用B
    
    ID3D12Resource* m_probeRadianceFiltered = nullptr;
    
    ID3D12Resource* m_octSH_R = nullptr;
    ID3D12Resource* m_octSH_G = nullptr;
    ID3D12Resource* m_octSH_B = nullptr;
    
    ID3D12Resource* m_screenIndirectLighting = nullptr;      // 最终滤波后输出
    ID3D12Resource* m_screenIndirectRaw = nullptr;           // FinalGather 原始输出 (时间滤波前)
    ID3D12Resource* m_prevScreenRadiance = nullptr;          // 上一帧滤波结果 (历史)
    
    ID3D12PipelineState* m_pso_ProbePlacement = nullptr;
    ID3D12PipelineState* m_pso_BRDFPDFGeneration = nullptr;
    ID3D12PipelineState* m_pso_LightingPDFGeneration = nullptr;
    ID3D12PipelineState* m_pso_GenerateSampleDirections = nullptr;
    ID3D12PipelineState* m_pso_MeshSDFTrace = nullptr;
    ID3D12PipelineState* m_pso_VoxelSDFTrace = nullptr;
    ID3D12PipelineState* m_pso_RadianceComposite = nullptr;
    ID3D12PipelineState* m_pso_TemporalAccumulation = nullptr;
    ID3D12PipelineState* m_pso_SpatialFilter = nullptr;
    ID3D12PipelineState* m_pso_FinalGather = nullptr;
    ID3D12PipelineState* m_pso_ScreenSpaceTemporalFilter = nullptr;  // 屏幕空间时间重投影
};

#endif