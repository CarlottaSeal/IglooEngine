#pragma once

#include "Engine/Math/Vec3.hpp"
#include "Engine/Math/AABB3.hpp"

#include <d3d12.h>
#include <vector>
#include <cstdint>

#include "Engine/Math/IntVec3.h"

struct MeshSDFInfoGPU;

struct VoxelVisibilityGPU
{
    uint32_t HitInstanceIndex;  // 0xFFFFFFFF = 未命中
    float HitDistance;
};

// struct VoxelSceneConstants
// {
//     float SceneBoundsMinX, SceneBoundsMinY, SceneBoundsMinZ;
//     float Pad0;
//     float SceneBoundsMaxX, SceneBoundsMaxY, SceneBoundsMaxZ;
//     float Pad1;
//     
//     uint32_t VoxelResolutionX, VoxelResolutionY, VoxelResolutionZ;
//     uint32_t TotalVoxelCount;
//     
//     float VoxelSizeX, VoxelSizeY, VoxelSizeZ;
//     uint32_t InstanceCount;
//     
//     uint32_t MaxTraceSteps;
//     float SDFThreshold;
//     float MaxTraceDistance;
//     float Pad2;
// };

class VoxelScene
{
public:
    VoxelScene() = default;
    ~VoxelScene() = default;
    
    // 生命周期
    void Initialize(ID3D12Device* device, const AABB3& sceneBounds, const IntVec3& resolution);
    void Shutdown();
    
    // 设置实例信息（从 Scene::BuildMeshSDFInfos 获取）
    void SetInstanceInfos(const std::vector<MeshSDFInfoGPU>& infos);
    
    // GPU 更新
    void UploadInstanceInfos(ID3D12GraphicsCommandList* cmdList);
    void BuildGlobalSDF(ID3D12GraphicsCommandList* cmdList);
    void BuildVoxelVisibility(ID3D12GraphicsCommandList* cmdList);
    void InjectLighting(ID3D12GraphicsCommandList* cmdList);
    void ClearVoxelLighting(ID3D12GraphicsCommandList* cmdList);
    
    ID3D12Resource* GetGlobalSDFTexture() const { return m_globalSDFTexture; }
    ID3D12Resource* GetVoxelVisibilityBuffer() const { return m_voxelVisibilityBuffer; }
    ID3D12Resource* GetVoxelLightingTexture() const { return m_voxelLightingTexture; }
    ID3D12Resource* GetInstanceInfoBuffer() const { return m_instanceInfoBuffer; }
    ID3D12Resource* GetConstantBuffer() const { return m_constantBuffer; }
    
    // 配置
    AABB3 GetSceneBounds() const { return m_sceneBounds; }
    IntVec3 GetResolution() const { return m_resolution; }
    Vec3 GetVoxelSize() const { return m_voxelSize; }
    uint32_t GetInstanceCount() const { return (uint32_t)m_instanceInfos.size(); }
    
    // PSO 设置（需要外部创建并设置）
    void SetBuildGlobalSDFPSO(ID3D12PipelineState* pso) { m_buildGlobalSDFPSO = pso; }
    void SetBuildVisibilityPSO(ID3D12PipelineState* pso) { m_buildVisibilityPSO = pso; }
    void SetInjectLightingPSO(ID3D12PipelineState* pso) { m_injectLightingPSO = pso; }
    void SetRootSignature(ID3D12RootSignature* rootSig) { m_rootSignature = rootSig; }
    
    // 标记需要重建
    void MarkGlobalSDFDirty() { m_globalSDFDirty = true; }
    void MarkVisibilityDirty() { m_visibilityDirty = true; }
    
private:
    void CreateGlobalSDFTexture(ID3D12Device* device);
    void CreateVoxelVisibilityBuffer(ID3D12Device* device);
    void CreateVoxelLightingTexture(ID3D12Device* device);
    void CreateInstanceInfoBuffer(ID3D12Device* device);
    void CreateConstantBuffer(ID3D12Device* device);
    void UpdateConstants();
    
    ID3D12Resource* m_globalSDFTexture = nullptr;        // RG32_FLOAT, 3D (距离 + 实例索引)
    ID3D12Resource* m_voxelVisibilityBuffer = nullptr;   // StructuredBuffer, 6方向
    ID3D12Resource* m_voxelLightingTexture = nullptr;    // RGBA16_FLOAT, 3D × 6方向
    ID3D12Resource* m_instanceInfoBuffer = nullptr;      // StructuredBuffer<MeshSDFInfoGPU>
    ID3D12Resource* m_instanceInfoUploadBuffer = nullptr;
    ID3D12Resource* m_constantBuffer = nullptr;
    
    // PSO（外部创建）
    ID3D12PipelineState* m_buildGlobalSDFPSO = nullptr;
    ID3D12PipelineState* m_buildVisibilityPSO = nullptr;
    ID3D12PipelineState* m_injectLightingPSO = nullptr;
    ID3D12RootSignature* m_rootSignature = nullptr;
    
    // CPU 端数据
    std::vector<MeshSDFInfoGPU> m_instanceInfos;
    
    // 配置
    AABB3 m_sceneBounds;
    IntVec3 m_resolution = IntVec3(128, 64, 128);
    Vec3 m_voxelSize;
    
    // 状态
    bool m_initialized = false;
    bool m_instanceInfosDirty = false;
    bool m_globalSDFDirty = true;
    bool m_visibilityDirty = true;
    
    // 常量
    static constexpr uint32_t MAX_INSTANCES = 256;
};