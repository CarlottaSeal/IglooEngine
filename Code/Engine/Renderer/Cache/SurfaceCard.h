#pragma once
#include <cstdint>

#include "Engine/Math/Vec3.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/Mat44.hpp"
#include "Engine/Math/IntVec2.hpp"
#include <vector>

class MeshObject;

enum class CardDirection : uint8_t
{
    POSITIVE_X = 0,  
    NEGATIVE_X = 1,  
    POSITIVE_Y = 2,  
    NEGATIVE_Y = 3,  
    POSITIVE_Z = 4,  
    NEGATIVE_Z = 5,  
    COUNT = 6
};

struct CardGenerationConfig
{
    float m_minSurfaceArea = 0.1f;  
    float m_texelsPerMeter = 128.0f;
    IntVec2 m_minResolution = IntVec2(32, 32);
    IntVec2 m_maxResolution = IntVec2(512, 512);
    bool m_forceAllDirections = false;
};

// SurfaceCardTemplate - 模板（per-mesh，只保几何）
// 模板只提供"如何把表面参数化"，不携带atlas占位信息
struct SurfaceCardTemplate
{
    uint8_t m_direction = 0;                    // 0-5 对应 ±X, ±Y, ±Z
    Vec2 m_localSize = Vec2(1.f, 1.f);          // Local space尺寸
    IntVec2 m_recommendedResolution = IntVec2(64, 64); // 推荐分辨率 TODO
    
    // 用于统计/调试（不用于实际atlas分配）
    uint32_t m_templateID = 0;

    // Local Space辅助方法
    Vec3 GetLocalNormal() const;
    Vec3 GetLocalAxisX() const;
    Vec3 GetLocalAxisY() const;
    Vec3 GetLocalOrigin(const AABB3& localBounds) const;
};

// CardInstanceData - 实例（per-object，只保语义与状态）
// 实例负责"这张卡在世界里长什么样、受哪些光影响、是否脏"
struct CardInstanceData
{
    uint32_t m_meshObjectID = UINT32_MAX;
    uint32_t m_templateIndex = 0;             
    
    Vec3 m_worldOrigin = Vec3(0, 0, 0);
    Vec3 m_worldAxisX = Vec3(1, 0, 0);
    Vec3 m_worldAxisY = Vec3(0, 0, 1);
    Vec3 m_worldNormal = Vec3(0, 1, 0);
    Vec2 m_worldSize = Vec2(1, 1);
    
    uint32_t m_lightMask[4] = {0};              // 支持128个lights
    
    bool m_isDirty = true;
    uint32_t m_lastUpdateFrame = 0;
    
    uint32_t m_surfaceCardId = UINT32_MAX;      // 指向SurfaceCard的ID
    
    CardInstanceData()
    {
        memset(m_lightMask, 0, sizeof(m_lightMask));
        m_isDirty = true;
        m_lastUpdateFrame = 0;
    }
};

// SurfaceCard - Atlas中的页（由 SurfaceCache/GISystem 管）
struct SurfaceCard
{
    IntVec2 m_atlasCoord = IntVec2(-1, -1);     // Tile坐标
    IntVec2 m_atlasTileSpan = IntVec2(0, 0);    // 占用的tiles数量 (tilesX, tilesY)
    
    IntVec2 m_atlasPixelCoord = IntVec2(-1, -1);  
    IntVec2 m_pixelResolution = IntVec2(64, 64);// 像素分辨率
    
    bool m_resident = false;                  
    bool m_pendingRealloc = false;            
    uint32_t m_lastTouchedFrame = 0;          
    float m_priority = 0.0f;                  
    
    uint32_t m_meshObjectID = UINT32_MAX;
    uint32_t m_templateIndex = 0;
    
    bool m_pendingUpdate = false;               // 本帧需要更新
    uint32_t m_updateRateTier = 0;              // 更新频率档位（0=每帧，1=每2帧，2=每4帧）
    
    IntVec2 m_oldAtlasCoord = IntVec2(-1, -1);
    IntVec2 m_oldTileSpan = IntVec2(0, 0);
    
    uint32_t m_globalCardID = 0;
    
    bool IsValid() const { return m_atlasCoord.x >= 0 && m_atlasCoord.y >= 0; }
    bool NeedsReallocation() const { return m_pendingRealloc; }
};

