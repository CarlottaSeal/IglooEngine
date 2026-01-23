#include "ScreenProbeFinalGather.h"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Renderer/ShaderIncludeHandler.h"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/DX12Renderer.hpp"
#include <d3dcompiler.h>

#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/HelperFunctionLib.h"

// 资源依赖关系总结：
// Pass1 (ProbePlacement):     写入 ProbeBuffer
// Pass2 (BRDFPDFGeneration):  读取 ProbeBuffer, 写入 BRDFPDF
// Pass3 (LightingPDFGeneration): 读取 ProbeBuffer, PrevRadiance, 写入 LightingPDF
// Pass4 (GenerateSampleDirections): 读取 ProbeBuffer, BRDFPDF, LightingPDF, 写入 SampleDirections
// Pass5 (MeshSDFTrace):       读取 ProbeBuffer, SampleDirections, 写入 MeshTraceResults
// Pass6 (VoxelSDFTrace):      读取 ProbeBuffer, SampleDirections, 写入 VoxelTraceResults
// Pass7 (RadianceComposite):  读取 ProbeBuffer, SampleDirections, VoxelTraceResults, 写入 ProbeRadiance
// Pass8 (TemporalAccumulation): 读取 ProbeRadiance, ProbeRadianceHistory, 写入 ProbeRadianceHistory
// Pass9 (SpatialFilter):      读取 ProbeRadianceHistory, 写入 ProbeRadianceFiltered
// Pass10 (FinalGather):       读取 ProbeRadianceFiltered, ProbeBuffer, GBuffer, 写入 ScreenIndirectLighting


ScreenProbeFinalGather::~ScreenProbeFinalGather()
{
    Shutdown();
}

void ScreenProbeFinalGather::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap, uint32_t screenWidth, uint32_t screenHeight)
{
    if (m_initialized)
        return;

    m_device = device;
    m_descriptorHeap = descriptorHeap;
    m_scuDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    
    m_probeGridWidth = (m_screenWidth + SCREEN_PROBE_SPACING-1) / SCREEN_PROBE_SPACING ;
    m_probeGridHeight = (m_screenHeight + SCREEN_PROBE_SPACING-1) / SCREEN_PROBE_SPACING;
    
    DebuggerPrintf("[ScreenProbe] Initialize: %ux%u screen, %ux%u probe grid\n",
        m_screenWidth, m_screenHeight, m_probeGridWidth, m_probeGridHeight);
    
    CreateRootSignature();
    CreateResources();
    m_initialized = true;
    DebuggerPrintf("[ScreenProbe] Initialized successfully\n");
}

void ScreenProbeFinalGather::Shutdown()
{
    DX_SAFE_RELEASE(m_probeBuffer);
    DX_SAFE_RELEASE(m_brdfPdfBuffer);
    DX_SAFE_RELEASE(m_lightingPdfBuffer);
    DX_SAFE_RELEASE(m_sampleDirectionsBuffer);
    DX_SAFE_RELEASE(m_meshTraceResultBuffer);
    DX_SAFE_RELEASE(m_voxelTraceResultBuffer);
    
    DX_SAFE_RELEASE(m_probeRadiance);
    DX_SAFE_RELEASE(m_probeRadianceHistoryA);
    DX_SAFE_RELEASE(m_probeRadianceHistoryB);
    DX_SAFE_RELEASE(m_probeRadianceFiltered);
    
    DX_SAFE_RELEASE(m_octSH_R);
    DX_SAFE_RELEASE(m_octSH_G);
    DX_SAFE_RELEASE(m_octSH_B);
    
    DX_SAFE_RELEASE(m_screenIndirectLighting);
    DX_SAFE_RELEASE(m_screenIndirectRaw);
    DX_SAFE_RELEASE(m_prevScreenRadiance);

    DX_SAFE_RELEASE(m_pso_ProbePlacement);
    DX_SAFE_RELEASE(m_pso_BRDFPDFGeneration);
    DX_SAFE_RELEASE(m_pso_LightingPDFGeneration);
    DX_SAFE_RELEASE(m_pso_GenerateSampleDirections);
    DX_SAFE_RELEASE(m_pso_MeshSDFTrace);
    DX_SAFE_RELEASE(m_pso_VoxelSDFTrace);
    DX_SAFE_RELEASE(m_pso_RadianceComposite);
    DX_SAFE_RELEASE(m_pso_TemporalAccumulation);
    DX_SAFE_RELEASE(m_pso_SpatialFilter);
    DX_SAFE_RELEASE(m_pso_FinalGather);
    DX_SAFE_RELEASE(m_pso_ScreenSpaceTemporalFilter);

    DX_SAFE_RELEASE(m_computeRootSignature);
    
    m_screenWidth = 0;
    m_screenHeight = 0;
    m_probeGridWidth = 0;
    m_probeGridHeight = 0;
    m_initialized = false;
    DebuggerPrintf("[ScreenProbe] Shutdown complete\n");
}

void ScreenProbeFinalGather::CreateResources()
{
    uint32_t probeCount = m_probeGridWidth * m_probeGridHeight;
    uint32_t numTraces = 256;
    DebuggerPrintf("[ScreenProbe] Creating resources for %u probes...\n", probeCount);
    
    // 1. Probe Buffer (Slot 401 SRV, 402 UAV)
    CreateBuffer(&m_probeBuffer,probeCount,sizeof(ScreenProbeGPU),
        SCREEN_PROBE_BUFFER_SRV,SCREEN_PROBE_BUFFER_UAV,L"ScreenProbe_ProbeBuffer");
    // 2. BRDF PDF Buffer (Slot 243 SRV, 244 UAV)
    CreateBuffer(&m_brdfPdfBuffer,probeCount * numTraces,sizeof(SH2CoeffsGPU), 
        SCREEN_PROBE_BRDF_PDF_SRV,SCREEN_PROBE_BRDF_PDF_UAV,L"ScreenProbe_BRDF_PDF");
    // 3. Lighting PDF Buffer (Slot 245 SRV, 246 UAV)
    CreateBuffer(&m_lightingPdfBuffer,probeCount * numTraces,sizeof(SH2CoeffsGPU),  
        SCREEN_PROBE_LIGHTING_PDF_SRV,SCREEN_PROBE_LIGHTING_PDF_UAV,L"ScreenProbe_Lighting_PDF");
    // 4. Sample Directions Buffer (Slot 247 SRV, 248 UAV)
    CreateBuffer(&m_sampleDirectionsBuffer,probeCount * numTraces,sizeof(ImportanceSampleGPU),  // float4
        SCREEN_PROBE_SAMPLE_DIRECTIONS_SRV,SCREEN_PROBE_SAMPLE_DIRECTIONS_UAV,L"ScreenProbe_SampleDirections");
    // 5. Mesh Trace Result Buffer (Slot 249 SRV, 250 UAV)
    CreateBuffer(&m_meshTraceResultBuffer,probeCount * numTraces,sizeof(TraceResult),  // TraceResult struct
        SCREEN_PROBE_MESH_TRACE_SRV,SCREEN_PROBE_MESH_TRACE_UAV,L"ScreenProbe_MeshTraceResult");
    //6. Voxel Trace Result Buffer (Slot 251 SRV, 252 UAV)
    CreateBuffer(&m_voxelTraceResultBuffer,probeCount * numTraces,
        sizeof(TraceResult),  // TraceResult struct
        SCREEN_PROBE_VOXEL_TRACE_SRV,SCREEN_PROBE_VOXEL_TRACE_UAV,L"ScreenProbe_VoxelTraceResult");
    // 7. Probe Radiance Texture (Slot 253 SRV, 254 UAV)
    CreateTexture2D(&m_probeRadiance,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,DXGI_FORMAT_R16G16B16A16_FLOAT,
        SCREEN_PROBE_RADIANCE_SRV,SCREEN_PROBE_RADIANCE_UAV,L"ScreenProbe_ProbeRadiance");
    // 8. Probe Radiance History (Slot 255 SRV, 256 UAV)
    CreateTexture2D(&m_probeRadianceHistoryA,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,DXGI_FORMAT_R16G16B16A16_FLOAT,
         SCREEN_PROBE_RADIANCE_HISTORY_SRV,      // 425
         SCREEN_PROBE_RADIANCE_HISTORY_UAV,      // 409
         L"ScreenProbe_ProbeRadianceHistoryA");
    // 8B. Probe Radiance History B 
    CreateTexture2D(&m_probeRadianceHistoryB,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,DXGI_FORMAT_R16G16B16A16_FLOAT,
        SCREEN_PROBE_RADIANCE_HISTORY_B_SRV,   SCREEN_PROBE_RADIANCE_HISTORY_B_UAV,   L"ScreenProbe_ProbeRadianceHistoryB");
    // 9. Probe Radiance Filtered (Slot 257 SRV, 258 UAV)
    CreateTexture2D(&m_probeRadianceFiltered,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,DXGI_FORMAT_R16G16B16A16_FLOAT,
        SCREEN_PROBE_RADIANCE_FILTERED_SRV,SCREEN_PROBE_RADIANCE_FILTERED_UAV,
        L"ScreenProbe_ProbeRadianceFiltered");
    // 10-12. Octahedral SH Textures (R, G, B)
    CreateTexture2D(&m_octSH_R,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,
        DXGI_FORMAT_R16_FLOAT,SCREEN_PROBE_OCT_SH_R_SRV,
        SCREEN_PROBE_OCT_SH_R_UAV,L"ScreenProbe_OctSH_R");
    CreateTexture2D(&m_octSH_G,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,DXGI_FORMAT_R16_FLOAT,
        SCREEN_PROBE_OCT_SH_G_SRV,SCREEN_PROBE_OCT_SH_G_UAV,L"ScreenProbe_OctSH_G");
    CreateTexture2D(&m_octSH_B,m_probeGridWidth * OCTAHEDRON_SIZE,m_probeGridHeight * OCTAHEDRON_SIZE,
        DXGI_FORMAT_R16_FLOAT,SCREEN_PROBE_OCT_SH_B_SRV,SCREEN_PROBE_OCT_SH_B_UAV,L"ScreenProbe_OctSH_B");
    // 13. Screen Indirect Lighting (Slot 265 SRV, 266 UAV) - 最终输出
    CreateTexture2D(&m_screenIndirectLighting,m_screenWidth,m_screenHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,
        SCREEN_INDIRECT_LIGHTING_SRV  ,SCREEN_INDIRECT_LIGHTING_UAV,L"ScreenProbe_IndirectLighting");
    // 14. Previous Screen Radiance (Slot 267 SRV, 268 UAV)
    CreateTexture2D(&m_prevScreenRadiance,m_screenWidth,m_screenHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,
        PREV_SCREEN_RADIANCE_SRV,PREV_SCREEN_RADIANCE_UAV,L"ScreenProbe_PrevRadiance");
    // 15. Screen Indirect Raw (FinalGather 原始输出，时间滤波前)
    CreateTexture2D(&m_screenIndirectRaw,m_screenWidth,m_screenHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,
        SCREEN_INDIRECT_RAW_SRV,SCREEN_INDIRECT_RAW_UAV,L"ScreenProbe_IndirectRaw");
    DebuggerPrintf("[ScreenProbe] All resources created\n");
}

void ScreenProbeFinalGather::CreateRootSignature()
{
    // Bindless Root Signature 布局:
    // [0] CBV: b0 (ScreenProbeConstants) - Root Descriptor
    // [1] Descriptor Table: 所有 SRV in space0 (unbounded, t0 开始)
    // [2] Descriptor Table: 所有 UAV in space0 (unbounded, u0 开始)
    // [3] Descriptor Table: Bindless SDF Textures in space1 (t0, space1)
    // Static Samplers: s0 (Point), s1 (Linear)
    // SRV Range (space0): 无限制数量，从 t0 开始
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        UINT_MAX,  // NumDescriptors = UINT_MAX 表示 unbounded
        0,         // BaseShaderRegister = t0
        0,         // RegisterSpace = space0
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
        0          // OffsetInDescriptorsFromTableStart
    );
    
    // UAV Range (space0): 无限制数量，从 u0 开始
    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        UINT_MAX,  // NumDescriptors = UINT_MAX
        0,         // BaseShaderRegister = u0
        0,         // RegisterSpace = space0
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
        0          // OffsetInDescriptorsFromTableStart
    );
    
    // Bindless SDF Textures (space1): 用于 Mesh SDF 追踪
    CD3DX12_DESCRIPTOR_RANGE1 sdfTextureRange;
    sdfTextureRange.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        MAX_SDF_TEXTURE_COUNT,        
        0,         // BaseShaderRegister = t0
        1,         // RegisterSpace = space1  <-- 重要！
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
        0
    );
    
    // Root Parameters
    CD3DX12_ROOT_PARAMETER1 rootParams[4];
    
    // [0] CBV: Constant Buffer at b0 (Root CBV, 不通过描述符堆)
    rootParams[0].InitAsConstantBufferView(
        0,  // shaderRegister = b0
        0,  // registerSpace = 0
        D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
        D3D12_SHADER_VISIBILITY_ALL
    );
    
    // [1] SRV Descriptor Table (unbounded, space0)
    rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
    
    // [2] UAV Descriptor Table (unbounded, space0)
    rootParams[2].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    
    // [3] Bindless SDF Textures (space1)
    rootParams[3].InitAsDescriptorTable(1, &sdfTextureRange, D3D12_SHADER_VISIBILITY_ALL);
    
    // Static Samplers
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[2];
    
    // s0: Point Sampler
    staticSamplers[0].Init(
        0,                                  // shaderRegister = s0
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f, 16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f, D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_ALL
    );
    
    // s1: Linear Sampler
    staticSamplers[1].Init(
        1,                                  // shaderRegister = s1
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f, 16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f, D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_ALL
    );
    
    // Root Signature 描述
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(
        _countof(rootParams), rootParams,
        _countof(staticSamplers), staticSamplers,
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
    );
    
    // 序列化 Root Signature
    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    
    HRESULT hr = D3DX12SerializeVersionedRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_1,
        &signature,
        &error
    );
    
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[ScreenProbe] Root Signature serialization failed: %s\n",
                (char*)error->GetBufferPointer());
            error->Release();
        }
        return;
    }
    
    hr = m_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_computeRootSignature)
    );
    
    signature->Release();
    
    if (FAILED(hr))
    {
        DebuggerPrintf("[ScreenProbe] Failed to create Root Signature: 0x%08X\n", hr);
    }
    else
    {
        m_computeRootSignature->SetName(L"ScreenProbe_BindlessRootSignature");
        DebuggerPrintf("[ScreenProbe] Bindless Root Signature created successfully\n");
    }
}

void ScreenProbeFinalGather::CreateBuffer(
    ID3D12Resource** outResource,
    uint32_t elementCount,
    uint32_t elementSize,
    int srvSlot,
    int uavSlot,
    const wchar_t* debugName)
{
    uint32_t bufferSize = elementCount * elementSize;
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 0;
    heapProps.VisibleNodeMask = 0;
    
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(outResource)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create buffer");
    (*outResource)->SetName(debugName);
    
    if (srvSlot >= 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    
        // 所有结构体都用 StructuredBuffer
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = elementCount;
        srvDesc.Buffer.StructureByteStride = elementSize;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    
        m_device->CreateShaderResourceView(
            *outResource, 
            &srvDesc, 
            GetCPUHandle(srvSlot)); 
    }
    if (uavSlot >= 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.NumElements = elementCount;
        uavDesc.Buffer.StructureByteStride = elementSize;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    
        m_device->CreateUnorderedAccessView(
            *outResource, 
            nullptr, 
            &uavDesc, 
            GetCPUHandle(uavSlot));
    }
}

void ScreenProbeFinalGather::CreateTexture2D(
    ID3D12Resource** outResource,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    int srvSlot,
    int uavSlot,
    const wchar_t* debugName)
{
    ID3D12Device* device = m_device;
    ID3D12DescriptorHeap* descriptorHeap = m_descriptorHeap;
    UINT descriptorSize = m_scuDescriptorSize;
    
    // 1. 创建 Texture2D Resource
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 0;
    heapProps.VisibleNodeMask = 0;
    
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(outResource)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Texture2D");
    (*outResource)->SetName(debugName);
    
    // 2. 创建 SRV
    if (srvSlot >= 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += srvSlot * descriptorSize;
        
        device->CreateShaderResourceView(*outResource, &srvDesc, srvHandle);
    }
    
    // 3. 创建 UAV
    if (uavSlot >= 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
        uavHandle.ptr += uavSlot * descriptorSize;
        
        device->CreateUnorderedAccessView(*outResource, nullptr, &uavDesc, uavHandle);
    }
}

ID3D12PipelineState* ScreenProbeFinalGather::GetOrCreatePSO(
    ID3D12PipelineState** psoPtr,
    const char* shaderFile,
    const char* entryPoint,
    const wchar_t* debugName)
{
    if (*psoPtr)
        return *psoPtr;
    *psoPtr = DX12Helper::CreateComputePSO(
        m_device,
        m_computeRootSignature,
        shaderFile,
        entryPoint,
        debugName,
        nullptr  // includeDirectory 可以自动推断
    );
    
    // if (*psoPtr)
    // {
    //     DebuggerPrintf("[ScreenProbe] PSO created: %s\n", shaderFile);
    // }
    return *psoPtr;
}

void ScreenProbeFinalGather::BindGBufferToDescriptorHeap(GBufferData* gBuffer)
{
    D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    // t214: Albedo
    if (gBuffer->m_albedo)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += GBUFFER_SRV_START_SLOT * m_scuDescriptorSize;
        m_device->CreateShaderResourceView(gBuffer->m_albedo, nullptr, handle);
    }
    // t215: Normal
    if (gBuffer->m_normal)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += (GBUFFER_SRV_START_SLOT + 1) * m_scuDescriptorSize;
        m_device->CreateShaderResourceView(gBuffer->m_normal, nullptr, handle);
    }
    // t216: Material
    if (gBuffer->m_material)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += (GBUFFER_SRV_START_SLOT + 2) * m_scuDescriptorSize;
        m_device->CreateShaderResourceView(gBuffer->m_material, nullptr, handle);
    }
    // t217: Motion
    if (gBuffer->m_worldPos)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += (GBUFFER_SRV_START_SLOT + 3) * m_scuDescriptorSize;
        m_device->CreateShaderResourceView(gBuffer->m_worldPos, nullptr, handle);
    }
}

void ScreenProbeFinalGather::Execute(
    ID3D12GraphicsCommandList* cmdList,
    ConstantBuffer* constantBuffer,
    const ScreenProbeConstants& constants,
    D3D12_GPU_DESCRIPTOR_HANDLE sdfTexturesHandle,
    GBufferData* gBuffer, SurfaceCache* surfaceCache)  
{
    UNUSED(surfaceCache);
    if (!m_initialized)
    {
        DebuggerPrintf("[ScreenProbe] Not initialized, skipping Execute\n");
        return;
    }
    m_commandList = cmdList;
    m_constantBuffer = constantBuffer;
    //DebuggerPrintf("[ScreenProbe] === Execute Frame ===\n");
    
    ID3D12Resource* historyInput = m_useHistoryB ? m_probeRadianceHistoryB : m_probeRadianceHistoryA;
    ID3D12Resource* historyOutput = m_useHistoryB ? m_probeRadianceHistoryA : m_probeRadianceHistoryB;
    
    CD3DX12_RESOURCE_BARRIER textureBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadiance,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        // History输入：转成SRV（用于读取）
        CD3DX12_RESOURCE_BARRIER::Transition(historyInput,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        // History输出：转成UAV（用于写入）
        CD3DX12_RESOURCE_BARRIER::Transition(historyOutput,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadianceFiltered,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_screenIndirectLighting,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_screenIndirectRaw,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(m_prevScreenRadiance,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    m_commandList->ResourceBarrier(_countof(textureBarriers), textureBarriers);
    
    m_commandList->SetComputeRootSignature(m_computeRootSignature);
    if (gBuffer)
    {
        BindGBufferToDescriptorHeap(gBuffer);
    }
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
    m_commandList->SetDescriptorHeaps(1, heaps);
    // Root Parameter 0: CBV
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_constantBuffer->GetDX12ConstantBuffer()->GetGPUVirtualAddress();
    m_commandList->SetComputeRootConstantBufferView(0, cbAddress);
    
    // Root Parameter 1: SRV Table (space0)
    D3D12_GPU_DESCRIPTOR_HANDLE heapStart = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    m_commandList->SetComputeRootDescriptorTable(1, heapStart);
    // Root Parameter 2: UAV Table (space0)
    m_commandList->SetComputeRootDescriptorTable(2, heapStart);
    // Root Parameter 3: Bindless SDF Textures (space1)
    m_commandList->SetComputeRootDescriptorTable(3, sdfTexturesHandle);
    
    Pass01_ProbePlacement(constants);
    Pass02_BRDFPDFGeneration(constants);
    Pass03_LightingPDFGeneration(constants);
    Pass04_GenerateSampleDirections(constants);
    Pass05_MeshSDFTrace(constants);
    Pass06_VoxelSDFTrace(constants);
    Pass07_RadianceComposite(constants);
    Pass08_TemporalAccumulation(constants);
    Pass09_SpatialFilter(constants);
    Pass10_FinalGather(constants);
    Pass11_ScreenSpaceTemporalFilter(constants);
    
    CD3DX12_RESOURCE_BARRIER endBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadiance,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        // History输入：从SRV转回COMMON
        CD3DX12_RESOURCE_BARRIER::Transition(historyInput,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        // History输出：从UAV转回COMMON
        CD3DX12_RESOURCE_BARRIER::Transition(historyOutput,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadianceFiltered,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(m_screenIndirectLighting,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(m_screenIndirectRaw,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
    };
    m_commandList->ResourceBarrier(_countof(endBarriers), endBarriers);
    
    m_commandList = nullptr;
    m_constantBuffer = nullptr;
    //DebuggerPrintf("[ScreenProbe] === Frame Complete ===\n");
}

//=============================================================================
// Pass 1: Probe Placement
// 输入: DepthBuffer (t218), NormalBuffer (t215) - 外部资源，已经是 SRV
// 输出: ProbeBuffer (u401) - Buffer，隐式提升
//=============================================================================
void ScreenProbeFinalGather::Pass01_ProbePlacement(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_ProbePlacement,
        "Data/Shaders/ScreenProbe/ProbePlacement.hlsl",
        "main",
        L"ScreenProbe_ProbePlacement"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t groupsX = (m_probeGridWidth + 7) / 8;
    uint32_t groupsY = (m_probeGridHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    // UAV Barrier: 确保 ProbeBuffer 写入完成后才能被后续 Pass 读取
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_probeBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 1] Probe Placement: %ux%u groups\n", groupsX, groupsY);
}

//=============================================================================
// Pass 2: BRDF PDF Generation
// 输入: ProbeBuffer (t415), DepthBuffer (t218), NormalBuffer (t215)
// 输出: BRDFPDFOutput (u402)
//=============================================================================
void ScreenProbeFinalGather::Pass02_BRDFPDFGeneration(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_BRDFPDFGeneration,
        "Data/Shaders/ScreenProbe/BRDFPDFGeneration.hlsl",
        "main",
        L"ScreenProbe_BRDFPDF"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t groupsX = (m_probeGridWidth + 7) / 8;
    uint32_t groupsY = (m_probeGridHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_brdfPdfBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 2] BRDF PDF: %ux%u groups\n", groupsX, groupsY);
}

//=============================================================================
// Pass 3: Lighting PDF Generation
// 输入: ProbeBuffer (t415), PrevScreenRadiance (t418)
// 输出: LightingPDFOutput (u403)
//=============================================================================
void ScreenProbeFinalGather::Pass03_LightingPDFGeneration(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_LightingPDFGeneration,
        "Data/Shaders/ScreenProbe/LightingPDFGeneration_Debug.hlsl",
        "main",
        L"ScreenProbe_LightingPDF"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t groupsX = (m_probeGridWidth + 7) / 8;
    uint32_t groupsY = (m_probeGridHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_lightingPdfBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 3] Lighting PDF: %ux%u groups\n", groupsX, groupsY);
}

//=============================================================================
// Pass 4: Generate Sample Directions
// 输入: ProbeBuffer (t415), BRDFPDF (t416), LightingPDF (t417)
// 输出: SampleDirections (u405)
//=============================================================================
void ScreenProbeFinalGather::Pass04_GenerateSampleDirections(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_GenerateSampleDirections,
        "Data/Shaders/ScreenProbe/GenerateSampleDirections.hlsl",
        "main",
        L"ScreenProbe_SampleDirections"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t totalRays = m_probeGridWidth * m_probeGridHeight * SCREEN_PROBE_RAYS; // RaysPerProbe = 64
    uint32_t groups = (totalRays + SCREEN_PROBE_RAYS - 1) / SCREEN_PROBE_RAYS;
    m_commandList->Dispatch(groups, 1, 1);
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_sampleDirectionsBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 4] Sample Directions: %u groups\n", groups);
}

//=============================================================================
// Pass 5: Mesh SDF Trace
// 输入: ProbeBuffer (t415), SampleDirections (t420), InstanceInfos (t380), etc.
// 输出: MeshTraceResults (u406)
//=============================================================================
void ScreenProbeFinalGather::Pass05_MeshSDFTrace(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_MeshSDFTrace,
        "Data/Shaders/ScreenProbe/MeshSDFTrace_Debug.hlsl",
        "main",
        L"ScreenProbe_MeshTrace"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t totalRays = m_probeGridWidth * m_probeGridHeight * SCREEN_PROBE_RAYS;
    uint32_t groups = (totalRays + SCREEN_PROBE_RAYS - 1) / SCREEN_PROBE_RAYS;
    m_commandList->Dispatch(groups, 1, 1);
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_meshTraceResultBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 5] Mesh SDF Trace: %u groups\n", groups);
}

//=============================================================================
// Pass 6: Voxel SDF Trace
// 输入: ProbeBuffer (t415), SampleDirections (t420), GlobalSDF (t378), VoxelLighting (t379)
// 输出: VoxelTraceResults (u407)
//=============================================================================
void ScreenProbeFinalGather::Pass06_VoxelSDFTrace(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_VoxelSDFTrace,
        "Data/Shaders/ScreenProbe/VoxelSDFTrace.hlsl",
        "main",
        L"ScreenProbe_VoxelTrace"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t totalRays = m_probeGridWidth * m_probeGridHeight * SCREEN_PROBE_RAYS;
    uint32_t groups = (totalRays + SCREEN_PROBE_RAYS - 1) / SCREEN_PROBE_RAYS;
    m_commandList->Dispatch(groups, 1, 1);
    
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_voxelTraceResultBuffer;
    m_commandList->ResourceBarrier(1, &barrier);
    
    //DebuggerPrintf("  [Pass 6] Voxel SDF Trace: %u groups\n", groups);
}

//=============================================================================
// Pass 7: Radiance Composite
// 输入: ProbeBuffer (t415), SampleDirections (t420), VoxelTraceResults (t422), VoxelLighting (t379)
// 输出: ProbeRadiance (u408) - Texture2D，当前是 UAV 状态
// 
// Pass 结束后需要转换 ProbeRadiance: UAV -> SRV (给 Pass8 读取)
//=============================================================================
void ScreenProbeFinalGather::Pass07_RadianceComposite(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_RadianceComposite,
        "Data/Shaders/ScreenProbe/RadianceComposite.hlsl",
        "main",
        L"ScreenProbe_RadianceComposite"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    // uint32_t texWidth = m_probeGridWidth * 8;
    // uint32_t texHeight = m_probeGridHeight * 8;
    // uint32_t groupsX = (texWidth + 7) / 8;
    // uint32_t groupsY = (texHeight + 7) / 8;
    uint32_t groupsX = m_probeGridWidth;
    uint32_t groupsY = m_probeGridHeight;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    // UAV Barrier + 状态转换: ProbeRadiance 从 UAV -> SRV
    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(m_probeRadiance),
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadiance,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
    
    //DebuggerPrintf("  [Pass 7] Radiance Composite: %ux%u groups\n", groupsX, groupsY);
}

void ScreenProbeFinalGather::Pass08_TemporalAccumulation(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_TemporalAccumulation,
        "Data/Shaders/ScreenProbe/TemporalAccumulation.hlsl",
        "main",
        L"ScreenProbe_TemporalAccumulation"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    
    uint32_t texWidth = m_probeGridWidth * 8;
    uint32_t texHeight = m_probeGridHeight * 8;
    uint32_t groupsX = (texWidth + 7) / 8;
    uint32_t groupsY = (texHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    ID3D12Resource* historyOutput = m_useHistoryB ? m_probeRadianceHistoryA : m_probeRadianceHistoryB;
    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(historyOutput),
        CD3DX12_RESOURCE_BARRIER::Transition(historyOutput,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
    
    m_useHistoryB = !m_useHistoryB;
    
    //DebuggerPrintf("  [Pass 8] Temporal Accumulation: %ux%u groups (Next frame will use History %c)\n", 
      //             groupsX, groupsY, m_useHistoryB ? 'B' : 'A');
}

//=============================================================================
// Pass 9: Spatial Filter
// 输入: ProbeRadianceHistory (t424) - 从 Pass8 转成 SRV
// 输出: ProbeRadianceFiltered (u410)
//
// Pass 结束后需要转换 ProbeRadianceFiltered: UAV -> SRV (给 Pass10 读取)
//=============================================================================
void ScreenProbeFinalGather::Pass09_SpatialFilter(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_SpatialFilter,
        "Data/Shaders/ScreenProbe/SpatialFilter.hlsl",
        "main",
        L"ScreenProbe_SpatialFilter"
    );
    if (!pso) return;
    
    m_commandList->SetPipelineState(pso);
    uint32_t texWidth = m_probeGridWidth * 8;
    uint32_t texHeight = m_probeGridHeight * 8;
    uint32_t groupsX = (texWidth + 7) / 8;
    uint32_t groupsY = (texHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);
    
    // UAV Barrier + 状态转换: ProbeRadianceFiltered 从 UAV -> SRV
    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(m_probeRadianceFiltered),
        CD3DX12_RESOURCE_BARRIER::Transition(m_probeRadianceFiltered,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
    
    //DebuggerPrintf("  [Pass 9] Spatial Filter: %ux%u groups\n", groupsX, groupsY);
}

//=============================================================================
// Pass 10: Final Gather
// 输入: DepthBuffer (t218), NormalBuffer (t215), AlbedoBuffer (t214)
//       ProbeRadianceFiltered (t425) - 从 Pass9 转成 SRV
//       ProbeBuffer (t415)
// 输出: ScreenIndirectRaw (u433) - FinalGather 原始输出
//
// Pass 结束后转换 ScreenIndirectRaw: UAV -> SRV (给 Pass11 时间滤波读取)
//=============================================================================
void ScreenProbeFinalGather::Pass10_FinalGather(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_FinalGather,
        "Data/Shaders/ScreenProbe/FinalGather.hlsl",
        "main",
        L"ScreenProbe_FinalGather"
    );
    if (!pso) return;

    m_commandList->SetPipelineState(pso);
    uint32_t groupsX = (m_screenWidth + 7) / 8;
    uint32_t groupsY = (m_screenHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);

    // UAV Barrier + 状态转换: ScreenIndirectRaw 从 UAV -> SRV (给 Pass11 读取)
    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(m_screenIndirectRaw),
        CD3DX12_RESOURCE_BARRIER::Transition(m_screenIndirectRaw,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);

    //DebuggerPrintf("  [Pass 10] Final Gather: %ux%u groups\n", groupsX, groupsY);
}

//=============================================================================
// Pass 11: Screen Space Temporal Filter
// 输入: ScreenIndirectRaw (t434) - FinalGather 原始输出
//       PrevScreenRadiance (t419) - 上一帧滤波结果 (历史)
//       GBuffer: WorldPos (t217), Normal (t215), Depth (t218)
// 输出: ScreenIndirectLighting (u414) - 时间滤波后的最终输出
//
// Pass 结束后复制 ScreenIndirectLighting -> PrevScreenRadiance (为下一帧历史)
//=============================================================================
void ScreenProbeFinalGather::Pass11_ScreenSpaceTemporalFilter(const ScreenProbeConstants& c)
{
    UNUSED(c);
    ID3D12PipelineState* pso = GetOrCreatePSO(
        &m_pso_ScreenSpaceTemporalFilter,
        "Data/Shaders/ScreenProbe/ScreenSpaceTemporalFilter.hlsl",
        "main",
        L"ScreenProbe_ScreenSpaceTemporalFilter"
    );
    if (!pso) return;

    m_commandList->SetPipelineState(pso);
    uint32_t groupsX = (m_screenWidth + 7) / 8;
    uint32_t groupsY = (m_screenHeight + 7) / 8;
    m_commandList->Dispatch(groupsX, groupsY, 1);

    // UAV Barrier 确保 ScreenIndirectLighting 写入完成
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_screenIndirectLighting;
    m_commandList->ResourceBarrier(1, &uavBarrier);

    //DebuggerPrintf("  [Pass 11] Screen Space Temporal Filter: %ux%u groups\n", groupsX, groupsY);

    // 复制 ScreenIndirectLighting → PrevScreenRadiance (为下一帧的历史)
    {
        // 1. 转换到复制状态
        CD3DX12_RESOURCE_BARRIER copyBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_screenIndirectLighting,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE
            ),
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_prevScreenRadiance,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST
            )
        };
        m_commandList->ResourceBarrier(_countof(copyBarriers), copyBarriers);
        // 2. 执行复制
        m_commandList->CopyResource(m_prevScreenRadiance, m_screenIndirectLighting);
        // 3. 转换到最终状态
        CD3DX12_RESOURCE_BARRIER restoreCopyBarriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_screenIndirectLighting,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            ),
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_prevScreenRadiance,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_COMMON
            )
        };
        m_commandList->ResourceBarrier(_countof(restoreCopyBarriers), restoreCopyBarriers);
        //DebuggerPrintf("  [Post-Pass11] Copied ScreenIndirectLighting → PrevScreenRadiance\n");
    }
}
#endif