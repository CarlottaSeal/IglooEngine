#pragma once
#include "Engine/Math/IntVec2.hpp"
struct SurfaceCard;

namespace RadiosityConfig
{
    constexpr uint32_t PROBE_SPACING = 4;      // Surface Cache 上每 4 像素一个 Probe
    constexpr uint32_t RAYS_PER_PROBE = 16;    // 每个 Probe 16 根光线
    constexpr float TEMPORAL_BLEND = 1.0f;    // 禁用 probe 级别 temporal
    constexpr float TRACE_DISTANCE = 200.0f;   // 追踪距离
    constexpr uint32_t TRACE_MAX_STEPS = 64;   // 最大步数
}

struct SurfaceCardMetadata
{
	uint32_t m_atlasX;           
	uint32_t m_atlasY;           
	uint32_t m_resolutionX;      
	uint32_t m_resolutionY;      
 
	float m_originX, m_originY, m_originZ;
	float m_padding0;
 
	float m_axisXx, m_axisXy, m_axisXz;
	int m_objectID;
 
	float m_axisYx, m_axisYy, m_axisYz;
	float m_padding2;
 
	float m_normalX, m_normalY, m_normalZ;
	float m_padding3;
 
	float m_worldSizeX, m_worldSizeY;
	uint32_t  m_direction;         // 0-5
	uint32_t  m_globalCardID;
 
	uint32_t m_lightMask[4];     // 支持128个lights
};

struct SurfaceCacheStats
{
    uint32_t m_allocatedTiles = 0;
    uint32_t m_dirtyTiles = 0;
    uint32_t m_accessCount = 0;

    uint32_t m_cacheHits = 0;
    uint32_t m_cacheMisses = 0;
    uint32_t m_evictedTiles = 0;
    uint32_t m_temporalUpdates = 0;
    float m_cacheHitRate = 0.f;
    float m_memoryUsageMB = 0.f;
};

enum SurfaceCacheLayerType
{
    SURFACE_CACHE_LAYER_ALBEDO,
    SURFACE_CACHE_LAYER_NORMAL,
    SURFACE_CACHE_LAYER_MATERIAL,
    SURFACE_CACHE_LAYER_DIRECT_LIGHT,
    SURFACE_CACHE_LAYER_INDIRECT_LIGHT,
    SURFACE_CACHE_LAYER_COMBINED_LIGHT,
    SURFACE_CACHE_LAYER_COUNT
};

struct CardAllocation
{
	IntVec2 m_baseCoord = IntVec2(-1, -1);
	IntVec2 m_tileCount = IntVec2(0, 0);
	IntVec2 m_pixelCoord = IntVec2(-1, -1);
	IntVec2 m_pixelResolution = IntVec2(0, 0);

	bool IsValid() const { return m_baseCoord.x >= 0 && m_baseCoord.y >= 0; }
};

struct TraceResult
{
	float   HitPosition[3];
	float   HitDistance;
	float   HitNormal[3];
	float   Validity;
	uint32_t    HitCardIndex;    
    uint32_t    Padding0;        
    uint32_t    Padding1;
    uint32_t    Padding2;
};

struct ScreenProbeGPU
{
	uint32_t    ScreenX;
    uint32_t    ScreenY;
    uint32_t    Padding0;      // ← 添加
    uint32_t    Padding1;      // ← 添加（确保 16 字节对齐）
    float  WorldPosition[3];
    float   Depth;         // ← 移到这里，填充 float3 后的空隙
    float  WorldNormal[3];
    float   Validity;      // ← 移到这里
};

// SH2 系数 (L0 + L1 = 4 个系数)
struct SH2CoeffsGPU
{
	float R[4];  // [L0, L1_x, L1_y, L1_z] for Red
	float G[4];
	float B[4];
};

struct ImportanceSampleGPU
{
	float Direction[3];     // 采样方向
	float PDF;              // 概率密度
};
