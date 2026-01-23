#pragma once
#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Math/Mat44.hpp"
#include "Engine/Renderer/RenderCommon.h"
#include <d3d12.h>
#include <cstdint>

class ConstantBuffer;
class SurfaceCache;

struct VisualizationConstants
{
    Mat44 ClipToRenderTransform;
    Mat44 RenderToCameraTransform;
    Mat44 CameraToWorldTransform;
    
    // 光照参数
    Mat44 LightWorldToCamera;
    Mat44 LightCameraToRender;
    Mat44 LightRenderToClip;
    
    float SunColor[4];
    float SunNormal[3];
    float ShadowMapSize;
    
    float AmbientColor[3];
    float AmbientIntensity;
    
    // 可视化参数
    uint32_t Mode;
    float Exposure;
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    
    // Screen Probe 参数
    uint32_t ProbeGridWidth;
    uint32_t ProbeGridHeight;
    uint32_t ProbeSpacing;
    uint32_t OctahedronSize;
    
    float DirectIntensity;
    float IndirectIntensity;
    float AOStrength;
    float Padding0;
    
    // Voxel 参数
    float VoxelGridMin[3];
    float VoxelSize;
    float VoxelGridMax[3];
    uint32_t VoxelResolution;
    
    // Global SDF 参数
    float GlobalSDFCenter[3];
    float GlobalSDFExtent;
    float GlobalSDFInvExtent[3];
    uint32_t GlobalSDFResolution;
    
    // Radiosity 参数
    uint32_t RadiosityProbeGridWidth;
    uint32_t RadiosityProbeGridHeight;
    uint32_t AtlasWidth;
    uint32_t AtlasHeight;
};

struct SurfaceCacheVizConstants
{
    Mat44 WorldToCameraTransform;
    Mat44 CameraToRenderTransform;
    Mat44 RenderToClipTransform;
    uint32_t SurfaceCacheLayer;
    uint32_t AtlasSize;
    uint32_t ActiveCardCount;
    float Exposure;
};

class GIVisualization
{
public:
    GIVisualization() = default;
    ~GIVisualization();
    
    void Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap,
                    uint32_t screenWidth, uint32_t screenHeight);
    void Shutdown();
    
    void Execute(ID3D12GraphicsCommandList* cmdList, 
                 ConstantBuffer* constantBuffer,
                 const GIVisualizationParams& params,
                 const CompositeConstants& compositeConsts,
                 const ScreenProbeConstants& screenProbeConsts,
                 const CameraConstants& cameraConsts,
                 SurfaceCache* surfaceCache,
                 uint32_t activeCardCount);
    
    // Fullscreen Compute 模式需要调用此方法将结果复制到目标
    // targetCurrentState: 目标当前的资源状态（默认 RENDER_TARGET）
    void CopyOutputToTarget(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* target,
                            D3D12_RESOURCE_STATES targetCurrentState = D3D12_RESOURCE_STATE_RENDER_TARGET);
    
    bool RenderImGuiPanel(GIVisualizationParams& params);
    
    bool IsInitialized() const { return m_initialized; }
    ID3D12Resource* GetOutputTexture() const { return m_outputTexture; }
    
    // 检查当前模式是否需要 Copy 到 BackBuffer（非 SurfaceCache 模式需要）
    static bool NeedsCopyToBackBuffer(GIVisualizationMode mode) { return !IsSurfaceCacheMode(mode); }
    
private:
    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateOutputTexture();
    void CreateSurfaceCacheResources();
    
    void ExecuteFullscreenCompute(ID3D12GraphicsCommandList* cmdList,
                                   ConstantBuffer* constantBuffer,
                                   const GIVisualizationParams& params,
                                   const CompositeConstants& compositeConsts,
                                   const ScreenProbeConstants& screenProbeConsts);
    
    void ExecuteSurfaceCacheVisualize(ID3D12GraphicsCommandList* cmdList,
                                       ConstantBuffer* constantBuffer,
                                       uint32_t layerIndex,
                                       float exposure,
                                       const CameraConstants& cameraConsts,
                                       SurfaceCache* surfaceCache,
                                       uint32_t activeCardCount);
    
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t slot);
    
private:
    ID3D12Device* m_device = nullptr;
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;
    uint32_t m_descriptorSize = 0;
    
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    
    // Fullscreen Compute 资源
    ID3D12RootSignature* m_fullscreenRootSig = nullptr;
    ID3D12PipelineState* m_fullscreenPSO = nullptr;
    
    // Surface Cache VS/PS 资源
    ID3D12RootSignature* m_surfaceCacheRootSig = nullptr;
    ID3D12PipelineState* m_surfaceCachePSO = nullptr;
    ID3D12Resource* m_quadVertexBuffer = nullptr;
    D3D12_VERTEX_BUFFER_VIEW m_quadVBView = {};
    
    // 输出纹理
    ID3D12Resource* m_outputTexture = nullptr;
    
    bool m_initialized = false;
};

#endif // ENGINE_DX12_RENDERER