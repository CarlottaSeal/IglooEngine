#include "RadianceCacheManager.h"

#include <algorithm>

#include "Engine/Scene/Scene.h"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/GI/GBufferData.h"
#include "Engine/Core/EngineCommon.hpp"
#ifdef ENGINE_DX12_RENDERER

IntVec3 ProbeHashGrid::WorldToCell(const Vec3& worldPos) const
{
    return IntVec3(
        (int)floorf(worldPos.x / m_cellSize),
        (int)floorf(worldPos.y / m_cellSize),
        (int)floorf(worldPos.z / m_cellSize)
    );
}

void ProbeHashGrid::Insert(uint32_t probeIndex, const Vec3& worldPos)
{
    IntVec3 cell = WorldToCell(worldPos);
    m_grid[cell].push_back(probeIndex);
}

void ProbeHashGrid::Remove(uint32_t probeIndex, const Vec3& worldPos)
{
    IntVec3 cell = WorldToCell(worldPos);
    auto it = m_grid.find(cell);
    if (it != m_grid.end())
    {
        auto& probes = it->second;
        probes.erase(std::remove(probes.begin(), probes.end(), probeIndex), probes.end());
        
        if (probes.empty())
        {
            m_grid.erase(it);
        }
    }
}

std::vector<uint32_t> ProbeHashGrid::Query(const Vec3& worldPos, float radius) const
{
    std::vector<uint32_t> result;
    
    IntVec3 centerCell = WorldToCell(worldPos);
    int cellRadius = (int)ceilf(radius / m_cellSize);
    
    for (int dx = -cellRadius; dx <= cellRadius; dx++)
    {
        for (int dy = -cellRadius; dy <= cellRadius; dy++)
        {
            for (int dz = -cellRadius; dz <= cellRadius; dz++)
            {
                IntVec3 cell = centerCell + IntVec3(dx, dy, dz);
                auto it = m_grid.find(cell);
                if (it != m_grid.end())
                {
                    for (uint32_t probeIndex : it->second)
                    {
                        result.push_back(probeIndex);
                    }
                }
            }
        }
    }
    
    return result;
}

void ProbeHashGrid::Clear()
{
    m_grid.clear();
}

// ========== RadianceCacheManager 实现 ==========

void RadianceCacheManager::Initialize(RadianceCache* cache, Scene* scene)
{
    m_cache = cache;
    m_scene = scene;
    
    // 配置参数
    m_minProbeSpacing = 2.0f;      // 最小间隔 2 米
    m_maxProbeDistance = 50.0f;    // 最远 50 米
    m_probesPerFrame = 256;        // 每帧更新 256 个
    
    // 初始化空间 Hash Grid
    m_spatialGrid.m_cellSize = m_minProbeSpacing * 2.0f;
    
    DebuggerPrintf("[RadianceCacheManager] Initialized\n");
}

void RadianceCacheManager::PlaceProbesScreenSpace(
    const Camera& camera,
    const GBufferData& gbuffer)
{
    UNUSED(gbuffer);
    // ========== 屏幕空间采样 ==========
    // 在屏幕上每隔 N 个像素采样一次深度，放置 Probe

    AABB2 cameraBounds = camera.GetOrthographicBounds();
    Vec2 cameraSize = cameraBounds.GetDimensions();
    uint32_t screenWidth = (uint32_t)cameraSize.x;
    uint32_t screenHeight = (uint32_t)cameraSize.y;
    uint32_t sampleStep = 32;  // 每 32 像素采样一次 TODO
    
    Mat44 viewProjInverse = camera.GetProjectionMatrix();
    viewProjInverse.GetOrthonormalInverse();
    
    for (uint32_t y = 0; y < screenHeight; y += sampleStep)
    {
        for (uint32_t x = 0; x < screenWidth; x += sampleStep)
        {
            // 🔴 这里需要从 GBuffer 读取深度
            // 由于我们在 CPU 端，无法直接读取 GPU 的深度缓冲
            // 实际实现可以：
            // 1. 使用 Compute Shader 在 GPU 端放置 Probes
            // 2. 或者只在关键位置手动放置 Probes
            
            // 为了示例，我这里用简化版：假设深度为 10 米
            float depth = 0.9f;  // NDC 深度
            
            // 重建世界坐标
            float ndcX = (x + 0.5f) / screenWidth * 2.0f - 1.0f;
            float ndcY = 1.0f - (y + 0.5f) / screenHeight * 2.0f;
            
            Vec3 worldPos = ReconstructWorldPosition(
                Vec2(ndcX, ndcY), 
                depth, 
                viewProjInverse
            );
            
            // 检查是否应该放置 Probe
            if (ShouldPlaceProbe(worldPos))
            {
                Vec2 screenPos((float)x, (float)y);
                uint32_t probeIndex = m_cache->AllocateProbe(worldPos, screenPos);
                
                if (probeIndex != UINT32_MAX)
                {
                    // 加入 Hash Grid
                    m_spatialGrid.Insert(probeIndex, worldPos);
                    
                    RadianceProbe* probe = m_cache->GetProbe(probeIndex);
                    probe->m_birthFrame = m_scene->m_currentFrame;
                }
            }
        }
    }
}

Vec3 RadianceCacheManager::ReconstructWorldPosition(const Vec2& ndc, float depth, const Mat44& viewProjInv)
{
    Vec4 clipPos(ndc.x, ndc.y, depth, 1.0f);
    Vec4 worldPos = viewProjInv.TransformHomogeneous3D(clipPos);
    
    if (fabsf(worldPos.w) > 0.0001f)
    {
        worldPos.x /= worldPos.w;
        worldPos.y /= worldPos.w;
        worldPos.z /= worldPos.w;
    }
    
    return Vec3(worldPos.x, worldPos.y, worldPos.z);
}

bool RadianceCacheManager::ShouldPlaceProbe(const Vec3& worldPos) const
{
    // 检查附近是否已经有 Probe
    std::vector<uint32_t> nearby = m_spatialGrid.Query(worldPos, m_minProbeSpacing);
    
    if (!nearby.empty())
    {
        // 已经有很近的 Probe 了，不需要再放置
        return false;
    }
    
    return true;
}

void RadianceCacheManager::BuildPriorityQueue(const Camera& camera, uint32_t currentFrame)
{
    Vec3 cameraPos = camera.GetPosition();
    
    // 更新所有 active probes 的优先级
    uint32_t maxProbes = m_cache->GetMaxProbes();
    for (uint32_t i = 0; i < maxProbes; i++)
    {
        RadianceProbe* probe = m_cache->GetProbe(i);
        if (probe && probe->m_isActive)
        {
            probe->m_priority = CalculateProbePriority(probe, cameraPos, currentFrame);
        }
    }
    
    // 构建更新队列
    m_cache->BuildUpdateQueue(m_probesPerFrame);
}

float RadianceCacheManager::CalculateProbePriority(
    const RadianceProbe* probe, 
    const Vec3& cameraPos, 
    uint32_t currentFrame) const
{
    if (!probe)
        return 0.0f;
    
    // 距离因子（越近优先级越高）
    float distance = (probe->m_worldPosition - cameraPos).GetLength();
    float distanceFactor = 1.0f / (distance + 1.0f);
    
    // 时间因子（越久没更新优先级越高）
    uint32_t framesSinceUpdate = currentFrame - probe->m_lastUpdateFrame;
    float timeFactor = (float)framesSinceUpdate / 60.0f;  // 假设 60 帧为一个周期
    
    // 综合优先级
    float priority = distanceFactor * 10.0f + timeFactor;
    
    return priority;
}

void RadianceCacheManager::RecycleFarProbes(const Vec3& cameraPos, float maxDistance)
{
    uint32_t maxProbes = m_cache->GetMaxProbes();
    std::vector<uint32_t> toRecycle;
    
    for (uint32_t i = 0; i < maxProbes; i++)
    {
        RadianceProbe* probe = m_cache->GetProbe(i);
        if (probe && probe->m_isActive)
        {
            float distance = (probe->m_worldPosition - cameraPos).GetLength();
            if (distance > maxDistance)
            {
                toRecycle.push_back(i);
            }
        }
    }
    
    // 回收
    for (uint32_t index : toRecycle)
    {
        RadianceProbe* probe = m_cache->GetProbe(index);
        m_spatialGrid.Remove(index, probe->m_worldPosition);
        m_cache->FreeProbe(index);
    }
    
    if (!toRecycle.empty())
    {
        DebuggerPrintf("[RadianceCacheManager] Recycled %zu far probes\n", toRecycle.size());
    }
}

std::vector<uint32_t> RadianceCacheManager::FindNearbyProbes(const Vec3& worldPos, uint32_t maxCount) const
{
    // 从 Hash Grid 查询附近的 Probes
    std::vector<uint32_t> nearby = m_spatialGrid.Query(worldPos, 10.0f);  // 10 米范围内
    
    if (nearby.size() <= maxCount)
    {
        return nearby;
    }
    
    // 按距离排序，取最近的 N 个
    struct ProbeWithDistance
    {
        uint32_t index;
        float distance;
        
        bool operator<(const ProbeWithDistance& other) const
        {
            return distance < other.distance;
        }
    };
    
    std::vector<ProbeWithDistance> sorted;
    sorted.reserve(nearby.size());
    
    for (uint32_t index : nearby)
    {
        const RadianceProbe* probe = m_cache->GetProbe(index);
        if (probe && probe->m_isActive)
        {
            float dist = (probe->m_worldPosition - worldPos).GetLength();
            sorted.push_back({index, dist});
        }
    }
    
    std::sort(sorted.begin(), sorted.end());
    
    std::vector<uint32_t> result;
    result.reserve(maxCount);
    for (uint32_t i = 0; i < maxCount && i < sorted.size(); i++)
    {
        result.push_back(sorted[i].index);
    }
    
    return result;
}

#endif