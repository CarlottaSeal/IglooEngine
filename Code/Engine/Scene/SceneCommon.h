#pragma once
#include <cstdint>
#include <d3d12.h>
#include <vector>

#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/Mat44.hpp"

constexpr int MAX_MESH_COUNT = 128;

struct SurfaceCard;

static std::vector<float> GetCommonScales()
{
	return { 0.5f, 1.0f, 2.0f };  // 只支持这3个scale
}

struct GIObjectEntry
{
    uint32_t m_objectID;
    Mat44 m_worldTransform;
    AABB3 m_worldBounds;
    //std::vector<SurfaceCard> m_cardInstances;
    std::vector<uint32_t> m_cardIDs;
    
    uint32_t m_lastUpdateFrame;
    bool m_isDirty;
};

struct CardPriority
{
    uint32_t cardID;
    SurfaceCard* card;
    float priority;
    uint32_t tileCount;  // 占用的tiles数量
    float priorityPerTile;
};

struct LRUNode
{
    uint32_t m_tileIndex;
    LRUNode* m_prev = nullptr;
    LRUNode* m_next = nullptr;
};

enum SceneTileFlags
{
    TILE_FLAG_DIRTY,
    TILE_FLAG_AFFECTED,
    TILE_FLAG_ALLOCATED,
    TILE_FLAG_COUNT
};
