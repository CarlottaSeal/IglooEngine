#pragma once
#include <queue>
#include <unordered_map>

#include "Engine/Renderer/Cache/SurfaceCache.h"
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Renderer/DXR/DXRAcceleration.h"

class CardBVH;
class Scene;

struct SurfaceCacheGlobalStats
{
    uint32_t m_totalAllocatedTiles = 0;
    uint32_t m_totalDirtyTiles = 0;
    uint32_t m_totalCacheHits = 0;
    uint32_t m_totalCacheMisses = 0;
    float m_averageHitRate = 0.0f;
    float m_memoryUsageMB = 0.0f;
};

class GISystem
{
    friend class DX12Renderer;
    friend class Scene;
public:
    GISystem(const GIConfig& config);
    ~GISystem();
    
    void Startup();
    void Shutdown();
    void BeginFrame(int frameIndex);
    void EndFrame();

    //Set scene
    void SetScene(Scene* scene);
    
    //DXR
    void InitializeDXR(ID3D12Device5* device);
    bool IsDXRSupported() const { return m_dxrSupported; }
    
    void SetDirtyCards(const std::vector<uint32_t>& cardIDs);
    const std::vector<uint32_t>& GetDirtyCards() const { return m_dirtyCards; }
    void RemoveProcessedDirtyCards(size_t count);
    void MarkAllCardsDirty();

    std::vector<uint32_t> BuildUpdateList(uint32_t maxCardsPerFrame);

    CardBVH* GetCardBVH() { return m_cardBVH; }
    
    void BuildCardBVH();
    
    float GetAtlasUsage() const;
    CardAllocation AllocateCardSpace(IntVec2 resolution);
    void FreeCardSpace(IntVec2 atlasCoord, IntVec2 tileCount);
    void CleanDirtyCards();

    Vec3 ReconstructWorldPosCPU(Vec2 screenPos, float depth,
        float screenWidth, float screenHeight,
        const Mat44& viewProjInverse);
    // SurfaceCacheConstants PrepareBasicCacheConstants(SurfaceCacheType type, size_t batchStart);
    void UpdateCardMetadata();
    const std::vector<SurfaceCardMetadata>& GetCurrentSurfaceCardMetadataCPU();
    
    void UpdateStatistics();
    const SurfaceCacheGlobalStats& GetStatistics() const { return m_globalStats; }

private:
    void InitializeAtlasFreeList();
	IntVec2 FindFreeRegion(uint32_t tilesX, uint32_t tilesY);
	bool IsRegionFree(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	void MarkTilesUsed(IntVec2 baseCoord, IntVec2 tileCount);
	void FreeTiles(IntVec2 baseCoord, IntVec2 tileCount);

    Vec2 WorldToScreen(const Vec3& worldPos);
    AABB2 CalculateScreenBounds(const Vec3& minWorld, const Vec3& maxWorld);
public:
    GIConfig m_config;

private:
    int m_frameIndex;
    bool m_initialized;

    Scene* m_scene;
    std::unordered_map<IntVec2, bool> m_tileUsageMap;         
    std::unordered_map<uint32_t, uint32_t> m_atlasToWorld;  // atlas索引到世界tile的映射 <-应该没用了

    CardBVH* m_cardBVH;
    
    SurfaceCacheGlobalStats m_globalStats;
    
    std::vector<uint32_t> m_dirtyCards;
    std::vector<SurfaceCardMetadata> m_cardMetadataCPU; 

    DXRAcceleration m_dxrAcceleration;
    bool m_dxrSupported = false;
};



