//=============================================================================
// GIVisualization.cpp
// GI 可视化系统实现 - 带详细调试输出
//=============================================================================

#include "GIVisualization.h"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/HelperFunctionLib.h"
#include "Engine/Renderer/ShaderIncludeHandler.h"
#include "Engine/Core/FileUtils.hpp"
#include "Cache/SurfaceCache.h"
#include "ThirdParty/d3dx12/d3dx12.h"
#include "ThirdParty/ImGui/imgui.h"
#include <d3dcompiler.h>

GIVisualization::~GIVisualization()
{
    Shutdown();
}

void GIVisualization::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap,
                                  uint32_t screenWidth, uint32_t screenHeight)
{
    if (m_initialized)
        return;
    
    m_device = device;
    m_descriptorHeap = descriptorHeap;
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    
    DebuggerPrintf("[GIVisualization] Initializing %ux%u...\n", screenWidth, screenHeight);
    
    CreateOutputTexture();
    CreateRootSignatures();
    CreatePipelineStates();
    CreateSurfaceCacheResources();
    
    m_initialized = true;
    
    DebuggerPrintf("[GIVisualization] Init Results:\n");
    DebuggerPrintf("  FullscreenRootSig: %s\n", m_fullscreenRootSig ? "OK" : "FAILED");
    DebuggerPrintf("  FullscreenPSO: %s\n", m_fullscreenPSO ? "OK" : "FAILED");
    DebuggerPrintf("  SurfaceCacheRootSig: %s\n", m_surfaceCacheRootSig ? "OK" : "FAILED");
    DebuggerPrintf("  SurfaceCachePSO: %s\n", m_surfaceCachePSO ? "OK" : "FAILED");
    DebuggerPrintf("  OutputTexture: %s\n", m_outputTexture ? "OK" : "FAILED");
    DebuggerPrintf("  QuadVB: %s\n", m_quadVertexBuffer ? "OK" : "FAILED");
}

void GIVisualization::Shutdown()
{
    if (!m_initialized)
        return;
    
    DX_SAFE_RELEASE(m_fullscreenPSO);
    DX_SAFE_RELEASE(m_fullscreenRootSig);
    DX_SAFE_RELEASE(m_surfaceCachePSO);
    DX_SAFE_RELEASE(m_surfaceCacheRootSig);
    DX_SAFE_RELEASE(m_quadVertexBuffer);
    DX_SAFE_RELEASE(m_outputTexture);
    
    m_initialized = false;
}

D3D12_GPU_DESCRIPTOR_HANDLE GIVisualization::GetGPUHandle(uint32_t slot)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += slot * m_descriptorSize;
    return handle;
}

void GIVisualization::CreateOutputTexture()
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_screenWidth;
    texDesc.Height = m_screenHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 匹配 BackBuffer 格式
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_outputTexture)
    );
    
    if (FAILED(hr))
    {
        DebuggerPrintf("[GIVisualization] ERROR: CreateOutputTexture failed! HR=0x%08X\n", hr);
        return;
    }
    
    m_outputTexture->SetName(L"GIVisualization_Output");
    
    // 创建 UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 匹配纹理格式
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += VIZ_OUTPUT_UAV * m_descriptorSize;
    
    m_device->CreateUnorderedAccessView(m_outputTexture, nullptr, &uavDesc, cpuHandle);
    
    DebuggerPrintf("[GIVisualization] Output texture created at UAV slot %d\n", VIZ_OUTPUT_UAV);
}

void GIVisualization::CreateRootSignatures()
{
    DebuggerPrintf("[GIVisualization] Creating Root Signatures...\n");
    
    // ===== Fullscreen Compute Root Signature =====
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0,
                      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
        
        CD3DX12_DESCRIPTOR_RANGE1 uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0,
                      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);
        
        CD3DX12_ROOT_PARAMETER1 rootParams[3];
        rootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
        rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        rootParams[2].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
        
        D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
        
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].ShaderRegister = 1;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        samplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // 与 Composite 一致
        samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[2].ShaderRegister = 2;
        samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(3, rootParams, 3, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);
        
        ID3DBlob* signature = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
        if (FAILED(hr))
        {
            if (error) { DebuggerPrintf("[GIVisualization] Fullscreen RootSig Error: %s\n", (char*)error->GetBufferPointer()); error->Release(); }
            return;
        }
        
        hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_fullscreenRootSig));
        signature->Release();
        if (error) error->Release();
        
        if (FAILED(hr))
        {
            DebuggerPrintf("[GIVisualization] ERROR: CreateRootSignature (Fullscreen) failed!\n");
            return;
        }
        m_fullscreenRootSig->SetName(L"GIViz_Fullscreen_RootSig");
        DebuggerPrintf("[GIVisualization] Fullscreen RootSig created OK\n");
    }
    
    // ===== Surface Cache VS/PS Root Signature =====
    {
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0,
                      D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
        
        CD3DX12_ROOT_PARAMETER1 rootParams[2];
        rootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE, D3D12_SHADER_VISIBILITY_ALL);
        rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        
        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].ShaderRegister = 1;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
        rootSigDesc.Init_1_1(2, rootParams, 2, samplers, 
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        
        ID3DBlob* signature = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
        if (FAILED(hr))
        {
            if (error) { DebuggerPrintf("[GIVisualization] SurfaceCache RootSig Error: %s\n", (char*)error->GetBufferPointer()); error->Release(); }
            return;
        }
        
        hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_surfaceCacheRootSig));
        signature->Release();
        if (error) error->Release();
        
        if (FAILED(hr))
        {
            DebuggerPrintf("[GIVisualization] ERROR: CreateRootSignature (SurfaceCache) failed!\n");
            return;
        }
        m_surfaceCacheRootSig->SetName(L"GIViz_SurfaceCache_RootSig");
        DebuggerPrintf("[GIVisualization] SurfaceCache RootSig created OK\n");
    }
}

void GIVisualization::CreatePipelineStates()
{
    DebuggerPrintf("[GIVisualization] Creating Pipeline States...\n");
    
    if (!m_fullscreenRootSig)
    {
        DebuggerPrintf("[GIVisualization] ERROR: Cannot create PSO - RootSig is null\n");
        return;
    }
    
    const char* shaderPath = "Data/Shaders/GIVisualization/GIVisualizationFullscreen.hlsl";
    std::string shaderCode;
    FileReadToString(shaderCode, shaderPath);
    
    if (shaderCode.empty())
    {
        DebuggerPrintf("[GIVisualization] ERROR: Failed to load shader: %s\n", shaderPath);
        return;
    }
    
    DebuggerPrintf("[GIVisualization] Loaded shader: %s (%zu bytes)\n", shaderPath, shaderCode.size());
    
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    
    ShaderIncludeHandler includeHandler("Data/Shaders/");
    ID3DBlob* csBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    HRESULT hr = D3DCompile(shaderCode.c_str(), shaderCode.size(), shaderPath,
        nullptr, &includeHandler, "CSMain", "cs_5_1", compileFlags, 0, &csBlob, &errorBlob);
    
    if (FAILED(hr))
    {
        if (errorBlob) 
        { 
            DebuggerPrintf("[GIVisualization] CS Compile Error:\n%s\n", (char*)errorBlob->GetBufferPointer()); 
            errorBlob->Release(); 
        }
        return;
    }
    if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }
    
    DebuggerPrintf("[GIVisualization] Shader compiled successfully\n");
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_fullscreenRootSig;
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_fullscreenPSO));
    csBlob->Release();
    
    if (FAILED(hr))
    {
        DebuggerPrintf("[GIVisualization] ERROR: CreateComputePipelineState failed! HR=0x%08X\n", hr);
        return;
    }
    
    m_fullscreenPSO->SetName(L"GIViz_Fullscreen_PSO");
    DebuggerPrintf("[GIVisualization] Fullscreen PSO created successfully\n");
}

void GIVisualization::CreateSurfaceCacheResources()
{
    DebuggerPrintf("[GIVisualization] Creating SurfaceCache Resources...\n");
    
    // 创建 Quad 顶点缓冲 - 使用 [0,1] 范围
    // Origin 是 Card 中心点，shader 中会 (uv - 0.5) * WorldSize
    struct QuadVertex
    {
        float Position[3];
        float UV[2];
    };
    
    QuadVertex quadVertices[6] = {
        // 第一个三角形
        { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },  // 左下
        { {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },  // 右下
        { {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },  // 右上
        // 第二个三角形
        { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },  // 左下
        { {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },  // 右上
        { {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },  // 左上
    };
    
    const uint32_t vbSize = sizeof(quadVertices);
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
    
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_quadVertexBuffer)
    );
    
    if (FAILED(hr))
    {
        DebuggerPrintf("[GIVisualization] ERROR: Failed to create quad VB\n");
        return;
    }
    
    m_quadVertexBuffer->SetName(L"GIViz_QuadVB");
    
    void* mappedData = nullptr;
    m_quadVertexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, quadVertices, vbSize);
    m_quadVertexBuffer->Unmap(0, nullptr);
    
    m_quadVBView.BufferLocation = m_quadVertexBuffer->GetGPUVirtualAddress();
    m_quadVBView.SizeInBytes = vbSize;
    m_quadVBView.StrideInBytes = sizeof(QuadVertex);
    
    // Surface Cache PSO
    if (!m_surfaceCacheRootSig)
    {
        DebuggerPrintf("[GIVisualization] ERROR: Cannot create SC PSO - RootSig is null\n");
        return;
    }
    
    const char* shaderPath = "Data/Shaders/GIVisualization/GIVisualizationSurfaceCache.hlsl";
    std::string shaderCode;
    FileReadToString(shaderCode, shaderPath);
    
    if (shaderCode.empty())
    {
        DebuggerPrintf("[GIVisualization] Warning: Failed to load SC shader: %s\n", shaderPath);
        return;
    }
    
    DebuggerPrintf("[GIVisualization] Loaded SC shader: %zu bytes\n", shaderCode.size());
    
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    
    ShaderIncludeHandler includeHandler("Data/Shaders/");
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    hr = D3DCompile(shaderCode.c_str(), shaderCode.size(), shaderPath,
        nullptr, &includeHandler, "VSMain", "vs_5_1", compileFlags, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob) { DebuggerPrintf("[GIVisualization] VS Error:\n%s\n", (char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
        return;
    }
    if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }
    DebuggerPrintf("[GIVisualization] SC VS compiled OK\n");
    
    hr = D3DCompile(shaderCode.c_str(), shaderCode.size(), shaderPath,
        nullptr, &includeHandler, "PSMain", "ps_5_1", compileFlags, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob) { DebuggerPrintf("[GIVisualization] PS Error:\n%s\n", (char*)errorBlob->GetBufferPointer()); errorBlob->Release(); }
        vsBlob->Release();
        return;
    }
    if (errorBlob) errorBlob->Release();
    DebuggerPrintf("[GIVisualization] SC PS compiled OK\n");
    
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_surfaceCacheRootSig;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    // 启用 Alpha 混合以支持边缘羽化
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_surfaceCachePSO));
    
    vsBlob->Release();
    psBlob->Release();
    
    if (SUCCEEDED(hr))
    {
        m_surfaceCachePSO->SetName(L"GIViz_SurfaceCache_PSO");
        DebuggerPrintf("[GIVisualization] SurfaceCache PSO created successfully\n");
    }
    else
    {
        DebuggerPrintf("[GIVisualization] ERROR: CreateGraphicsPipelineState (SC) failed! HR=0x%08X\n", hr);
    }
}

void GIVisualization::Execute(ID3D12GraphicsCommandList* cmdList, 
                               ConstantBuffer* constantBuffer,
                               const GIVisualizationParams& params,
                               const CompositeConstants& compositeConsts,
                               const ScreenProbeConstants& screenProbeConsts,
                               const CameraConstants& cameraConsts,
                               SurfaceCache* surfaceCache,
                               uint32_t activeCardCount)
{
    //// 调试：前10帧每帧打印，之后每60帧打印一次
    //static int frameCount = 0;
    //bool shouldLog = (frameCount < 10) || (frameCount % 60 == 0);
    //frameCount++;
    //
    //if (shouldLog)
    //{
    //    DebuggerPrintf("[GIVisualization] Execute frame %d: mode=%d, initialized=%d\n", 
    //                   frameCount, (int)params.Mode, m_initialized);
    //}
    //
    //if (!m_initialized)
    //{
    //    if (shouldLog) DebuggerPrintf("[GIVisualization] SKIP: not initialized\n");
    //    return;
    //}
    
    GIVisualizationMode mode = params.Mode;
    
    if (IsSurfaceCacheMode(mode))
    {
        /*if (shouldLog)
        {
            DebuggerPrintf("[GIVisualization] -> SurfaceCache mode, layer=%u, cardCount=%u\n",
                           GetSurfaceCacheLayerIndex(mode), activeCardCount);
        }
        */
        uint32_t layer = GetSurfaceCacheLayerIndex(mode);
        ExecuteSurfaceCacheVisualize(cmdList, constantBuffer, layer, params.Exposure, cameraConsts, surfaceCache, activeCardCount);
    }
    else
    {
        /*if (shouldLog)
        {
            DebuggerPrintf("[GIVisualization] -> Fullscreen mode, will Dispatch\n");
        }*/
        
        ExecuteFullscreenCompute(cmdList, constantBuffer, params, compositeConsts, screenProbeConsts);
    }
}

void GIVisualization::ExecuteFullscreenCompute(ID3D12GraphicsCommandList* cmdList,
                                                ConstantBuffer* constantBuffer,
                                                const GIVisualizationParams& params,
                                                const CompositeConstants& compositeConsts,
                                                const ScreenProbeConstants& screenProbeConsts)
{
    if (!m_fullscreenPSO)
    {
        DebuggerPrintf("[GIVisualization] ExecuteFullscreenCompute: PSO is NULL, skipping!\n");
        return;
    }
    
    if (!m_fullscreenRootSig)
    {
        DebuggerPrintf("[GIVisualization] ExecuteFullscreenCompute: RootSig is NULL, skipping!\n");
        return;
    }
    
    if (!constantBuffer)
    {
        DebuggerPrintf("[GIVisualization] ExecuteFullscreenCompute: constantBuffer is NULL, skipping!\n");
        return;
    }
    
    VisualizationConstants constants = {};
    
    constants.ClipToRenderTransform = compositeConsts.ClipToRenderTransform;
    constants.RenderToCameraTransform = compositeConsts.RenderToCameraTransform;
    constants.CameraToWorldTransform = compositeConsts.CameraToWorldTransform;
    
    constants.LightWorldToCamera = compositeConsts.LightWorldToCamera;
    constants.LightCameraToRender = compositeConsts.LightCameraToRender;
    constants.LightRenderToClip = compositeConsts.LightRenderToClip;
    
    memcpy(constants.SunColor, compositeConsts.SunColor, sizeof(constants.SunColor));
    memcpy(constants.SunNormal, compositeConsts.SunNormal, sizeof(constants.SunNormal));
    constants.ShadowMapSize = compositeConsts.ShadowMapSize;
    
    constants.AmbientColor[0] = compositeConsts.AmbientColor.x;
    constants.AmbientColor[1] = compositeConsts.AmbientColor.y;
    constants.AmbientColor[2] = compositeConsts.AmbientColor.z;
    constants.AmbientIntensity = compositeConsts.AmbientIntensity;
    
    constants.Mode = static_cast<uint32_t>(params.Mode);
    constants.Exposure = params.Exposure;
    constants.ScreenWidth = m_screenWidth;
    constants.ScreenHeight = m_screenHeight;
    
    constants.ProbeGridWidth = screenProbeConsts.ProbeGridWidth;
    constants.ProbeGridHeight = screenProbeConsts.ProbeGridHeight;
    constants.ProbeSpacing = screenProbeConsts.ProbeSpacing;
    constants.OctahedronSize = screenProbeConsts.OctahedronSize;
    
    constants.DirectIntensity = compositeConsts.DirectIntensity;
    constants.IndirectIntensity = compositeConsts.IndirectIntensity;
    constants.AOStrength = compositeConsts.AOStrength;
    
    constants.VoxelGridMin[0] = screenProbeConsts.VoxelGridMin.x;
    constants.VoxelGridMin[1] = screenProbeConsts.VoxelGridMin.y;
    constants.VoxelGridMin[2] = screenProbeConsts.VoxelGridMin.z;
    constants.VoxelSize = screenProbeConsts.VoxelSize;
    constants.VoxelGridMax[0] = screenProbeConsts.VoxelGridMax.x;
    constants.VoxelGridMax[1] = screenProbeConsts.VoxelGridMax.y;
    constants.VoxelGridMax[2] = screenProbeConsts.VoxelGridMax.z;
    constants.VoxelResolution = screenProbeConsts.VoxelResolution;
    
    constants.GlobalSDFCenter[0] = screenProbeConsts.GlobalSDFCenter.x;
    constants.GlobalSDFCenter[1] = screenProbeConsts.GlobalSDFCenter.y;
    constants.GlobalSDFCenter[2] = screenProbeConsts.GlobalSDFCenter.z;
    constants.GlobalSDFExtent = screenProbeConsts.GlobalSDFExtent;
    constants.GlobalSDFInvExtent[0] = screenProbeConsts.GlobalSDFInvExtent.x;
    constants.GlobalSDFInvExtent[1] = screenProbeConsts.GlobalSDFInvExtent.y;
    constants.GlobalSDFInvExtent[2] = screenProbeConsts.GlobalSDFInvExtent.z;
    constants.GlobalSDFResolution = screenProbeConsts.GlobalSDFResolution;
    
    constants.RadiosityProbeGridWidth = screenProbeConsts.AtlasWidth / 4;
    constants.RadiosityProbeGridHeight = screenProbeConsts.AtlasHeight / 4;
    constants.AtlasWidth = screenProbeConsts.AtlasWidth;
    constants.AtlasHeight = screenProbeConsts.AtlasHeight;
    
    constantBuffer->AppendData(&constants, sizeof(constants), 0);
    
    // 设置管线
    cmdList->SetComputeRootSignature(m_fullscreenRootSig);
    cmdList->SetPipelineState(m_fullscreenPSO);
    
    cmdList->SetComputeRootConstantBufferView(0, constantBuffer->GetDX12ConstantBuffer()->GetGPUVirtualAddress());
    cmdList->SetComputeRootDescriptorTable(1, GetGPUHandle(0));
    cmdList->SetComputeRootDescriptorTable(2, GetGPUHandle(VIZ_OUTPUT_UAV));
    
    uint32_t groupsX = (m_screenWidth + 7) / 8;
    uint32_t groupsY = (m_screenHeight + 7) / 8;
    
    // static int dispatchCount = 0;
    // if (dispatchCount < 10 || dispatchCount % 60 == 0)
    // {
    //     DebuggerPrintf("[GIVisualization] Dispatch(%u, %u, 1) - screen %ux%u\n", 
    //                    groupsX, groupsY, m_screenWidth, m_screenHeight);
    // }
    // dispatchCount++;
    
    cmdList->Dispatch(groupsX, groupsY, 1);
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_outputTexture);
    cmdList->ResourceBarrier(1, &barrier);
}

//=============================================================================
// CopyOutputToTarget - 将 Compute 输出复制到目标（如 BackBuffer）
// targetCurrentState: 目标当前的资源状态（默认 RENDER_TARGET）
//=============================================================================
void GIVisualization::CopyOutputToTarget(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* target,
                                          D3D12_RESOURCE_STATES targetCurrentState)
{
    if (!m_outputTexture || !target)
    {
        DebuggerPrintf("[GIVisualization] CopyOutputToTarget: null resource (output=%p, target=%p)\n", 
                       m_outputTexture, target);
        return;
    }
    
	/*static int copyCount = 0;
	if (copyCount < 5)
	{
		DebuggerPrintf("[GIVisualization] CopyOutputToTarget: copying to BackBuffer\n");
	}
	copyCount++;*/
    
    // Transition: UAV -> COPY_SOURCE, Target -> COPY_DEST
    CD3DX12_RESOURCE_BARRIER barriers[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_outputTexture, 
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(target, 
            targetCurrentState, D3D12_RESOURCE_STATE_COPY_DEST)
    };
    cmdList->ResourceBarrier(2, barriers);
    
    // Copy
    cmdList->CopyResource(target, m_outputTexture);
    
    // Transition back
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_outputTexture, 
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(target, 
        D3D12_RESOURCE_STATE_COPY_DEST, targetCurrentState);
    cmdList->ResourceBarrier(2, barriers);
}

void GIVisualization::ExecuteSurfaceCacheVisualize(ID3D12GraphicsCommandList* cmdList,
                                                    ConstantBuffer* constantBuffer,
                                                    uint32_t layerIndex,
                                                    float exposure,
                                                    const CameraConstants& cameraConsts,
                                                    SurfaceCache* surfaceCache,
                                                    uint32_t activeCardCount)
{
    if (!m_surfaceCachePSO)
    {
        DebuggerPrintf("[GIVisualization] ExecuteSurfaceCache: PSO is NULL!\n");
        return;
    }
    
    if (!surfaceCache)
    {
        DebuggerPrintf("[GIVisualization] ExecuteSurfaceCache: surfaceCache is NULL!\n");
        return;
    }
    
    if (activeCardCount == 0)
    {
        DebuggerPrintf("[GIVisualization] ExecuteSurfaceCache: activeCardCount is 0!\n");
        return;
    }
    
    SurfaceCacheVizConstants constants = {};
    constants.WorldToCameraTransform = cameraConsts.WorldToCameraTransform;
    constants.CameraToRenderTransform = cameraConsts.CameraToRenderTransform;
    constants.RenderToClipTransform = cameraConsts.RenderToClipTransform;
    constants.SurfaceCacheLayer = layerIndex;
    constants.AtlasSize = surfaceCache->m_atlasSize;
    constants.ActiveCardCount = activeCardCount;
    constants.Exposure = exposure;
    
    constantBuffer->AppendData(&constants, sizeof(constants), 0);
    
    cmdList->SetGraphicsRootSignature(m_surfaceCacheRootSig);
    cmdList->SetPipelineState(m_surfaceCachePSO);
    
    cmdList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetDX12ConstantBuffer()->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(1, GetGPUHandle(0));
    
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_quadVBView);
    
    /*static int drawCount = 0;
    if (drawCount < 10 || drawCount % 60 == 0)
    {
        DebuggerPrintf("[GIVisualization] DrawInstanced(6, %u) - layer %u\n", activeCardCount, layerIndex);
    }
    drawCount++;*/
    
    cmdList->DrawInstanced(6, activeCardCount, 0, 0);
}

bool GIVisualization::RenderImGuiPanel(GIVisualizationParams& params)
{
    bool changed = false;
    
    ImGui::Begin("GI Visualization", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    // 状态显示
    ImGui::Text("Status: %s", m_initialized ? "Initialized" : "NOT INITIALIZED");
    ImGui::Text("Fullscreen PSO: %s", m_fullscreenPSO ? "OK" : "FAILED");
    ImGui::Text("SurfaceCache PSO: %s", m_surfaceCachePSO ? "OK" : "FAILED");
    ImGui::Separator();
    
    // 快捷按钮
    if (ImGui::Button("Final")) { params.Mode = GIVisualizationMode::FinalLighting; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Direct")) { params.Mode = GIVisualizationMode::DirectLightingOnly; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Indirect")) { params.Mode = GIVisualizationMode::IndirectLightingOnly; changed = true; }
    
    ImGui::Separator();
    
    // 模式选择
    const char* modeNames[] = {
        "Final Lighting",
        "Direct Lighting",
        "Indirect Lighting",
        "SurfaceCache: Albedo",
        "SurfaceCache: Normal",
        "SurfaceCache: Direct",
        "SurfaceCache: Indirect",
        "SurfaceCache: Combined",
        "Voxel Lighting",
        "Radiosity Trace",
        "ScreenProbe: BRDF PDF",
        "ScreenProbe: Lighting PDF",
        "ScreenProbe: MeshSDF Trace",
        "ScreenProbe: Radiance Oct",
        "ScreenProbe: Radiance Filtered",
        "MeshSDF: Normal"
    };
    
    int currentMode = static_cast<int>(params.Mode);
    if (ImGui::Combo("Mode", &currentMode, modeNames, IM_ARRAYSIZE(modeNames)))
    {
        params.Mode = static_cast<GIVisualizationMode>(currentMode);
        changed = true;
    }
    
    ImGui::Separator();
    changed |= ImGui::SliderFloat("Exposure", &params.Exposure, 0.1f, 5.0f);
    
    ImGui::End();
    
    return changed;
}

#endif // ENGINE_DX12_RENDERER