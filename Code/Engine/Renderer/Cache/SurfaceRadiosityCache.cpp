#include "SurfaceRadiosityCache.h"

#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Renderer/RenderCommon.h"

#ifdef ENGINE_DX12_RENDERER
#include "SurfaceCache.h"
#include "CacheCommon.h"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/HelperFunctionLib.h"
#include "ThirdParty/d3dx12/d3dx12.h"

// SimLumen 常量
static constexpr uint32_t PROBE_TEXELS_SIZE = 4;  // 每个 probe 是 4x4 像素

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

    // SimLumen: Probe Grid = Atlas / PROBE_TEXELS_SIZE
    m_probeGridWidth = atlasWidth / PROBE_TEXELS_SIZE;
    m_probeGridHeight = atlasHeight / PROBE_TEXELS_SIZE;

    CreateRadiosityBuffers();
    CreateRootSignature();
    CreatePipelineStates();

    DebuggerPrintf("[SurfaceRadiosity] SimLumen style: Atlas %u×%u, Probes %u×%u\n",
                   m_atlasWidth, m_atlasHeight, m_probeGridWidth, m_probeGridHeight);
}

void SurfaceRadiosity::Execute(ID3D12GraphicsCommandList* cmdList, ConstantBuffer* constantBuffer,
                               const SurfaceRadiosityConstants& constants, SurfaceCache* surfaceCache)
{
    m_cmdList = cmdList;
    m_constantBuffer = constantBuffer;

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

    // [5] CardIndexLookup SRV (t12) — O(1) tile lookup to replace per-ray linear scan
    D3D12_GPU_DESCRIPTOR_HANDLE cardLookupHandle =
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, CARD_INDEX_LOOKUP_SRV);
    m_cmdList->SetComputeRootDescriptorTable(5, cardLookupHandle);

    // =========================================================================
    // Pass 1: RadiosityTrace (SimLumen: 每像素一条射线)
    // Input:  SurfaceCache (t0-t1), GlobalSDF (t10), VoxelLighting (t11)
    // Output: TraceRadianceAtlas (u0) - Atlas 分辨率
    // =========================================================================
    Pass_RadiosityTrace(constants);

    DX12Helper::UAVBarrier(m_cmdList, m_radiosityTraceResult);
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_radiosityTraceResult,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    m_cmdList->ResourceBarrier(1, &barrier);

    // =========================================================================
    // Pass 2: RadiosityFilter (SimLumen: 每像素级别 2x2 十字滤波)
    // Input:  TraceRadianceAtlas (t20)
    // Output: TraceRadianceFiltered (u2) - Atlas 分辨率
    // =========================================================================
    Pass_RadiosityFilter(constants);

    DX12Helper::UAVBarrier(m_cmdList, m_radiosityFiltered);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_radiosityFiltered,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    m_cmdList->ResourceBarrier(1, &barrier);

    // =========================================================================
    // Pass 3: ConvertToSH (SimLumen: 遍历 4x4 像素投影到 SH)
    // Input:  TraceRadianceFiltered (t22), SurfaceCache (t0-t1)
    // Output: SH_R/G/B (u3, u4, u5) - Probe Grid 分辨率
    // =========================================================================
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

    // =========================================================================
    // Pass 4: IntegrateSH (SimLumen: 双线性插值 + DotSH)
    // Input:  SH_R/G/B (t23, t24, t25), SurfaceCache (t0-t1)
    // Output: Surface Cache Indirect Light 层 - Atlas 分辨率
    // =========================================================================
    D3D12_GPU_DESCRIPTOR_HANDLE surfaceCacheAtlasUav =
        DX12Helper::GetGPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, SURFCACHE_PRIMARY_ATLAS_UAV);
    m_cmdList->SetComputeRootDescriptorTable(4, surfaceCacheAtlasUav);
    ExecuteIntegrateSH(constants);

    DX12Helper::UAVBarrier(m_cmdList, surfaceCache->GetAtlasTexture());

    // =========================================================================
    // Pass 5: CombineLight (Multi-bounce GI)
    // =========================================================================
    CD3DX12_RESOURCE_BARRIER preCombineBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            surfaceCache->GetAtlasTexture(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            SURFACE_CACHE_LAYER_INDIRECT_LIGHT
        ),
        CD3DX12_RESOURCE_BARRIER::Transition(
            surfaceCache->GetAtlasTexture(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            SURFACE_CACHE_LAYER_COMBINED_LIGHT
        )
    };
    m_cmdList->ResourceBarrier(2, preCombineBarriers);

    Pass_CombineLight(constants, surfaceCache);

    DX12Helper::UAVBarrier(m_cmdList, surfaceCache->GetAtlasTexture());

    // SimLumen: 不需要 History 拷贝 (无时间累积)

    // 恢复状态
    CD3DX12_RESOURCE_BARRIER barrierBack = CD3DX12_RESOURCE_BARRIER::Transition(
        surfaceCache->GetAtlasTexture(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        SURFACE_CACHE_LAYER_COMBINED_LIGHT
    );
    m_cmdList->ResourceBarrier(1, &barrierBack);

    CD3DX12_RESOURCE_BARRIER restoreBarriers[4] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityTraceResult, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiosityFiltered, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_R, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_radiositySH_G, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    m_cmdList->ResourceBarrier(4, restoreBarriers);

    CD3DX12_RESOURCE_BARRIER restoreBarrierB = CD3DX12_RESOURCE_BARRIER::Transition(
        m_radiositySH_B, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_cmdList->ResourceBarrier(1, &restoreBarrierB);

    m_firstFrame = false;
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
    DebuggerPrintf("[SurfaceRadiosity] Creating SimLumen-style buffers...\n");

    // =========================================================================
    // SimLumen: TraceResult 和 Filtered 是 Atlas 分辨率 (每像素追踪)
    // =========================================================================

    // Radiosity Trace Result (Atlas 分辨率)
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiosityTraceResult,
        m_atlasWidth,      // SimLumen: Atlas 分辨率
        m_atlasHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_TRACE_RESULT_SRV,
        RADIOSITY_TRACE_RESULT_UAV,
        L"SurfaceRadiosity_TraceResult",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );

    // Radiosity Filtered (Atlas 分辨率)
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiosityFiltered,
        m_atlasWidth,      // SimLumen: Atlas 分辨率
        m_atlasHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        RADIOSITY_FILTERED_SRV,
        RADIOSITY_FILTERED_UAV,
        L"SurfaceRadiosity_Filtered",
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );

    // =========================================================================
    // SimLumen: SH 缓冲区是 Probe Grid 分辨率 (每 probe 一组 SH)
    // =========================================================================

    // Radiosity SH - R Channel (Probe Grid 分辨率)
    DX12Helper::CreateTexture2D(
        m_device,
        m_descriptorHeap,
        m_scuDescriptorSize,
        &m_radiositySH_R,
        m_probeGridWidth,   // Probe Grid 分辨率
        m_probeGridHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
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

    // SimLumen: 不需要 History 缓冲区 (无时间累积)
    // 保留 m_radiosityHistory 但不创建，避免修改头文件
    m_radiosityHistory = nullptr;

    // 保留 ProbeDepth 和 ProbeNormal (可能其他地方用到)
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

    DebuggerPrintf("[SurfaceRadiosity] Buffers created: Trace/Filter=%ux%u, SH=%ux%u\n",
        m_atlasWidth, m_atlasHeight, m_probeGridWidth, m_probeGridHeight);
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

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = DX12Helper::GetCPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, descriptorIndex);
    m_device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);
}

void SurfaceRadiosity::CreateTexture2DSRV(ID3D12Resource* resource, int descriptorIndex)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = resource->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = DX12Helper::GetCPUDescriptorHandle(m_descriptorHeap, m_scuDescriptorSize, descriptorIndex);
    m_device->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
}

void SurfaceRadiosity::Pass_RadiosityTrace(const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_radiosityTracePSO);

    // SimLumen: Atlas 分辨率, threadgroup [16,16,1]
    uint32_t dispatchX = (m_atlasWidth + 15) / 16;
    uint32_t dispatchY = (m_atlasHeight + 15) / 16;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
}

void SurfaceRadiosity::Pass_RadiosityFilter(const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_radiosityFilterPSO);

    // SimLumen: Atlas 分辨率, threadgroup [16,16,1]
    uint32_t dispatchX = (m_atlasWidth + 15) / 16;
    uint32_t dispatchY = (m_atlasHeight + 15) / 16;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
}

void SurfaceRadiosity::Pass_ConvertToSH(const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_convertToSHPSO);

    // SimLumen: Probe Grid 分辨率, threadgroup [8,8,1]
    uint32_t dispatchX = (m_probeGridWidth + 7) / 8;
    uint32_t dispatchY = (m_probeGridHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
}

void SurfaceRadiosity::ExecuteIntegrateSH(const SurfaceRadiosityConstants& constants)
{
    UNUSED(constants)
    m_cmdList->SetPipelineState(m_integrateSHPSO);

    // SimLumen: Atlas 分辨率, threadgroup [16,16,1]
    uint32_t dispatchX = (m_atlasWidth + 15) / 16;
    uint32_t dispatchY = (m_atlasHeight + 15) / 16;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
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
    CD3DX12_DESCRIPTOR_RANGE srvRange2;
    srvRange2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 20);  // t20-t27

    // Range 3: CardIndexLookup SRV (t12) — O(1) atlas-tile → card-index lookup
    CD3DX12_DESCRIPTOR_RANGE srvRangeCardLookup;
    srvRangeCardLookup.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 12);

    // Range 4: Radiosity UAVs (u0 - u7)
    CD3DX12_DESCRIPTOR_RANGE uavRange0;
    uavRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0);  // u0-u7

    CD3DX12_ROOT_PARAMETER rootParams[6];

    // [0] Constant Buffer (b0)
    rootParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [1] Surface Cache SRVs (t0-t5)
    rootParams[1].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_ALL);

    // [2] Global SDF + Voxel Lighting SRVs (t10-t11)
    rootParams[2].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_ALL);

    // [3] Radiosity SRVs (t20-t27)
    rootParams[3].InitAsDescriptorTable(1, &srvRange2, D3D12_SHADER_VISIBILITY_ALL);

    // [4] Radiosity UAVs (u0-u7)
    rootParams[4].InitAsDescriptorTable(1, &uavRange0, D3D12_SHADER_VISIBILITY_ALL);

    // [5] CardIndexLookup SRV (t12)
    rootParams[5].InitAsDescriptorTable(1, &srvRangeCardLookup, D3D12_SHADER_VISIBILITY_ALL);

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

    DebuggerPrintf("[SurfaceRadiosity] Created Root Signature\n");
}

void SurfaceRadiosity::CreatePipelineStates()
{
    m_radiosityTracePSO = DX12Helper::CreateComputePSO(
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

    m_cmdList->SetPipelineState(m_combineLightPSO);

    uint32_t dispatchX = (m_atlasWidth + 7) / 8;
    uint32_t dispatchY = (m_atlasHeight + 7) / 8;
    m_cmdList->Dispatch(dispatchX, dispatchY, 1);
}

#endif
