#include "GISystem.h"

#include <algorithm>

#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Renderer/DX11Renderer.hpp"
#include "Engine/Renderer/VulkanRenderer.h"
#include "Engine/Renderer/Cache/CardBVH.h"
#include "Engine/Renderer/Cache/RadianceCacheManager.h"
#include "Engine/Renderer/Cache/SurfaceCard.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Object/Mesh/MeshObject.h"
#include "Engine/Scene/SDF/SDFCommon.h"

extern Window* g_theWindow;

GISystem::GISystem(const GIConfig& config)
	: m_config(config)
{
	InitializeAtlasFreeList();

	m_radianceCacheManager = new RadianceCacheManager(); 
}

GISystem::~GISystem()
{
}

void GISystem::Startup()
{
#ifdef ENGINE_DX12_RENDERER 
	InitializeDXR(m_config.m_renderer->GetSubRenderer()->m_device);

	if (m_radianceCacheManager && m_config.m_renderer)
	{
		DX12Renderer* renderer = m_config.m_renderer->GetSubRenderer();
		RadianceCache* cache = renderer->GetRadianceCache();
        
		m_radianceCacheManager->Initialize(cache, m_scene);
	}
	
	m_initialized = true;
#endif
}

void GISystem::Shutdown()
{
	delete m_radianceCacheManager;
	m_radianceCacheManager = nullptr;
	delete m_cardBVH;
	m_cardBVH = nullptr;
}

void GISystem::BeginFrame(int frameIndex)
{
	m_frameIndex = frameIndex;

	m_globalStats.m_totalCacheHits = 0;
	m_globalStats.m_totalCacheMisses = 0;
}

void GISystem::EndFrame()
{
	UpdateStatistics();
}

void GISystem::SetScene(Scene* scene)
{
	m_scene = scene;
}

void GISystem::InitializeDXR(ID3D12Device5* device)
{
	m_dxrSupported = m_dxrAcceleration.Initialize(device);
}

void GISystem::SetDirtyCards(const std::vector<uint32_t>& cardIDs)
{
	m_dirtyCards = cardIDs;
	//m_needsCacheUpdate = !cardIDs.empty();
}

void GISystem::RemoveProcessedDirtyCards(size_t count)
{
	if (count >= m_dirtyCards.size())
	{
		m_dirtyCards.clear();
	}
	else
	{
		m_dirtyCards.erase(m_dirtyCards.begin(), m_dirtyCards.begin() + count);
	}
}

std::vector<uint32_t> GISystem::BuildUpdateList(uint32_t maxCardsPerFrame)
{
	std::vector<uint32_t> result;
#ifdef ENGINE_DX12_RENDERER
	std::vector<std::pair<uint32_t, float>> cardPriorities;
	cardPriorities.reserve(m_dirtyCards.size());
    
	Vec3 cameraPos = m_config.m_renderer->GetSubRenderer()->m_currentCam.CameraWorldPosition;
    
	for (uint32_t cardID : m_dirtyCards)
	{
		SurfaceCard* card = m_scene->GetSurfaceCardByID(cardID);
		if (!card)
			continue;
        
		MeshObject* obj = static_cast<MeshObject*>(m_scene->GetSceneObject(card->m_meshObjectID));
		if (!obj)
			continue;
        
		const CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
		if (!instance)
			continue;
        
		// 基于距离的优先级
		float distance = GetDistance3D(cameraPos, instance->m_worldOrigin);
		float priority = 1.0f / (1.0f + distance * 0.1f);
        
		cardPriorities.push_back({cardID, priority});
	}
    
	std::sort(cardPriorities.begin(), cardPriorities.end(),
			  [](const auto& a, const auto& b) { return a.second > b.second; });
    
	
	int count = MinI(maxCardsPerFrame, (int)cardPriorities.size());
	for (int i = 0; i < count; i++)
	{
		result.push_back(cardPriorities[i].first);
	}
    
	return result;
#endif
	UNUSED(maxCardsPerFrame);
	return result;
}

void GISystem::BuildCardBVH()
{
#ifdef ENGINE_DX12_RENDERER
	if (!m_cardBVH || !m_scene)
		return;
    
	// ========== 1. 收集所有 Card Metadata ==========
	//std::vector<SurfaceCardMetadata> allCards = m_scene->CollectAllCardMetadata();
    
	if (m_cardMetadataCPU.empty())
	{
		DebuggerPrintf("[GISystem] No cards to build BVH\n");
		return;
	}
    
	// ========== 2. 构建 BVH（CPU 端）==========
	m_cardBVH->Build(m_cardMetadataCPU);
    
	// ========== 3. 扁平化并上传到 GPU ==========
	std::vector<GPUCardBVHNode> gpuNodes;
	std::vector<uint32_t> gpuCardIndices;
	m_cardBVH->FlattenForGPU(gpuNodes, gpuCardIndices);
    
	// ========== 4. 让 Renderer 创建 GPU 资源 ==========
	m_config.m_renderer->GetSubRenderer()->CreateCardBVHBuffers(gpuNodes, gpuCardIndices);
#endif
}

float GISystem::GetAtlasUsage() const
{
	uint32_t atlasSize = m_config.m_primaryAtlasSize;
	uint32_t tileSize = m_config.m_primaryTileSize;
	size_t totalTiles = (atlasSize / tileSize) * (atlasSize / tileSize);
	size_t usedTiles = m_tileUsageMap.size();

	return (float)usedTiles / totalTiles;
}

void GISystem::FreeCardSpace(IntVec2 atlasCoord, IntVec2 tileCount)
{
	if (atlasCoord.x < 0 || atlasCoord.y < 0)
		return;

	FreeTiles(atlasCoord, tileCount);

	DebuggerPrintf("[GISystem] Freed %dx%d tiles at (%d,%d)\n",
		tileCount.x, tileCount.y, atlasCoord.x, atlasCoord.y);
}

void GISystem::CleanDirtyCards()
{
	m_dirtyCards.clear();
}

Vec3 GISystem::ReconstructWorldPosCPU(Vec2 screenPos, float depth, float screenWidth, float screenHeight, const Mat44& viewProjInverse)
{
	// 归一化到 [0,1]
	Vec2 uv = Vec2(screenPos.x / screenWidth, screenPos.y / screenHeight);

	// 转换到 NDC [-1,1]
	Vec2 ndc = uv * 2.0f - Vec2(1.f,1.f);
	ndc.y = -ndc.y; // D3D Y 翻转

	// 注意：shader 里用了 (1 - depth)
	float z = depth;

	Vec4 clipPos(ndc.x, ndc.y, z, 1.0f);

	// 乘逆矩阵得到 world space
	Vec4 worldPos = viewProjInverse.TransformHomogeneous3D(clipPos);
	return Vec3(worldPos.x / worldPos.w, worldPos.y / worldPos.w, worldPos.z / worldPos.w);
}

void GISystem::UpdateCardMetadata()
{
	for (auto& [objectID, entry] : m_scene->m_giRegistry)
    {
        MeshObject* obj = static_cast<MeshObject*>(m_scene->GetSceneObject(objectID));
        if (!obj)
            continue;
        
        for (uint32_t cardID : entry.m_cardIDs)
        {
            // 关键：从Scene获取SurfaceCard
            SurfaceCard* card = m_scene->GetSurfaceCardByID(cardID);
            if (!card || !card->m_resident)
                continue;
            
            // 获取对应的CardInstance
            CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
            if (!instance)
                continue;
            
            SurfaceCardMetadata meta = {};
            
            // 来自SurfaceCard：atlas位置和分辨率
            uint32_t tileSize = m_config.m_primaryTileSize;
            meta.m_atlasX = card->m_atlasCoord.x * tileSize;
            meta.m_atlasY = card->m_atlasCoord.y * tileSize;
            meta.m_resolutionX = card->m_pixelResolution.x;
            meta.m_resolutionY = card->m_pixelResolution.y;
            
            // 来自CardInstanceData：世界变换和LightMask
            meta.m_originX = instance->m_worldOrigin.x;
            meta.m_originY = instance->m_worldOrigin.y;
            meta.m_originZ = instance->m_worldOrigin.z;
            
            meta.m_axisXx = instance->m_worldAxisX.x;
            meta.m_axisXy = instance->m_worldAxisX.y;
            meta.m_axisXz = instance->m_worldAxisX.z;
            
            meta.m_axisYx = instance->m_worldAxisY.x;
            meta.m_axisYy = instance->m_worldAxisY.y;
            meta.m_axisYz = instance->m_worldAxisY.z;
            
            meta.m_normalX = instance->m_worldNormal.x;
            meta.m_normalY = instance->m_worldNormal.y;
            meta.m_normalZ = instance->m_worldNormal.z;
            
            meta.m_worldSizeX = instance->m_worldSize.x;
            meta.m_worldSizeY = instance->m_worldSize.y;
            
            memcpy(meta.m_lightMask, instance->m_lightMask, sizeof(meta.m_lightMask));
            
            meta.m_objectID = card->m_meshObjectID; 
            meta.m_direction = obj->GetMesh()->m_cardTemplates[card->m_templateIndex].m_direction;
            meta.m_globalCardID = cardID;
            m_cardMetadataCPU.push_back(meta);
        }
    }
    
}

const std::vector<SurfaceCardMetadata>& GISystem::GetCurrentSurfaceCardMetadataCPU()
{
	UpdateCardMetadata();
	return m_cardMetadataCPU;
}

void GISystem::UpdateStatistics()
{
	float hitRate = 0.0f;
	if (m_globalStats.m_totalCacheHits + m_globalStats.m_totalCacheMisses > 0) {
		hitRate = (float)m_globalStats.m_totalCacheHits /
			(m_globalStats.m_totalCacheHits + m_globalStats.m_totalCacheMisses);
	}
	m_globalStats.m_averageHitRate = hitRate;

	float memoryMB = 0.0f;
	memoryMB += (m_config.m_primaryAtlasSize * m_config.m_primaryAtlasSize * 16) / (1024.0f * 1024.0f);
	if (m_config.m_enableMultipleTypes)
	{
		memoryMB += (m_config.m_reflectionAtlasSize * m_config.m_reflectionAtlasSize * 16) / (1024.0f * 1024.0f);
		memoryMB += (m_config.m_giAtlasSize * m_config.m_giAtlasSize * 16) / (1024.0f * 1024.0f);
	}
	m_globalStats.m_memoryUsageMB = memoryMB;
}

void GISystem::InitializeAtlasFreeList()
{
	m_tileUsageMap.clear();

	uint32_t totalTiles = (m_config.m_primaryAtlasSize / m_config.m_primaryTileSize) *
		(m_config.m_primaryAtlasSize / m_config.m_primaryTileSize);
	m_tileUsageMap.reserve(totalTiles);
}

IntVec2 GISystem::FindFreeRegion(uint32_t tilesX, uint32_t tilesY)
{
	uint32_t atlasSize = m_config.m_primaryAtlasSize;
	uint32_t tileSize = m_config.m_primaryTileSize;
	uint32_t maxTilesPerRow = atlasSize / tileSize;

	if (tilesX > maxTilesPerRow || tilesY > maxTilesPerRow)
	{
		return IntVec2(-1, -1);
	}

	for (uint32_t y = 0; y <= maxTilesPerRow - tilesY; y++)
	{
		for (uint32_t x = 0; x <= maxTilesPerRow - tilesX; x++)
		{
			if (IsRegionFree(x, y, tilesX, tilesY))
			{
				return IntVec2(x, y);
			}
		}
	}

	return IntVec2(-1, -1); 
}

bool GISystem::IsRegionFree(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	for (uint32_t ty = y; ty < y + h; ty++)
	{
		for (uint32_t tx = x; tx < x + w; tx++)
		{
			IntVec2 coord(tx, ty);
			if (m_tileUsageMap.find(coord) != m_tileUsageMap.end())
			{
				return false;  // 已被占用
		}
	}
}
	return true;
}

void GISystem::MarkTilesUsed(IntVec2 baseCoord, IntVec2 tileCount)
{
	for (int ty = 0; ty < tileCount.y; ty++)
	{
		for (int tx = 0; tx < tileCount.x; tx++)
		{
			IntVec2 coord(baseCoord.x + tx, baseCoord.y + ty);
			m_tileUsageMap[coord] = true;
		}
	}
}

void GISystem::FreeTiles(IntVec2 baseCoord, IntVec2 tileCount)
{
	for (int ty = 0; ty < tileCount.y; ty++)
	{
		for (int tx = 0; tx < tileCount.x; tx++)
		{
			IntVec2 coord(baseCoord.x + tx, baseCoord.y + ty);
			m_tileUsageMap.erase(coord);
		}
	}
}

Vec2 GISystem::WorldToScreen(const Vec3& worldPos)
{
#ifdef ENGINE_DX11_RENDERER
	UNUSED(worldPos)
	return Vec2();
#endif
#ifdef ENGINE_DX12_RENDERER
	const auto& cam = m_config.m_renderer->GetSubRenderer()->m_currentCam;
	const Mat44& worldToCamera = cam.WorldToCameraTransform;   
	const Mat44& cameraToRender = cam.CameraToRenderTransform; 
	const Mat44& renderToClip = cam.RenderToClipTransform; 

	Mat44 worldToCLip = renderToClip;
	worldToCLip.Append(cameraToRender);
	worldToCLip.Append(worldToCamera);

	Vec4 clipPos = worldToCLip.TransformHomogeneous3D(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));

	if (fabsf(clipPos.w) < 1e-6f)
		return Vec2(-1.0f, -1.0f);

	const float invW = 1.0f / clipPos.w;
	const float ndcX = clipPos.x * invW; // [-1,1]
	const float ndcY = clipPos.y * invW; // [-1,1]

	// NDC [-1,1] → 屏幕像素坐标 [0, W/H]
	const IntVec2 win = m_config.m_window->GetClientDimensions();
	Vec2 screenPos;
	screenPos.x = (ndcX + 1.0f) * 0.5f * static_cast<float>(win.x);
	screenPos.y = (1.0f - ndcY) * 0.5f * static_cast<float>(win.y); // 顶点坐标系：左上为(0,0)

	return screenPos;
#endif

#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(worldPos)
	return Vec2();
#endif
}

AABB2 GISystem::CalculateScreenBounds(const Vec3& minWorld, const Vec3& maxWorld)
{
	AABB3 box = AABB3(minWorld, maxWorld);
	auto corners = box.GetCorners();

	float minX = FLT_MAX, minY = FLT_MAX;
	float maxX = -FLT_MAX, maxY = -FLT_MAX;

	for (const Vec3& corner : corners)
	{
		Vec2 screenPos = WorldToScreen(corner);  
		if (screenPos.x >= 0)  
		{
			minX = min(minX, screenPos.x);
			maxX = max(maxX, screenPos.x);
			minY = min(minY, screenPos.y);
			maxY = max(maxY, screenPos.y);
		}
	}
	return AABB2(Vec2(minX, minY), Vec2(maxX, maxY));
}

CardAllocation GISystem::AllocateCardSpace(IntVec2 resolution)
{
	CardAllocation alloc;
	alloc.m_baseCoord = IntVec2(-1, -1);
	alloc.m_tileCount = IntVec2(0, 0);

	uint32_t tileSize = m_config.m_primaryTileSize;
	uint32_t tilesX = (resolution.x + tileSize - 1) / tileSize;
	uint32_t tilesY = (resolution.y + tileSize - 1) / tileSize;

	const uint32_t MAX_TILES_PER_DIM = 8;
	if (tilesX > MAX_TILES_PER_DIM || tilesY > MAX_TILES_PER_DIM)
	{
		DebuggerPrintf("[GISystem] Card resolution too large: %dx%d (max %d tiles per dimension)\n",
			resolution.x, resolution.y, MAX_TILES_PER_DIM);
		return alloc;
	}

	alloc.m_tileCount = IntVec2(tilesX, tilesY);
	alloc.m_baseCoord = FindFreeRegion(tilesX, tilesY);

	if (alloc.IsValid())
	{
		MarkTilesUsed(alloc.m_baseCoord, alloc.m_tileCount);
        
		alloc.m_pixelCoord.x = alloc.m_baseCoord.x * tileSize;
		alloc.m_pixelCoord.y = alloc.m_baseCoord.y * tileSize;
        
		alloc.m_pixelResolution.x = tilesX * tileSize;
		alloc.m_pixelResolution.y = tilesY * tileSize;

		DebuggerPrintf("[GISystem] Allocated %dx%d tiles at tile(%d,%d) pixel(%d,%d) for resolution %dx%d\n",
			tilesX, tilesY, 
			alloc.m_baseCoord.x, alloc.m_baseCoord.y,
			alloc.m_pixelCoord.x, alloc.m_pixelCoord.y,
			resolution.x, resolution.y);
	}
	else
	{
		DebuggerPrintf("[GISystem] Failed to allocate space for %dx%d resolution\n",
			resolution.x, resolution.y);
	}

	return alloc;
}