#include "CombineSurfaceCache.h"
#include "SurfaceCache.h"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Renderer/ShaderIncludeHandler.h"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/DX12Renderer.hpp"
#include <d3dcompiler.h>

CombineSurfaceCache::~CombineSurfaceCache()
{
    Shutdown();
}

void CombineSurfaceCache::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* descriptorHeap)
{
    if (m_initialized)
        return;
    
    m_device = device;
    m_descriptorHeap = descriptorHeap;
    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    DebuggerPrintf("[CombineSurfaceCache] Initializing...\n");
    
    CreateRootSignature();
    CreatePipelineState();
    
    m_initialized = true;
    DebuggerPrintf("[CombineSurfaceCache] Initialized successfully\n");
}

void CombineSurfaceCache::Shutdown()
{
    if (!m_initialized)
        return;
    
    DX_SAFE_RELEASE(m_pso);
    DX_SAFE_RELEASE(m_rootSignature);
    
    m_device = nullptr;
    m_descriptorHeap = nullptr;
    m_initialized = false;
    
    DebuggerPrintf("[CombineSurfaceCache] Shutdown complete\n");
}

void CombineSurfaceCache::CreateRootSignature()
{
    // Root Signature 布局 (简化版):
    // [0] Descriptor Table: SRV t0 (CardMetadata)
    // [1] Descriptor Table: UAV u0 (Surface Cache Atlas - 读取和写入)
    
    CD3DX12_DESCRIPTOR_RANGE1 metaSrvRange;
    metaSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 
                      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    
    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 
                  D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    
    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsDescriptorTable(1, &metaSrvRange, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, 
                         D3D12_ROOT_SIGNATURE_FLAG_NONE);
    
    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[CombineSurfaceCache] Root Signature error: %s\n", 
                (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to serialize CombineSurfaceCache root signature!");
    }
    
    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
    signature->Release();
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create CombineSurfaceCache root signature!");
    m_rootSignature->SetName(L"CombineSurfaceCache_RootSignature");
}

void CombineSurfaceCache::CreatePipelineState()
{
    const char* shaderPath = "Data/Shaders/CombineSurfaceCache/SurfaceCacheCombine.hlsl";
    
    std::string shaderCode;
    FileReadToString(shaderCode, shaderPath);
    
    if (shaderCode.empty())
        GUARANTEE_OR_DIE(false, "Failed to load SurfaceCacheCombine.hlsl!");
    
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    ShaderIncludeHandler includeHandler("Data/Shaders/");
    
    HRESULT hr = D3DCompile(shaderCode.c_str(), shaderCode.size(), shaderPath,
        nullptr, &includeHandler, "main", "cs_5_1", compileFlags, 0, 
        &shaderBlob, &errorBlob);
    
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            DebuggerPrintf("[CombineSurfaceCache] Shader error:\n%s\n",
                (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to compile SurfaceCacheCombine.hlsl!");
    }
    
    if (errorBlob) errorBlob->Release();
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.CS = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
    
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
    shaderBlob->Release();
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create CombineSurfaceCache PSO!");
    m_pso->SetName(L"CombineSurfaceCache_PSO");
}

void CombineSurfaceCache::Execute(ID3D12GraphicsCommandList* cmdList, SurfaceCache* surfaceCache, int globalActiveCardCount, uint32_t maxCardSize)
{
    if (!m_initialized || !surfaceCache)
        return;

    uint32_t activeCardCount = globalActiveCardCount;
    if (activeCardCount == 0)
        return;

    if (maxCardSize == 0)
        maxCardSize = MAX_CARD_SIZE;

    // 1. Barrier: 整个 Atlas 转为 UAV 状态（用于读取和写入）
    CD3DX12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        surfaceCache->m_atlasTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
    );
    cmdList->ResourceBarrier(1, &preBarrier);

    cmdList->SetComputeRootSignature(m_rootSignature);
    cmdList->SetPipelineState(m_pso);

    // 3. 设置 Descriptor Tables
    // [0] t0 = CardMetadata SRV
    // [1] u0 = Atlas UAV
    cmdList->SetComputeRootDescriptorTable(0, GetGPUHandle(SURFCACHE_PRIMARY_META_SRV));   // t0
    cmdList->SetComputeRootDescriptorTable(1, GetGPUHandle(SURFCACHE_PRIMARY_ATLAS_UAV));  // u0

    uint32_t groupsXY = (maxCardSize + 7) / 8;
    cmdList->Dispatch(groupsXY, groupsXY, activeCardCount);
    
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(surfaceCache->m_atlasTexture);
    cmdList->ResourceBarrier(1, &uavBarrier);
    
    // 6. Barrier: 转回 SRV 状态
    CD3DX12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        surfaceCache->m_atlasTexture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
    );
    cmdList->ResourceBarrier(1, &postBarrier);
}

#endif // ENGINE_DX12_RENDERER