#include "VoxelScene.h"

#include "SDFCommon.h"
#include "Engine/Core/EngineCommon.hpp"
#include "ThirdParty/d3dx12/d3dx12.h"

void VoxelScene::Initialize(ID3D12Device* device, const AABB3& sceneBounds, const IntVec3& resolution)
{
    if (m_initialized)
        return;
    
    m_sceneBounds = sceneBounds;
    m_resolution = resolution;

    Vec3 sceneSize = sceneBounds.GetBoundsSize();
    m_voxelSize = Vec3(
        sceneSize.x / (float)resolution.x,
        sceneSize.y / (float)resolution.y,
        sceneSize.z / (float)resolution.z
    );
    
    CreateGlobalSDFTexture(device);
    CreateVoxelVisibilityBuffer(device);
    CreateVoxelLightingTexture(device);
    CreateInstanceInfoBuffer(device);
    CreateConstantBuffer(device);
    
    m_initialized = true;
    m_globalSDFDirty = true;
    m_visibilityDirty = true;
    
    DebuggerPrintf("[VoxelScene] Initialized: %dx%dx%d voxels, size: (%.2f, %.2f, %.2f)\n",
                   resolution.x, resolution.y, resolution.z,
                   m_voxelSize.x, m_voxelSize.y, m_voxelSize.z);
}

void VoxelScene::Shutdown()
{
    if (!m_initialized)
        return;
    
    DX_SAFE_RELEASE(m_globalSDFTexture);
    DX_SAFE_RELEASE(m_voxelVisibilityBuffer);
    DX_SAFE_RELEASE(m_voxelLightingTexture);
    DX_SAFE_RELEASE(m_instanceInfoBuffer);
    DX_SAFE_RELEASE(m_instanceInfoUploadBuffer);
    DX_SAFE_RELEASE(m_constantBuffer);
    
    m_instanceInfos.clear();
    m_initialized = false;
}

void VoxelScene::CreateGlobalSDFTexture(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = m_resolution.x;
    texDesc.Height = m_resolution.y;
    texDesc.DepthOrArraySize = m_resolution.z;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;  // R = 距离, G = 实例索引
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_globalSDFTexture)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Global SDF texture!");
    m_globalSDFTexture->SetName(L"VoxelScene_GlobalSDF");
}

void VoxelScene::CreateVoxelVisibilityBuffer(ID3D12Device* device)
{
    // 6 方向 × 每个 Voxel
    uint32_t totalVoxels = m_resolution.x * m_resolution.y * m_resolution.z;
    uint32_t bufferSize = sizeof(VoxelVisibilityGPU) * totalVoxels * 6;
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );
    
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_voxelVisibilityBuffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Voxel Visibility buffer!");
    m_voxelVisibilityBuffer->SetName(L"VoxelScene_VoxelVisibility");
}

void VoxelScene::CreateVoxelLightingTexture(ID3D12Device* device)
{
    // 使用 3D 纹理，Z 方向 × 6 存储 6 个方向
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = m_resolution.x;
    texDesc.Height = m_resolution.y;
    texDesc.DepthOrArraySize = m_resolution.z * 6;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_voxelLightingTexture)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Voxel Lighting texture!");
    m_voxelLightingTexture->SetName(L"VoxelScene_VoxelLighting");
}

void VoxelScene::CreateInstanceInfoBuffer(ID3D12Device* device)
{
    uint32_t bufferSize = sizeof(MeshSDFInfoGPU) * MAX_INSTANCES;
    
    // Default Buffer
    D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_instanceInfoBuffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Instance Info buffer!");
    m_instanceInfoBuffer->SetName(L"VoxelScene_InstanceInfo");
    
    // Upload Buffer
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_instanceInfoUploadBuffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Instance Info upload buffer!");
    m_instanceInfoUploadBuffer->SetName(L"VoxelScene_InstanceInfoUpload");
}

void VoxelScene::CreateConstantBuffer(ID3D12Device* device)
{
    uint32_t bufferSize = sizeof(VoxelSceneConstants);
    bufferSize = (bufferSize + 255) & ~255;  // 256 字节对齐
    
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create constant buffer!");
    m_constantBuffer->SetName(L"VoxelScene_Constants");
}

void VoxelScene::SetInstanceInfos(const std::vector<MeshSDFInfoGPU>& infos)
{
    m_instanceInfos = infos;
    m_instanceInfosDirty = true;
    m_globalSDFDirty = true;
    m_visibilityDirty = true;
    
    UpdateConstants();
}

void VoxelScene::UpdateConstants()
{
    VoxelSceneConstants constants = {};
    
    constants.SceneBoundsMinX = m_sceneBounds.m_mins.x;
    constants.SceneBoundsMinY = m_sceneBounds.m_mins.y;
    constants.SceneBoundsMinZ = m_sceneBounds.m_mins.z;
    constants.SceneBoundsMaxX = m_sceneBounds.m_maxs.x;
    constants.SceneBoundsMaxY = m_sceneBounds.m_maxs.y;
    constants.SceneBoundsMaxZ = m_sceneBounds.m_maxs.z;
    
    constants.VoxelResolutionX = m_resolution.x;
    constants.VoxelResolutionY = m_resolution.y;
    constants.VoxelResolutionZ = m_resolution.z;
    constants.TotalVoxelCount = m_resolution.x * m_resolution.y * m_resolution.z;
    
    constants.VoxelSizeX = m_voxelSize.x;
    constants.VoxelSizeY = m_voxelSize.y;
    constants.VoxelSizeZ = m_voxelSize.z;
    constants.InstanceCount = (uint32_t)m_instanceInfos.size();
    
    constants.MaxTraceSteps = 64;
    constants.SDFThreshold = m_voxelSize.x * 0.5f;
    constants.MaxTraceDistance = m_sceneBounds.GetBoundsSize().GetLength();
    
    void* mappedData = nullptr;
    HRESULT hr = m_constantBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedData, &constants, sizeof(VoxelSceneConstants));
        m_constantBuffer->Unmap(0, nullptr);
    }
}

void VoxelScene::UploadInstanceInfos(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_instanceInfosDirty || m_instanceInfos.empty())
        return;
    
    // 复制到 Upload Buffer
    void* mappedData = nullptr;
    HRESULT hr = m_instanceInfoUploadBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedData, m_instanceInfos.data(), sizeof(MeshSDFInfoGPU) * m_instanceInfos.size());
        m_instanceInfoUploadBuffer->Unmap(0, nullptr);
    }
    
    // Barrier: Common -> Copy Dest
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_instanceInfoBuffer,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    // 复制
    cmdList->CopyBufferRegion(
        m_instanceInfoBuffer, 0,
        m_instanceInfoUploadBuffer, 0,
        sizeof(MeshSDFInfoGPU) * m_instanceInfos.size()
    );
    
    // Barrier: Copy Dest -> SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_instanceInfoBuffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    m_instanceInfosDirty = false;
}

void VoxelScene::BuildGlobalSDF(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_globalSDFDirty || !m_buildGlobalSDFPSO || m_instanceInfos.empty())
        return;
    
    // 确保 Instance Infos 已上传
    UploadInstanceInfos(cmdList);
    
    // Barrier: Global SDF -> UAV
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_globalSDFTexture,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    // 设置 PSO 和 Root Signature
    cmdList->SetPipelineState(m_buildGlobalSDFPSO);
    cmdList->SetComputeRootSignature(m_rootSignature);
    
    // 绑定资源（根据你的 Root Signature 布局调整）
    // 假设：
    // b0: VoxelSceneConstants
    // t0: InstanceInfos
    // u0: GlobalSDF
    // Bindless SDF textures 通过 descriptor table 绑定
    
    cmdList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(1, m_instanceInfoBuffer->GetGPUVirtualAddress());
    // u0 和 bindless textures 需要通过 descriptor table 绑定
    
    // Dispatch
    uint32_t groupsX = (m_resolution.x + 3) / 4;
    uint32_t groupsY = (m_resolution.y + 3) / 4;
    uint32_t groupsZ = (m_resolution.z + 3) / 4;
    cmdList->Dispatch(groupsX, groupsY, groupsZ);
    
    // Barrier: UAV -> SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_globalSDFTexture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    m_globalSDFDirty = false;
    
    DebuggerPrintf("[VoxelScene] Global SDF built\n");
}

void VoxelScene::BuildVoxelVisibility(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_visibilityDirty || !m_buildVisibilityPSO)
        return;
    
    // 确保 Global SDF 已构建
    if (m_globalSDFDirty)
        BuildGlobalSDF(cmdList);
    
    // Barrier: Visibility Buffer -> UAV
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_voxelVisibilityBuffer,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    cmdList->SetPipelineState(m_buildVisibilityPSO);
    cmdList->SetComputeRootSignature(m_rootSignature);
    
    // 绑定资源
    cmdList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    // t0: GlobalSDF (SRV)
    // t1: InstanceInfos
    // u0: VoxelVisibility
    
    // Dispatch
    uint32_t groupsX = (m_resolution.x + 3) / 4;
    uint32_t groupsY = (m_resolution.y + 3) / 4;
    uint32_t groupsZ = (m_resolution.z + 3) / 4;
    cmdList->Dispatch(groupsX, groupsY, groupsZ);
    
    // Barrier: UAV -> SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_voxelVisibilityBuffer,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    m_visibilityDirty = false;
    
    DebuggerPrintf("[VoxelScene] Voxel Visibility built\n");
}

void VoxelScene::InjectLighting(ID3D12GraphicsCommandList* cmdList)
{
    if (!m_injectLightingPSO)
        return;
    
    // 确保 Visibility 已构建
    if (m_visibilityDirty)
        BuildVoxelVisibility(cmdList);
    
    // Barrier: Voxel Lighting -> UAV
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_voxelLightingTexture,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    cmdList->ResourceBarrier(1, &barrier);
    
    cmdList->SetPipelineState(m_injectLightingPSO);
    cmdList->SetComputeRootSignature(m_rootSignature);
    
    // 绑定资源
    cmdList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    // t0: VoxelVisibility
    // t1: InstanceInfos
    // t2: SurfaceCacheAtlas (需要外部绑定)
    // t3: CardMetadata (需要外部绑定)
    // u0: VoxelLighting
    
    // Dispatch
    uint32_t groupsX = (m_resolution.x + 3) / 4;
    uint32_t groupsY = (m_resolution.y + 3) / 4;
    uint32_t groupsZ = (m_resolution.z + 3) / 4;
    cmdList->Dispatch(groupsX, groupsY, groupsZ);
    
    // Barrier: UAV -> SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_voxelLightingTexture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
}

void VoxelScene::ClearVoxelLighting(ID3D12GraphicsCommandList* cmdList)
{
    // 可以用一个简单的 Clear Shader，或者在 InjectLighting 开始时清零
    // 这里省略，可以之后添加
    UNUSED(cmdList);
}