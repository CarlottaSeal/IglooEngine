#pragma once
#include "Engine/Math/IntVec2.hpp"
struct SurfaceCard;

struct SurfaceCardMetadata
{
	uint32_t m_atlasX;           // Atlas像素坐标X
	uint32_t m_atlasY;           // Atlas像素坐标Y
	uint32_t m_resolutionX;      // Card分辨率X
	uint32_t m_resolutionY;      // Card分辨率Y
 
	float m_originX, m_originY, m_originZ;
	float m_padding0;
 
	float m_axisXx, m_axisXy, m_axisXz;
	float m_padding1;
 
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

enum class CardCaptureMode
{
	INITIAL_FULL,
	TIME_SLICED,
	DIRTY_ONLY
};

struct CardCaptureStats
{
	int m_cardsUpdatedThisFrame = 0;
	int m_totalCardsUpdated = 0;
	float m_totalCaptureTime = 0.0f;
};