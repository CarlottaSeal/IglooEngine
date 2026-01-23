#include "SurfaceRadiosityCache.h"

#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Renderer/RenderCommon.h"

#ifdef ENGINE_DX12_RENDERER
#include "SurfaceCache.h"
#include "CacheCommon.h"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/HelperFunctionLib.h"
#include "ThirdParty/d3dx12/d3dx12.h"

void SurfaceRadiosity::Initialize(
    ID3D12Device* device,
    ID3D12DescriptorHeap* descriptorHeap,
    uint32_t atlasWidth,
    uint32_t atlasHeight)
{
    m_device = device;
    m_descriptorHeap = descriptorHeap;
    m_scuDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;
    
    // 计算 Probe Grid 尺寸
    m_probeGridWidth = atlasWidth / RadiosityConfig::PROBE_SPACING;
    m_probeGridHeight = atlasHeight / RadiosityConfig::PROBE_SPACING;
    
    CreateRadiosityBuffers();
    CreateRootSignature();
    CreatePipelineStates();
    
    DebuggerPrintf("[SurfaceRadiosity] Initialized: %u×%u Probes\n", 
                   m_probeGridWidth, m_probeGridHeight);
}

void SurfaceRadiosity::Execute(ID3D12GraphicsCommandList* cmdList, ConstantBuffer* constantBuffer,
                               const SurfaceRadiosityConstants& constants, SurfaceCache* surfaceCache)
{
    m_cmdList = cmdList;
    m_constantBuffer = constantBuffer;
    //DebuggerPrintf("[SurfaceRadiosity] Execute: Frame %u\n", constants.FrameIndex);
    
    // CD3DX12_RESOURCE_BARRIER atlasBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    //     surfaceCache->GetAtlasTexture(),
    //     D3D12_RESOURCE_STATE_COMMON,
    //     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    //     D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES  
    // );
    // CD3DX12_RESOURCE_BARRIER metadataBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
    //     surfaceCache->GetMetadataBuffer(),
    //     D3D12_RESOURCE_STATE_COMMON,
    //     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    // );
    // CD3DX12_RESOURCE_BARRIER preRadiosityBarriers[2] = { atlasBarrier, metadataBarrier };
    // m_cmdList->ResourceBarrier(2, preRadiosityBarriers);

    CD3DX12_RESOURCE_BARRIER barrierS = CD3DX12_RESOURCE_BARRIER::Transition(
    surfaceCache->GetAtlasTexture(), 
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,  
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    SURFACE_CACHE_LAYER_INDIRECT_LIGHT  
);
    m_cmdList->ResourceBarrier(1, &barrierS);

    
    m_cmdList->SetComputeRootSignature(m_rootSignature);
    // CBV [0]
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer->GetDX12ConstantBuffer()->GetGPUVirtualAddress();
    m_cmdList->SetComputeRootConstantBufferView(0, cbAddress);
    // [1] Surface Cache SRVs (t0-t5)
    D3D12_GPU_DESCRIPTOR_HANDLE surfaceCacheSrvHandle = 
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, SURFCACHE_PRIMARY_ATLAS_SRV);
    m_cmdList->SetComputeRootDescriptorTable(1, surfaceCacheSrvHandle);
    // [2] Global SDF + Voxel Lighting (t10-t11)
    D3D12_GPU_DESCRIPTOR_HANDLE globalResourcesHandle = 
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, GLOBAL_SDF_SRV);
    m_cmdList->SetComputeRootDescriptorTable(2, globalResourcesHandle);
    // [3] Radiosity SRVs (t20-t27)
    D3D12_GPU_DESCRIPTOR_HANDLE radiositySrvHandle = 
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, RADIOSITY_SRV_BASE);
    m_cmdList->SetComputeRootDescriptorTable(3, radiositySrvHandle);
    // [4] Radiosity UAVs (u0-u7)
    D3D12_GPU_DESCRIPTOR_HANDLE radiosityUavHandle = 
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, RADIOSITY_UAV_BASE);
    m_cmdList->SetComputeRootDescriptorTable(4, radiosityUavHandle);
    
    // Pass 1: RadiosityTrace
    // Input:  History (t21), SurfaceCache (t0-t1), GlobalSDF (t10), VoxelLighting (t11)
    // Output: TraceResult (u0)
    Pass_RadiosityTrace(constants);
    
    // UAV Barrier + 状态转换
    DX12Helper::UAVBarrier(m_cmdList, m_radiosityTraceResult);
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_radiosityTraceResult,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    m_cmdList->ResourceBarrier(1, &barrier);
    
    // Pass 2: RadiosityFilter
    // Input:  TraceResult (t20)
    // Output: Filtered (u2)
    Pass_RadiosityFilter(constants);
    DX12Helper::UAVBarrier(m_cmdList, m_radiosityFiltered);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_radiosityFiltered,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    m_cmdList->ResourceBarrier(1, &barrier);
    
    // Pass 3: ConvertToSH
    // Input:  Filtered (t22), SurfaceCache (t0-t1)
    // Output: SH_R/G/B (u3, u4, u5)
    Pass_ConvertToSH(constants);
    DX12Helper::UAVBarrier(m_cmdList, m_radiositySH_R);
    DX12Helper::UAVBarrier(m_cmdList, m_radiositySH_G);
    DX12Helper::UAVBarrier(m_cmdList, m_radiositySH_B);
    CD3DX12_RESOURCE_BARRIER shBarriers[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_R, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_G, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_B, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    m_cmdList->ResourceBarrier(3, shBarriers);
    
    // Pass 4: IntegrateSH
    // Input:  SH_R/G/B (t23, t24, t25), SurfaceCache (t0-t1)
    // Output: 重新绑定 UAV 到 Surface Cache Atlas
    D3D12_GPU_DESCRIPTOR_HANDLE surfaceCacheAtlasUav =
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, SURFCACHE_PRIMARY_ATLAS_UAV);
    m_cmdList->SetComputeRootDescriptorTable(4, surfaceCacheAtlasUav);
    ExecuteIntegrateSH(constants);

    // UAV Barrier for IndirectLight layer before CombineLight reads it
    DX12Helper::UAVBarrier(m_cmdList, surfaceCache->GetAtlasTexture());

    // Pass 5: CombineLight (Multi-bounce GI)
    // Input:  DirectLight (layer 3) + IndirectLight (layer 4) - 从 SRV (root param [1]) 读取
    // Output: CombinedLight (layer 5) - 写入 UAV (root param [4] 已绑定到 Surface Cache Atlas)
    // Combined 层会被下一帧的 InjectVoxelLighting 采样，实现多次反弹

    // 转换 IndirectLight 层回 SRV 状态 (CombineLight 需要读取)
    // 同时转换 Combined 层到 UAV 状态 (用于写入)
    CD3DX12_RESOURCE_BARRIER preCombineBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            surfaceCache->GetAtlasTexture(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            SURFACE_CACHE_LAYER_INDIRECT_LIGHT  // Layer 4: UAV -> SRV for reading
        ),
        CD3DX12_RESOURCE_BARRIER::Transition(
            surfaceCache->GetAtlasTexture(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            SURFACE_CACHE_LAYER_COMBINED_LIGHT  // Layer 5: SRV -> UAV for writing
        )
    };
    m_cmdList->ResourceBarrier(2, preCombineBarriers);

    Pass_CombineLight(constants, surfaceCache);

    // UAV Barrier for CombinedLight layer
    DX12Helper::UAVBarrier(m_cmdList, surfaceCache->GetAtlasTexture());

    // 复制 TraceResult -> History (为下帧时间累积)
    CD3DX12_RESOURCE_BARRIER copyBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityTraceResult, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    m_cmdList->ResourceBarrier(2, copyBarriers);
    
    m_cmdList->CopyResource(m_radiosityHistory, m_radiosityTraceResult);

    // 转换 CombinedLight 层回 SRV 状态
    // (IndirectLight 已经在 preCombineBarriers 中转回 SRV 状态)
    CD3DX12_RESOURCE_BARRIER barrierBack = CD3DX12_RESOURCE_BARRIER::Transition(
        surfaceCache->GetAtlasTexture(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        SURFACE_CACHE_LAYER_COMBINED_LIGHT   // Layer 5 (for next frame's InjectVoxelLighting)
    );
    m_cmdList->ResourceBarrier(1, &barrierBack);
    // 恢复下一帧需要的状态
    CD3DX12_RESOURCE_BARRIER restoreBarriers[6] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityTraceResult, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityHistory, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityFiltered, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_R, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_G, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_B, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    m_cmdList->ResourceBarrier(6, restoreBarriers);
 
    m_firstFrame = false;
    //DebuggerPrintf("[SurfaceRadiosity] Execute complete\n");
    m_cmdList = nullptr;
    m_constantBuffer = nullptr;
}

void SurfaceRadiosity::Shutdown()
{
    DX_SAFE_RELEASE(m_radiosityTraceResult);
    DX_SAFE_RELEASE(m_radiosityHistory);
    DX_SAFE_RELEASE(m_radiosityFiltered);
    DX_SAFE_RELEASE(m_radiositySH_R);
    DX_SAFE_RELEASE(m_radiositySH_G);
    DX_SAFE_RELEASE(m_radiositySH_B);
    DX_SAFE_RELEASE(m_probeDepth);
    DX_SAFE_RELEASE(m_probeNormal);

    DX_SAFE_RELEASE(m_rootSignature);
    DX_SAFE_RELEASE(m_radiosityTracePSO);
    DX_SAFE_RELEASE(m_radiosityFilterPSO);
    DX_SAFE_RELEASE(m_convertToSHPSO);
    DX_SAFE_RELEASE(m_integrateSHPSO);
    DX_SAFE_RELEASE(m_combineLightPSO);
}

void SurfaceRadiosity::CreateRadiosityBuffers()
{
    //DebuggerPrintf("[SurfaceRadiosity] Creating radiosity buffers: %ux%u probes\n",
      //  m_probeGridWidth, m_probeGridHeight);
    // Radiosity Trace Result 
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiosityTraceResult,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_TRACE_RESULT_SRV,  // 从 RenderCommon.h
        RADIOSITY_TRACE_RESULT_UAV,
        L"SurfaceRadiosity_TraceResult",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    // Radiosity History (用于时间累积)
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiosityHistory,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_HISTORY_SRV,
        RADIOSITY_HISTORY_UAV,
        L"SurfaceRadiosity_History",
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    // Radiosity Filtered (空间滤波后) 
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiosityFiltered,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_FILTERED_SRV,
        RADIOSITY_FILTERED_UAV,
        L"SurfaceRadiosity_Filtered",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    // Radiosity SH - R Channel 
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiositySH_R,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,  // 4 个 SH 系数 (L0, L1_x, L1_y, L1_z)
        RADIOSITY_SH_R_SRV,
        RADIOSITY_SH_R_UAV,
        L"SurfaceRadiosity_SH_R",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    // Radiosity SH - G Channel
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiositySH_G,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_SH_G_SRV,
        RADIOSITY_SH_G_UAV,
        L"SurfaceRadiosity_SH_G",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    
    // Radiosity SH - B Channel
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiositySH_B,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_SH_B_SRV,
        RADIOSITY_SH_B_UAV,
        L"SurfaceRadiosity_SH_B",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    // Probe Depth (降采样的深度，用于 Filter)
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_probeDepth,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R32_FLOAT,
        RADIOSITY_PROBE_DEPTH_SRV,
        RADIOSITY_PROBE_DEPTH_UAV,
        L"SurfaceRadiosity_ProbeDepth",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    // Probe Normal (降采样的法线，用于 Filter) 
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_probeNormal,
        m_probeGridWidth,
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_PROBE_NORMAL_SRV,
        RADIOSITY_PROBE_NORMAL_UAV,
        L"SurfaceRadiosity_ProbeNormal",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    
    DebuggerPrintf("[SurfaceRadiosity] Created all radiosity buffers\n");
}

void SurfaceRadiosity::CreateTexture2D(
    ID3D12Resource*& resource,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format)
{
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        width,
        height,
        1,  // Array size
        1,  // Mip levels
        1,  // Sample count
        0,  // Sample quality
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );
    
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource)
    );
    
    if (FAILED(hr))
    {
        ERROR_AND_DIE("[SurfaceRadiosity] Failed to create texture!");
    }
}

void SurfaceRadiosity::CreateTexture2DUAV(ID3D12Resource* resource, int descriptorIndex)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = resource->GetDesc().Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = DX12Helper::GetCPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize
        , descriptorIndex);
    m_device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);
}

void SurfaceRadiosity::CreateTexture2DSRV(ID3D12Resource* resource, int descriptorIndex)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = resource->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = DX12Helper::GetCPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize,
        descriptorIndex);
    m_device->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
}

void SurfaceRadiosity::Pass_RadiosityTrace(const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_radiosityTracePSO);
    
    uint32_t dispatchX = (m_probeGridWidth + 7) / 8;
    uint32_t dispatchY = (m_probeGridHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
    
    //DebuggerPrintf("  [Pass 1] Radiosity Trace: %ux%u groups\n", dispatchX, dispatchY);
}

void SurfaceRadiosity::Pass_RadiosityFilter(
    const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_radiosityFilterPSO);
    
    uint32_t dispatchX = (m_probeGridWidth + 7) / 8;
    uint32_t dispatchY = (m_probeGridHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
      
    //DebuggerPrintf("  [Pass 2] Radiosity Filter: %ux%u groups\n", dispatchX, dispatchY);
}

void SurfaceRadiosity::Pass_ConvertToSH(
    const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_convertToSHPSO);
    
    uint32_t dispatchX = (m_probeGridWidth + 7) / 8;
    uint32_t dispatchY = (m_probeGridHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
    
    //DebuggerPrintf("  [Pass 3] Convert to SH: %ux%u groups\n", dispatchX, dispatchY);
}

void SurfaceRadiosity::ExecuteIntegrateSH(
    const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_integrateSHPSO);
    // 注意：这个 Pass 是在 Atlas 分辨率上操作，不是 Probe Grid！
    // 因为要为每个 Atlas Texel 从周围 Probes 插值
    // Dispatch (Atlas 全分辨率)
    uint32_t dispatchX = (m_atlasWidth + 7) / 8;
    uint32_t dispatchY = (m_atlasHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
    
    //DebuggerPrintf("  [Pass 4] Integrate SH: %ux%u groups\n", dispatchX, dispatchY);
}

void SurfaceRadiosity::CreateRootSignature()
{
    // Range 0: Surface Cache Atlas SRV (t0 - t5)
    CD3DX12_DESCRIPTOR_RANGE srvRange0;
    srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0);  // t0-t5

    // Range 1: Global SDF + Voxel Lighting SRV (t10 - t11)
    CD3DX12_DESCRIPTOR_RANGE srvRange1;
    srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 10);  // t10-t11

    // Range 2: Radiosity Textures SRV (t20 - t27)
    // 修改：从 6 个增加到 8 个 (加了 ProbeDepth 和 ProbeNormal)
    CD3DX12_DESCRIPTOR_RANGE srvRange2;
    srvRange2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 20);  // t20-t27

    // Range 3: Radiosity UAVs (u0 - u7)
    // 从 6 个增加到 8 个 (加了 ProbeDepth 和 ProbeNormal)
    CD3DX12_DESCRIPTOR_RANGE uavRange0;
    uavRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0);  // u0-u7
    
    CD3DX12_ROOT_PARAMETER rootParams[5];
    
    // [0] Constant Buffer (b0) - SurfaceRadiosityConstants
    rootParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    
    // [1] Surface Cache SRVs (t0-t5)
    rootParams[1].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_ALL);
    
    // [2] Global SDF + Voxel Lighting SRVs (t10-t11)
    rootParams[2].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_ALL);
    
    // [3] Radiosity SRVs (t20-t25)
    rootParams[3].InitAsDescriptorTable(1, &srvRange2, D3D12_SHADER_VISIBILITY_ALL);
    
    // [4] Radiosity UAVs (u0-u5)
    rootParams[4].InitAsDescriptorTable(1, &uavRange0, D3D12_SHADER_VISIBILITY_ALL);
    
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    
    // Sampler 0: Point Clamp (s0)
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MipLODBias = 0;
    samplers[0].MaxAnisotropy = 0;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // Sampler 1: Linear Clamp (s1)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MipLODBias = 0;
    samplers[1].MaxAnisotropy = 0;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    // ===== 创建 Root Signature =====
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        _countof(rootParams),
        rootParams,
        _countof(samplers),
        samplers,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );
    
    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );
    
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[SurfaceRadiosity] Root signature error: %s\n",
                (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to serialize SurfaceRadiosity root signature!");
    }
    
    hr = m_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create SurfaceRadiosity root signature!");
    
    m_rootSignature->SetName(L"SurfaceRadiosity_RootSignature");
    
    if (signature) signature->Release();
    if (error) error->Release();
    
    DebuggerPrintf("[SurfaceRadiosity] Created independent Root Signature\n");
}

void SurfaceRadiosity::CreatePipelineStates()
{
    m_radiosityTracePSO =  DX12Helper::CreateComputePSO(
        m_device, m_rootSignature,
        "Data/Shaders/SurfaceRadiosity/RadiosityTrace.hlsl",
        "main",
        L"SurfaceRadiosity_TracePSO"
    );
    
    m_radiosityFilterPSO = DX12Helper::CreateComputePSO(
        m_device, m_rootSignature,
        "Data/Shaders/SurfaceRadiosity/RadiosityFilter.hlsl",
        "main",
        L"SurfaceRadiosity_FilterPSO"
    );
    
    m_convertToSHPSO = DX12Helper::CreateComputePSO(
        m_device, m_rootSignature,
        "Data/Shaders/SurfaceRadiosity/ConvertToSH.hlsl",
        "main",
        L"SurfaceRadiosity_ConvertToSHPSO"
    );
    m_integrateSHPSO = DX12Helper::CreateComputePSO(
        m_device, m_rootSignature,
        "Data/Shaders/SurfaceRadiosity/IntegrateSH.hlsl",
        "main",
        L"SurfaceRadiosity_IntegrateSHPSO"
    );

    // Multi-bounce: CombineLight PSO
    m_combineLightPSO = DX12Helper::CreateComputePSO(
        m_device, m_rootSignature,
        "Data/Shaders/CombineSurfaceCacheLight.hlsl",
        "CSMain",
        L"SurfaceRadiosity_CombineLightPSO"
    );
}

void SurfaceRadiosity::Pass_CombineLight(const SurfaceRadiosityConstants& constants, SurfaceCache* surfaceCache)
{
    UNUSED(constants)
    UNUSED(surfaceCache)

    // 设置 PSO
    m_cmdList->SetPipelineState(m_combineLightPSO);

    // UAV 已经在 Execute() 中绑定到 Surface Cache Atlas
    // Dispatch (Atlas 全分辨率)
    uint32_t dispatchX = (m_atlasWidth + 7) / 8;
    uint32_t dispatchY = (m_atlasHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);

    //DebuggerPrintf("  [Pass 5] Combine Light (Multi-bounce): %ux%u groups\n", dispatchX, dispatchY);
}

#endif