#include "Scene.h"

#include "Object/Mesh/MeshObject.h"

#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Renderer/SDFTexture3D.h"
#include "Engine/Renderer/Cache/SurfaceCard.h"
#include "Engine/Renderer/GI/GISystem.h"
#include <algorithm>
#include <utility>

Scene::Scene(SceneConfig const config)
    : m_config(config)
{
    m_meshManager = new MeshManager(this);
    InitializeRoughly();
    //m_octree = std::make_unique<Octree>(AABB3(Vec3(-1000), Vec3(1000)));
}

Scene::~Scene()
{
    for (auto& [cardID, card] : m_cardIDToCardPtr)
    {
        delete card;
    }
    m_cardIDToCardPtr.clear();
 
    m_giRegistry.clear();
    
    for (auto& [id, entity] : m_objects)
    {
        entity->OnDestroy();
    }
    m_objects.clear();
    m_meshObjects.clear();
    m_lightObjects.clear();
    m_allObjects.clear();
    delete m_meshManager;
    m_meshManager = nullptr;
}

void Scene::InitializeRoughly()
{
    m_meshObjects.reserve(1000);
    m_lightObjects.reserve(100);
    m_opaqueRenderItems.reserve(1000);
}

void Scene::InitializeBoundsAndMeshSDF()
{
    UpdateSceneBounds();
    BuildMeshSDFInfos();

    // Sort mesh objects by mesh pointer (primary) then material for instanced batching
    std::sort(m_meshObjects.begin(), m_meshObjects.end(),
        [](const MeshObject* a, const MeshObject* b)
        {
            const auto* meshA = a->GetMesh();
            const auto* meshB = b->GetMesh();
            if (meshA != meshB)
                return meshA < meshB;
            if (meshA->m_diffuseTexture != meshB->m_diffuseTexture)
                return meshA->m_diffuseTexture < meshB->m_diffuseTexture;
            if (meshA->m_normalTexture != meshB->m_normalTexture)
                return meshA->m_normalTexture < meshB->m_normalTexture;
            return meshA->m_specularTexture < meshB->m_specularTexture;
        });
}

void Scene::Update(float deltaTime)
{
    m_currentFrame++;
    bool anyLightMoved = false;

    for (auto* object : m_allObjects)
    {
        if (!object->IsActive())
            continue;

        object->Update(deltaTime);

        if (!object->HasMoved())
            continue;

        if (object->GetType() == OBJECT_MESH)
        {
            OnMeshObjectTransformChanged(object->GetID());
            object->ClearMoveFlag(); // 别忘了清标记
        }
        else if (object->GetType() == OBJECT_LIGHT)
        {
            anyLightMoved = true;
            m_pointLightShadowDirty = true; // shadows must re-render every frame

            // Expensive GI work (cards + metadata + DirectLightPass) throttled
            if (m_currentFrame % 10 == 0)
            {
                auto* light = static_cast<LightObject*>(object);
                m_suppressCaptureDirty = true;
                light->UpdateAffectedCards();
                m_suppressCaptureDirty = false;
                m_pointLightDirty = true;
            }

            object->ClearMoveFlag();
        }
        else
        {
            object->ClearMoveFlag();
        }
    }
    if (anyLightMoved)
    {
        ProduceLightVariables();
    }

    ProcessGIUpdates();

    // if (m_currentFrame % 60 == 0)
    // {
    //     CheckMemoryPressure();
    // }
}

void Scene::CheckMemoryPressure()
{
    // Card-based 方案可能不需要 LRU
    // 因为 cards 数量相对固定（每个 mesh 6 个左右）
    
    // 如果场景很大，可以实现：
    // - 驱逐不可见物体的 cards
    // - 降低远处物体的 card 分辨率
    
    // 简单实现：统计 atlas 使用率
    float atlasUsage = m_config.m_giSystem->GetAtlasUsage();
    
    if (atlasUsage > 0.95f)
    {
        EvictLowPriorityCards();
    }
}

void Scene::Render() const
{
}

MeshObject* Scene::CreateMeshEntity(const std::string& path, const std::string& name, Vec3 position, EulerAngles orientation)
{
    uint32_t id = m_nextEntityID++;
    auto entity = std::make_unique<MeshObject>(id, name, path, position, orientation);
    MeshObject* ptr = entity.get();

    entity->OnCreate(this);
    m_objects[id] = std::move(entity);
    
    AddObjectToLists(ptr);

	if (ShouldObjectHaveGI(ptr)) 
    {      
		RegisterMeshObjectForGI(ptr);
	}

    OnMeshObjectTransformChanged(id);
    return ptr;
}

LightObject* Scene::CreateLightEntity(const std::string& name, LightObjectType type, Vec3 position, Rgba8 sunColor, Vec3 sunDirection, Rgba8 lightColor, float
                                      ambience, Vec3 spotForward, float innerRadius, float outerRadius, float innerDotThreshold, float outerDotThreshold)
{
    uint32_t id = m_nextEntityID++;
    auto entity = std::make_unique<LightObject>(id, name, type, position, sunColor, sunDirection, lightColor, ambience,
        spotForward, innerRadius, outerRadius, innerDotThreshold, outerDotThreshold);
    LightObject* ptr = entity.get();

    m_objects[id] = std::move(entity);
    AddObjectToLists(ptr);      // 先添加到列表，这会调用 ReassignLightIDs() 设置 m_generalLightID
    ProduceLightVariables();    // 填充光源数据到数组（m_worldLightPositions, m_lightColors 等）
    ptr->OnCreate(this);        // 然后再 OnCreate，这样 UpdateAffectedCards 中 m_generalLightID 已经有效

    return ptr;
}

void Scene::UpdateCardMetadata()
{
    m_config.m_giSystem->UpdateCardMetadata();
}

void Scene::DestroyObject(uint32_t entityID)
{
    auto it = m_objects.find(entityID);
    if (it == m_objects.end()) return;

    if (auto* meshObj = dynamic_cast<MeshObject*>(it->second.get()))
    {
        auto gitIt = m_giRegistry.find(entityID);
        if (gitIt != m_giRegistry.end())
        {
            for (auto& cardID : gitIt->second.m_cardIDs)
            {
                // 从light的affected列表中移除
                for (auto* light : m_lightObjects)
                {
                    auto& affectedCards = light->m_affectedCards;
                    affectedCards.erase(
                        std::remove(affectedCards.begin(), affectedCards.end(), cardID),
                        affectedCards.end()
                    );
                }
                
                m_cardToLightObjects.erase(cardID);
            }
            
            // 新增：清理此对象的所有SurfaceCards
            CleanupSurfaceCardsForObject(entityID);
            
            m_giRegistry.erase(gitIt);
        }
        
        meshObj->m_cardInstances.clear();
    }
    
    it->second->OnDestroy();
    RemoveObjectFromLists(it->second.get());
    m_objects.erase(it);
}

SceneObject* Scene::GetSceneObject(uint32_t entityID)
{
    auto it = m_objects.find(entityID);
    if (it != m_objects.end())
    {
        return it->second.get();
    }
    return nullptr;
}

const SceneObject* Scene::GetSceneObject(uint32_t entityID) const
{
    auto it = m_objects.find(entityID);
    if (it != m_objects.end())
    {
        return it->second.get();
    }
    return nullptr;
}

uint32_t Scene::FindClosestObject(const Vec3& pos) //TODO：没用
{
    UNUSED(pos)
    //float minDist = FLT_MAX;
    uint32_t closestID = UINT32_MAX;
    
    // for (const auto& [id, instance] : m_sdfInstances)
    // {
    //     Vec3 localPos = instance.m_inverseTransform.TransformPosition3D(pos);
    //     float dist = instance.Sample(localPos);
    //     if (dist < minDist)
    //     {
    //         minDist = dist;
    //         closestID = id;
    //     }
    // }
    return closestID;
}

std::vector<MeshObject*> Scene::GetVisibleMeshes(const Camera& camera)
{
    //TODO: 视锥剔除
    UNUSED(camera);
    return m_meshObjects;
}

void Scene::ProduceLightVariables()
{
    m_worldLightPositions.clear();
    m_ambiences.clear();
    m_spotForwards.clear();
    m_lightColors.clear();
    m_innerRadii.clear();
    m_outerRadii.clear();
    m_innerDotThresholds.clear();
    m_outerDotThresholds.clear();
    m_numLights = (int)m_lightObjects.size() -1;
    for (int i = 0; i < m_lightObjects.size(); i++)
    {
        LightObject* light = m_lightObjects[i];
        //light->m_generalLightID = i;
        if (light->GetLightType() == LIGHT_DIRECTIONAL)
        {
            m_sunColor = light->m_sunColor;
            m_sunDirection = light->m_sunDirection;
        }
        if (light->GetLightType() == LIGHT_POINT || light->GetLightType() == LIGHT_SPOT)
        {
            m_worldLightPositions.push_back(light->m_position);
            m_lightColors.push_back(light->m_lightColor);
            m_spotForwards.push_back(light->m_spotForward);
            m_ambiences.push_back(light->m_ambience);
            m_innerRadii.push_back(light->m_innerRadius);
            m_outerRadii.push_back(light->m_outerRadius);
            m_innerDotThresholds.push_back(light->m_innerDotThresholds);
            m_outerDotThresholds.push_back(light->m_outerDotThresholds);
        }
    }
}

void Scene::ReassignLightIDs()
{
    int arrayIndex = 0;
	for (LightObject* light : m_lightObjects)
	{
		if (light->GetLightType() == LIGHT_DIRECTIONAL)
		{
			light->m_generalLightID = -1;
		}
		else
		{
			light->m_generalLightID = arrayIndex;
			arrayIndex++;
		}
	}
}

void Scene::SetLightConstants() const
{
    m_config.m_renderer->SetGeneralLightConstants(m_sunColor, m_sunDirection.GetNormalized(), m_numLights,
        m_lightColors, m_worldLightPositions, m_spotForwards, m_ambiences, m_innerRadii, m_outerRadii,
        m_innerDotThresholds, m_outerDotThresholds);
}

std::vector<uint32_t> Scene::RegisterLightInfluence(uint32_t lightID, const AABB3& bounds)
{
    std::vector<uint32_t> affectedCards;
    
    LightObject* light = (LightObject*)(GetSceneObject(lightID));
    if (!light) return affectedCards;
    
    DebuggerPrintf("[Scene] Registering light %u influence\n", lightID);
    
    for (auto& [objectID, entry] : m_giRegistry)
    {
        MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
        if (!obj)
            continue;
        
        // ✅ 修改：遍历m_surfaceCardIDs
        for (size_t i = 0; i < entry.m_cardIDs.size(); i++)
        {
            uint32_t cardID = entry.m_cardIDs[i];
            
            // ✅ 修改：通过GetSurfaceCardByID获取SurfaceCard
            SurfaceCard* card = GetSurfaceCardByID(cardID);
            if (!card)
                continue;
            
            // ✅ 修改：获取CardInstance计算bounds
            CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
            if (!instance)
                continue;
            
            // 计算card的世界bounds
            AABB3 cardBounds = ComputeCardWorldBounds(instance, card);
            
            if (!DoAABBsOverlap3D(bounds, cardBounds))
                continue;
            
            if (light->GetLightType() == LIGHT_SPOT)
            {
                Vec3 toCard = instance->m_worldOrigin - light->m_position;
                float dot = DotProduct3D(toCard.GetNormalized(), light->m_spotForward);
                if (dot < light->m_outerDotThresholds)
                    continue;
            }

            // 使用 generalLightID（在 LightsArray 中的索引）而不是 scene object ID
            // 这样才能与 shader 中的 lightIndex 对应
            int generalLightID = light->m_generalLightID;
            if (generalLightID < 0)  // 方向光没有 generalLightID，跳过
                continue;

            uint32_t wordIndex = generalLightID / 32;
            uint32_t bitIndex = generalLightID & 31;
            instance->m_lightMask[wordIndex] |= (1u << bitIndex);
            
            instance->m_isDirty = true;

            card->m_pendingUpdate = true;

            // Only add to capture-dirty list if not suppressed (e.g. light-only changes
            // use UpdateDirectLightPass instead of full CardCapture)
            if (!m_suppressCaptureDirty)
            {
                auto it = std::find(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end(), cardID);
                if (it == m_dirtyCardIDs.end())
                {
                    m_dirtyCardIDs.push_back(cardID);
                }
            }

            //DebuggerPrintf("[Scene]   Light %u affects card %u of object %u\n",
            //              lightID, cardID, objectID);

            m_cardToLightObjects[cardID].push_back(lightID);
            affectedCards.push_back(cardID);
        }
    }
    
    DebuggerPrintf("[Scene] Light %u affects %zu cards\n", 
                   lightID, affectedCards.size());
    
    return affectedCards;
}

void Scene::RemoveLightFromCard(uint32_t lightID, uint32_t tileIndex)
{
    auto it = m_cardToLightObjects.find(tileIndex);
    if (it != m_cardToLightObjects.end())
    {
        auto& lights = it->second;
        lights.erase(
            std::remove(lights.begin(), lights.end(), lightID),
            lights.end()
        );
    }
}

const std::vector<uint32_t>& Scene::GetLightsForCard(uint32_t cardID) const
{
    static const std::vector<uint32_t> empty;
    auto it = m_cardToLightObjects.find(cardID);
    return (it != m_cardToLightObjects.end()) ? it->second : empty;
}

void Scene::OnMeshObjectTransformChanged(uint32_t objectID)
{
    auto it = m_giRegistry.find(objectID);
    if (it == m_giRegistry.end())
        return;

    MeshObject* object = static_cast<MeshObject*>(GetSceneObject(objectID));
    if (!object)
        return;

    for (size_t i = 0; i < it->second.m_cardIDs.size(); ++i)
    {
        uint32_t cardID = it->second.m_cardIDs[i];
        
        if (i < object->m_cardInstances.size())
        {
            CardInstanceData& instance = object->m_cardInstances[i];
            
            memset(instance.m_lightMask, 0, sizeof(instance.m_lightMask));
            
            instance.m_isDirty = true;
        }
        
        SurfaceCard* card = GetSurfaceCardByID(cardID);
        if (card)
        {
            card->m_pendingUpdate = true;
        }
        
        m_cardToLightObjects.erase(cardID);
    }

    it->second.m_isDirty = true;
    
    RegisterCardsToLightSystem(objectID);
    
    for (uint32_t cardID : it->second.m_cardIDs)
    {
        auto dirtyIt = std::find(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end(), cardID);
        if (dirtyIt == m_dirtyCardIDs.end())
        {
            m_dirtyCardIDs.push_back(cardID);
        }
    }
    
    DebuggerPrintf("[Scene] Transform changed for object %u, marked %zu cards dirty\n",
                   objectID, it->second.m_cardIDs.size());
}

void Scene::ProcessGIUpdates()
{
    if (m_dirtyCardIDs.empty())
    return;
    
    std::sort(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end());
    m_dirtyCardIDs.erase(
        std::unique(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end()),
        m_dirtyCardIDs.end()
    );
    
    m_config.m_giSystem->SetDirtyCards(m_dirtyCardIDs);
    
    ClearDirtyCards();
}

void Scene::PrepareRenderData(const Camera& camera)
{
    UNUSED(camera);
    if (!m_renderDataDirty)
        return;
    
    // 清空上一帧数据
    m_opaqueRenderItems.clear();
    m_transparentRenderItems.clear();
    m_visibleMeshes.clear();
    m_activeLights.clear();
    
    // // 视锥剔除
    // Frustum frustum = camera.GetFrustum();
    // CullEntities(frustum);
    
    // 收集可见网格的渲染项
    for (MeshObject* mesh : m_visibleMeshes)
    {
        if (!mesh->IsVisible())
            continue;
            
        RenderItem item = mesh->GetRenderItem();
        
        // 根据材质判断是否透明
        
        // if (mesh->IsVisible() && material->isTransparent)
        // {
        //     m_transparentRenderItems.push_back(item);
        // }
        // else
        // {
            m_opaqueRenderItems.push_back(item);
        //}
    }
    
    // 排序渲染项
    //SortRenderItems(); TODO:有用吗？
    
    // for (MeshObject* mesh : GetVisibleMeshes(camera))
    // {
    //     for (SurfaceCard card : mesh->m_cardInstances)
    //     {
    //         card.m_lastUpdateFrame = m_currentFrame;
    //     }
    // }
    
    m_renderDataDirty = false;
}

void Scene::AddObjectToLists(SceneObject* object)
{
    m_allObjects.push_back(object);
    
    switch (object->GetType())
    {
    case SceneObjectType::OBJECT_MESH:
        m_meshObjects.push_back(static_cast<MeshObject*>(object));
        break;
    case SceneObjectType::OBJECT_LIGHT:
        m_lightObjects.push_back(static_cast<LightObject*>(object));
        ReassignLightIDs();
        break;
    }
    
    // 添加到空间加速结构
    // if (m_octree)
    // {
    //     m_octree->Insert(object);
    // }
    
    m_renderDataDirty = true;
}

void Scene::RemoveObjectFromLists(SceneObject* object)
{
    if (!object) return;
    
    auto it = std::find(m_allObjects.begin(), m_allObjects.end(), object);
    if (it != m_allObjects.end())
    {
        m_allObjects.erase(it);
    }
    
    switch (object->GetType())
    {
    case SceneObjectType::OBJECT_MESH:
        {
            auto meshIt = std::find(m_meshObjects.begin(), m_meshObjects.end(), 
                                   static_cast<MeshObject*>(object));
            if (meshIt != m_meshObjects.end())
            {
                m_meshObjects.erase(meshIt);
            }
        }
        break;
        
    case SceneObjectType::OBJECT_LIGHT:
        {
            auto lightIt = std::find(m_lightObjects.begin(), m_lightObjects.end(), 
                                    static_cast<LightObject*>(object));
            if (lightIt != m_lightObjects.end())
            {
                m_lightObjects.erase(lightIt);
                
                ReassignLightIDs();
            }
        }
        break;
    }
    
    // 从空间加速结构中移除（如果有的话）
    // if (m_octree) {
    //     m_octree->Remove(object);
    // }
    
    // 标记渲染数据需要更新 TODO: shanchu
    m_renderDataDirty = true;
}

bool Scene::ShouldObjectHaveGI(const MeshObject* object) const
{
    if (!object || !object->IsActive()) return false;
    
    // 策略：静态、足够大的物体参与GI
    //AABB3 bounds = object->GetLocalBounds();
    //float volume = bounds.GetVolume();
    Sphere sphere = object->GetLocalBoundsSphere();
    
    return object->IsStaticForGI() && sphere.m_radius > 0.1f;
}

uint32_t Scene::HashWorldPosition(const Vec3& pos) const
{
    const float cellSize = 1.0f;
    int x = (int)(pos.x / cellSize);
    int y = (int)(pos.y / cellSize);
    int z = (int)(pos.z / cellSize);
    
    return (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
}

void Scene::EvictLowPriorityCards()
{
#ifdef ENGINE_DX12_RENDERER
    if (!m_config.m_renderer || !m_config.m_giSystem)
        return;
    
    std::vector<CardPriority> candidates;
    
    // ===== 1. 收集所有可驱逐的cards =====
    Vec3 cameraPos = m_config.m_renderer->GetSubRenderer()->m_currentCam.CameraWorldPosition;
    
    for (auto& [objectID, entry] : m_giRegistry)
    {
        MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
        if (!obj)
            continue;
        
        for (uint32_t cardID : entry.m_cardIDs)
        {
            // ✅ 通过GetSurfaceCardByID获取SurfaceCard
            SurfaceCard* card = GetSurfaceCardByID(cardID);
            if (!card || !card->m_resident)
                continue;
            
            // 跳过pending update的cards（正在使用）
            if (card->m_pendingUpdate)
                continue;
            
            // ✅ 获取CardInstance计算优先级
            CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
            if (!instance)
                continue;
            
            // ===== 计算优先级 =====
            float priority = CalculateCardPriority(card, instance, cameraPos);
            
            // 计算占用的tiles数量
            uint32_t tileCount = card->m_atlasTileSpan.x * card->m_atlasTileSpan.y;
            
            CardPriority cp;
            cp.cardID = cardID;
            cp.card = card;
            cp.priority = priority;
            cp.tileCount = tileCount;
            
            candidates.push_back(cp);
        }
    }
    
    if (candidates.empty())
    {
        DebuggerPrintf("[Scene] No cards available for eviction\n");
        return;
    }
    
    // ===== 2. 按优先级排序（低优先级优先驱逐） =====
    std::sort(candidates.begin(), candidates.end(),
              [](const CardPriority& a, const CardPriority& b) {
                  return a.priority < b.priority;  // 优先级低的排前面
              });
    
    // ===== 3. 驱逐策略：驱逐最低优先级的10%或至少1张 =====
    size_t numToEvict = std::max<size_t>(1, candidates.size() / 10);
    
    DebuggerPrintf("[Scene] Evicting %zu / %zu cards\n", numToEvict, candidates.size());
    
    uint32_t freedTiles = 0;
    
    for (size_t i = 0; i < numToEvict && i < candidates.size(); i++)
    {
        SurfaceCard* card = candidates[i].card;
        
        // ✅ 释放atlas空间
        if (m_config.m_giSystem && card->m_atlasCoord.x >= 0)
        {
            m_config.m_giSystem->FreeCardSpace(
                card->m_atlasCoord,
                card->m_atlasTileSpan
            );
            
            freedTiles += candidates[i].tileCount;
            
            DebuggerPrintf("[Scene]   Evicted card %u (priority=%.3f, tiles=%u)\n",
                          card->m_globalCardID, candidates[i].priority, candidates[i].tileCount);
        }
        
        // ✅ 标记card为非resident
        card->m_resident = false;
        card->m_atlasCoord = IntVec2(-1, -1);
        card->m_atlasTileSpan = IntVec2(0, 0);
        
        // ✅ 标记为需要重新分配
        card->m_pendingUpdate = true;
        
        // 添加到脏卡列表（下次会重新分配和捕获）
        auto it = std::find(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end(), card->m_globalCardID);
        if (it == m_dirtyCardIDs.end())
        {
            m_dirtyCardIDs.push_back(card->m_globalCardID);
        }
    }
    
    DebuggerPrintf("[Scene] Eviction complete: freed %u tiles\n", freedTiles);
    
#endif
}

float Scene::CalculateCardPriority(SurfaceCard* card, CardInstanceData* instance, const Vec3& cameraPos)
{
    if (!card || !instance)
        return 0.0f;
    
    // ===== 1. 基于距离的优先级 =====
    float distance = GetDistance3D(cameraPos, instance->m_worldOrigin);
    float distancePriority = 1.0f / (1.0f + distance * 0.1f);  // 距离越远，优先级越低
    
    // ===== 2. 基于最后访问时间（LRU） =====
    uint32_t framesSinceTouch = m_currentFrame - card->m_lastTouchedFrame;
    float recencyPriority = 1.0f / (1.0f + framesSinceTouch * 0.01f);  // 越久未访问，优先级越低
    
    // ===== 3. 基于可见性（面向相机） =====
    Vec3 toCamera = (cameraPos - instance->m_worldOrigin).GetNormalized();
    float facingDot = DotProduct3D(instance->m_worldNormal, toCamera);
    float facingPriority = MaxF(0.0f, facingDot);  // 背对相机的优先级低
    
    // ===== 4. 基于分辨率（占用空间） =====
    // 大的card如果不常用，优先级应该更低（更应该被驱逐）
    uint32_t tileCount = card->m_atlasTileSpan.x * card->m_atlasTileSpan.y;
    float sizePenalty = 1.0f / (1.0f + tileCount * 0.1f);
    
    // ===== 组合优先级 =====
    // 权重：距离40%，时效性30%，可见性20%，大小10%
    float priority = distancePriority * 0.4f + 
                    recencyPriority * 0.3f + 
                    facingPriority * 0.2f +
                    sizePenalty * 0.1f;
    
    return priority;
}

void Scene::EvictCards(const std::vector<SurfaceCard*>& cards)
{
    uint32_t freedTiles = 0;
    
    for (SurfaceCard* card : cards)
    {
        if (!card || !card->m_resident)
            continue;
        
        // 释放空间
        if (m_config.m_giSystem && card->m_atlasCoord.x >= 0)
        {
            m_config.m_giSystem->FreeCardSpace(card->m_atlasCoord, card->m_atlasTileSpan);
            freedTiles += card->m_atlasTileSpan.x * card->m_atlasTileSpan.y;
        }
        
        // 标记
        card->m_resident = false;
        card->m_atlasCoord = IntVec2(-1, -1);
        card->m_atlasTileSpan = IntVec2(0, 0);
        card->m_pendingUpdate = true;
        
        m_dirtyCardIDs.push_back(card->m_globalCardID);
    }
    
    DebuggerPrintf("[Scene] Evicted %zu cards, freed %u tiles\n", 
                  cards.size(), freedTiles);
}

void Scene::EvictLowPriorityCards_Tiered()
{
    
#ifdef ENGINE_DX12_RENDERER
    // 驱逐策略分3层：
    // 1. 首先驱逐：超过5秒未访问 + 距离>100米的
    // 2. 其次驱逐：超过2秒未访问 + 背对相机的
    // 3. 最后驱逐：最低优先级的10%
    
    const uint32_t FRAMES_PER_SECOND = 60;  // 假设60fps
    
    Vec3 cameraPos = m_config.m_renderer->GetSubRenderer()->m_currentCam.CameraWorldPosition;
    std::vector<SurfaceCard*> toEvict;
    
    // ===== Tier 1: 长时间未用且很远的 =====
    for (auto& [cardID, card] : m_cardIDToCardPtr)
    {
        if (!card->m_resident || card->m_pendingUpdate)
            continue;
        
        uint32_t framesOld = m_currentFrame - card->m_lastTouchedFrame;
        
        // 获取距离
        MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(card->m_meshObjectID));
        if (!obj)
            continue;
        
        CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
        if (!instance)
            continue;
        
        float distance = GetDistance3D(cameraPos, instance->m_worldOrigin);
        
        // 超过5秒未访问 且 距离>100米
        if (framesOld > FRAMES_PER_SECOND * 5 && distance > 100.0f)
        {
            toEvict.push_back(card);
        }
    }
    
    if (!toEvict.empty())
    {
        DebuggerPrintf("[Scene] Tier 1 eviction: %zu cards (old & far)\n", toEvict.size());
        EvictCards(toEvict);
        return;
    }
    
    // ===== Tier 2: 背对相机且未用 =====
    for (auto& [cardID, card] : m_cardIDToCardPtr)
    {
        if (!card->m_resident || card->m_pendingUpdate)
            continue;
        
        uint32_t framesOld = m_currentFrame - card->m_lastTouchedFrame;
        
        MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(card->m_meshObjectID));
        if (!obj)
            continue;
        
        CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
        if (!instance)
            continue;
        
        Vec3 toCamera = (cameraPos - instance->m_worldOrigin).GetNormalized();
        float facingDot = DotProduct3D(instance->m_worldNormal, toCamera);
        
        // 超过2秒未访问 且 背对相机（dot < 0）
        if (framesOld > FRAMES_PER_SECOND * 2 && facingDot < 0.0f)
        {
            toEvict.push_back(card);
        }
    }
    
    if (!toEvict.empty())
    {
        DebuggerPrintf("[Scene] Tier 2 eviction: %zu cards (unused & facing away)\n", 
                      toEvict.size());
        EvictCards(toEvict);
        return;
    }
    
    // ===== Tier 3: 标准LRU驱逐 =====
    DebuggerPrintf("[Scene] Tier 3 eviction: standard LRU\n");
    EvictLowPriorityCards();
    
#endif
}

void Scene::UpdateSceneBounds()
{
    CalculateSceneBounds();
    m_needsRebuildGlobalLighting = true;
}

void Scene::CalculateSceneBounds()
{
     Vec3 mins = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
     Vec3 maxs = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    
     if (m_meshObjects.empty())
     {
         m_sceneBounds = AABB3(Vec3(-1.f, -1.f, -1.f), Vec3(1.f, 1.f, 1.f));
         return;
     }
    
     for (MeshObject* meshObj : m_meshObjects)
     {
         if (meshObj == nullptr)
             continue;
         
         AABB3 bounds = meshObj->GetWorldBounds();
     
         mins.x = (bounds.m_mins.x < mins.x) ? bounds.m_mins.x : mins.x;
         mins.y = (bounds.m_mins.y < mins.y) ? bounds.m_mins.y : mins.y;
         mins.z = (bounds.m_mins.z < mins.z) ? bounds.m_mins.z : mins.z;
     
         maxs.x = (bounds.m_maxs.x > maxs.x) ? bounds.m_maxs.x : maxs.x;
         maxs.y = (bounds.m_maxs.y > maxs.y) ? bounds.m_maxs.y : maxs.y;
         maxs.z = (bounds.m_maxs.z > maxs.z) ? bounds.m_maxs.z : maxs.z;
     }
    
     m_sceneBounds = AABB3(mins, maxs);
}

void Scene::BuildMeshSDFInfos()
{
    //return;
    m_meshInfos.clear();
    
    for (MeshObject* meshObj : m_meshObjects)
    {
        StaticMesh* mesh = meshObj->GetMesh();
        SDFTexture3D* sdf = mesh->GetSDF(meshObj->GetScale());
        if (sdf->GetSDFTextureIndex() == UINT32_MAX)
            continue;
        
        MeshSDFInfoGPU info = {};
        Mat44 worldTransform = meshObj->GetWorldMatrixWithoutScaling();
        info.LocalToWorld = worldTransform;
        info.WorldToLocal = worldTransform.GetOrthonormalInverse();
        info.SDFTextureIndex = sdf->GetSDFTextureIndex(); //bindless的索引，而非srvHeap中的索引
        info.LocalToWorldScale = meshObj->GetScale();
        AABB3 bounds = meshObj->GetMesh()->GetScaledBounds(1.f); //TODO
        info.LocalBoundsMin = bounds.m_mins;
        info.LocalBoundsMax = bounds.m_maxs;

        info.CardCount = (uint32_t)meshObj->m_cardInstances.size();
        info.CardStartIndex = meshObj->m_cardInstances[0].m_surfaceCardId;
        // //Test
        // Vec3 meshWorldCenter = meshObj->GetPosition();
        // Vec3 localPos = info.WorldToLocal.TransformPosition3D(meshWorldCenter);
        // DebuggerPrintf("Mesh world center: (%.3f, %.3f, %.3f)\n", meshWorldCenter.x, meshWorldCenter.y, meshWorldCenter.z);
        // DebuggerPrintf("LocalPos after transform: (%.3f, %.3f, %.3f)\n", localPos.x, localPos.y, localPos.z);
        // DebuggerPrintf("LocalBounds: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)\n", 
        //        bounds.m_mins.x, bounds.m_mins.y, bounds.m_mins.z,
        //        bounds.m_maxs.x, bounds.m_maxs.y, bounds.m_maxs.z);
        
        m_meshInfos.push_back(info);
    }
    DebuggerPrintf("[Scene] Built %zu instance SDF infos\n", m_meshInfos.size());
}

void Scene::EvictLowPriorityCards_Advanced(uint32_t targetTilesToFree)
{
#ifdef ENGINE_DX12_RENDERER
    if (!m_config.m_renderer || !m_config.m_giSystem)
        return;
    
    std::vector<CardPriority> candidates;
    Vec3 cameraPos = m_config.m_renderer->GetSubRenderer()->m_currentCam.CameraWorldPosition;
    
    // 收集candidates
    for (auto& [objectID, entry] : m_giRegistry)
    {
        MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
        if (!obj)
            continue;
        
        for (uint32_t cardID : entry.m_cardIDs)
        {
            SurfaceCard* card = GetSurfaceCardByID(cardID);
            if (!card || !card->m_resident || card->m_pendingUpdate)
                continue;
            
            CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
            if (!instance)
                continue;
            
            float priority = CalculateCardPriority(card, instance, cameraPos);
            uint32_t tileCount = card->m_atlasTileSpan.x * card->m_atlasTileSpan.y;
            
            CardPriority cp;
            cp.cardID = cardID;
            cp.card = card;
            cp.priority = priority;
            cp.tileCount = tileCount;
            cp.priorityPerTile = priority / static_cast<float>(tileCount);  // 性价比
            
            candidates.push_back(cp);
        }
    }
    
    if (candidates.empty())
        return;
    
    // ✅ 策略：优先驱逐"性价比"最低的cards
    // 即：优先级低且占用空间大的cards
    std::sort(candidates.begin(), candidates.end(),
              [](const CardPriority& a, const CardPriority& b) {
                  return a.priorityPerTile < b.priorityPerTile;
              });
    
    // 驱逐直到满足目标
    uint32_t freedTiles = 0;
    size_t evictedCount = 0;
    
    for (const auto& candidate : candidates)
    {
        if (freedTiles >= targetTilesToFree)
            break;
        
        SurfaceCard* card = candidate.card;
        
        // 释放空间
        if (m_config.m_giSystem && card->m_atlasCoord.x >= 0)
        {
            m_config.m_giSystem->FreeCardSpace(card->m_atlasCoord, card->m_atlasTileSpan);
            freedTiles += candidate.tileCount;
            evictedCount++;
            
            DebuggerPrintf("[Scene]   Evicted card %u (prio=%.3f, tiles=%u, ratio=%.3f)\n",
                          card->m_globalCardID, candidate.priority, 
                          candidate.tileCount, candidate.priorityPerTile);
        }
        
        // 标记
        card->m_resident = false;
        card->m_atlasCoord = IntVec2(-1, -1);
        card->m_atlasTileSpan = IntVec2(0, 0);
        card->m_pendingUpdate = true;
        
        // 添加到脏列表
        m_dirtyCardIDs.push_back(card->m_globalCardID);
    }
    
    DebuggerPrintf("[Scene] Advanced eviction: evicted %zu cards, freed %u/%u tiles\n",
                   evictedCount, freedTiles, targetTilesToFree);
    
#endif
#ifdef ENGINE_DX11_RENDERER
    UNUSED(targetTilesToFree)
#endif
}

void Scene::PrepareStaticGI()
{
    for (MeshObject* mesh : m_meshObjects)
    {
        if (ShouldObjectHaveGI(mesh))
        {
            RegisterMeshObjectForGI(mesh);
        }
    }
    
    if (m_config.m_giSystem && m_config.m_renderer)
    {
        m_config.m_giSystem->BuildCardBVH();
    }
}

void Scene::RegisterMeshObjectForGI(MeshObject* object)
{
    if (!object || !object->GetMesh())
    {
        DebuggerPrintf("[Scene] RegisterMeshObjectForGI: Invalid object or mesh\n");
        return;
    }

    StaticMesh* mesh = object->GetMesh();
    uint32_t objectID = object->GetID();
    //float objectScale = object->GetScale(); TODO

    std::vector<uint32_t> surfaceCardIDs;
    for (size_t i = 0; i < mesh->m_cardTemplates.size(); i++)
    {
        SurfaceCard* card = GetOrCreateSurfaceCard(objectID, (uint32_t)i);
        if (card)
        {
            if (!card->m_resident && m_config.m_giSystem)
            {
                CardAllocation alloc = m_config.m_giSystem->AllocateCardSpace(card->m_pixelResolution);
                if (alloc.IsValid())
                {
                    card->m_atlasCoord = alloc.m_baseCoord;
                    card->m_atlasTileSpan = alloc.m_tileCount;
                    card->m_atlasPixelCoord = alloc.m_pixelCoord;
                    card->m_pixelResolution = alloc.m_pixelResolution;
                    card->m_meshObjectID = objectID;
                    //card->m_templateIndex = templateIndex;
                    
                    card->m_resident = true;
                
                    DebuggerPrintf("[Scene] Card %u allocated: Tile(%d,%d) Pixel(%d,%d) Res(%dx%d)\n",
            card->m_globalCardID,
            card->m_atlasCoord.x, card->m_atlasCoord.y,
            card->m_atlasPixelCoord.x, card->m_atlasPixelCoord.y,
            card->m_pixelResolution.x, card->m_pixelResolution.y);
                }
                else
                {
                    DebuggerPrintf("[Scene] WARNING: Failed to allocate atlas space for card %u\n",
                                   card->m_globalCardID);
                }
            }
        
            surfaceCardIDs.push_back(card->m_globalCardID);
            m_dirtyCardIDs.push_back(card->m_globalCardID);
        }
    }
    
    GIObjectEntry entry;
    entry.m_objectID = objectID;
    entry.m_worldTransform = object->GetWorldMatrix();
    entry.m_worldBounds = object->GetWorldBounds();
    entry.m_lastUpdateFrame = m_currentFrame;
    entry.m_isDirty = false;
    entry.m_cardIDs = surfaceCardIDs; 

    if (auto it = m_giRegistry.find(objectID); it != m_giRegistry.end())
    {
        CleanupSurfaceCardsForObject(objectID);
    }

    m_giRegistry[objectID] = entry;

    if (m_config.m_giSystem)
    {
        for (uint32_t cardID : surfaceCardIDs)
        {
            SurfaceCard* card = GetSurfaceCardByID(cardID);
            if (card)
            {
                m_dirtyCardIDs.push_back(card->m_globalCardID);
            }
        }
    }

    DebuggerPrintf("[Scene] GI registration complete for object %u (%zu cards)\n",
                   objectID, surfaceCardIDs.size());
#ifdef ENGINE_DX11_RENDERER
    //UNUSED(objectScale);
#endif
}

void Scene::UnregisterMeshObjectFromGI(uint32_t objectID)
{
    auto it = m_giRegistry.find(objectID);
    if (it == m_giRegistry.end())
        return;

    // ✅ 清理所有SurfaceCards
    CleanupSurfaceCardsForObject(objectID);

    // 从registry移除
    m_giRegistry.erase(it);

    DebuggerPrintf("[Scene] Unregistered object %u from GI\n", objectID);
}

void Scene::RegisterCardsToLightSystem(uint32_t objectID)
{
    auto it = m_giRegistry.find(objectID);
    if (it == m_giRegistry.end())
        return;
    
    MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
    if (!obj)
        return;
    
    DebuggerPrintf("[Scene] Registering cards for object %u to light system\n", objectID);
    
    for (auto& instance : obj->m_cardInstances)
    {
        memset(instance.m_lightMask, 0, sizeof(instance.m_lightMask));
    }
    
    for (size_t i = 0; i < it->second.m_cardIDs.size(); i++)
    {
        uint32_t cardID = it->second.m_cardIDs[i];
        
        SurfaceCard* card = GetSurfaceCardByID(cardID);
        if (!card || card->m_templateIndex >= obj->m_cardInstances.size())
            continue;
        
        CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
        if (!instance)
            continue;
        
        AABB3 cardBounds = ComputeCardWorldBounds(instance, card);
        
        for (auto* light : m_lightObjects)
        {
            AABB3 lightBounds = light->GetWorldBounds();
            
            if (!DoAABBsOverlap3D(cardBounds, lightBounds))
                continue;
            
            if (light->GetLightType() == LIGHT_SPOT)
            {
                Vec3 toCard = instance->m_worldOrigin - light->m_position;
                float dot = DotProduct3D(toCard.GetNormalized(), light->m_spotForward);
                if (dot < light->m_outerDotThresholds)
                    continue;
            }
            
            // 使用 generalLightID（在 LightsArray 中的索引）而不是 scene object ID
            int generalLightID = light->m_generalLightID;
            if (generalLightID < 0)  // 方向光没有 generalLightID，跳过
                continue;

            uint32_t wordIndex = generalLightID / 32;
            uint32_t bitIndex = generalLightID & 31;
            instance->m_lightMask[wordIndex] |= (1u << bitIndex);

            uint32_t lightID = light->GetID();
            m_cardToLightObjects[cardID].push_back(lightID);
            light->m_affectedCards.push_back(cardID);

            DebuggerPrintf("[Scene]   Light %u (arrayIndex %d) affects card %u\n", lightID, generalLightID, cardID);
        }
        
        instance->m_isDirty = true;
        card->m_pendingUpdate = true;
    }
}

void Scene::MarkCardsDirty(std::vector<uint32_t>& cardIDs)
{
    for (uint32_t cardID : cardIDs)
    {
        // std::vector<MeshObject*> instances = GetInstancesUsingCard(cardID);
        //
        // for (MeshObject* obj : instances)
        // {
        //     for (size_t i = 0; i < obj->GetMesh()->m_cardTemplates.size(); i++)
        //     {
        //         if (obj->GetMesh()->m_cardTemplates[i].m_globalCardID == cardID)
        //         {
        //             if (i < obj->m_cardInstances.size())
        //             {
        //                 obj->m_cardInstances[i].m_isDirty = true;
        //                 
        //                 DebuggerPrintf("[Scene] Marked card %u of object %u as dirty\n",
        //                                cardID, obj->GetID());
        //             }
        //             break;
        //         }
        //     }
        // }

        //TODO!!!!
        // 添加到待捕获列表（每个cardID只添加一次）
        if (std::find(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end(), cardID) == m_dirtyCardIDs.end())
        {
            m_dirtyCardIDs.push_back(cardID);
        }
    }
}

void Scene::MarkCardDirty(uint32_t cardID)
{
    std::vector<uint32_t> cardIDs = { cardID };
    MarkCardsDirty(cardIDs);
}

AABB3 Scene::ComputeCardWorldBounds(const CardInstanceData* instance, const SurfaceCard* card)
{
    if (!instance || !card)
        return AABB3();
    
    Vec3 halfX = instance->m_worldAxisX * (instance->m_worldSize.x * 0.5f);
    Vec3 halfY = instance->m_worldAxisY * (instance->m_worldSize.y * 0.5f);
    Vec3 halfN = instance->m_worldNormal * 0.1f;  

    Vec3 corners[8] = {
        instance->m_worldOrigin + halfX + halfY + halfN,
        instance->m_worldOrigin + halfX + halfY - halfN,
        instance->m_worldOrigin + halfX - halfY + halfN,
        instance->m_worldOrigin + halfX - halfY - halfN,
        instance->m_worldOrigin - halfX + halfY + halfN,
        instance->m_worldOrigin - halfX + halfY - halfN,
        instance->m_worldOrigin - halfX - halfY + halfN,
        instance->m_worldOrigin - halfX - halfY - halfN
    };

    Vec3 minP = corners[0], maxP = corners[0];
    for (int i = 1; i < 8; i++) {
        minP.x = MinF(minP.x, corners[i].x);
        minP.y = MinF(minP.y, corners[i].y);
        minP.z = MinF(minP.z, corners[i].z);
        maxP.x = MaxF(maxP.x, corners[i].x);
        maxP.y = MaxF(maxP.y, corners[i].y);
        maxP.z = MaxF(maxP.z, corners[i].z);
    }
    return AABB3(minP, maxP);
}

void Scene::ClearDirtyCards()
{
    m_dirtyCardIDs.clear();
}

uint32_t Scene::AllocateCardID()
{
    return m_nextCardID ++;
}

void Scene::MarkInstanceDirty(uint32_t objectID, uint32_t templateIndex)
{
    MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
    if (!obj)
        return;

    CardInstanceData* instance = obj->GetCardInstance(templateIndex);
    if (!instance)
        return;

    instance->m_isDirty = true;

    // 如果已经有SurfaceCard，也标记它
    if (instance->m_surfaceCardId != UINT32_MAX)
    {
        SurfaceCard* card = GetSurfaceCardByID(instance->m_surfaceCardId);
        if (card)
        {
            card->m_pendingUpdate = true;
            
            auto it = std::find(m_dirtyCardIDs.begin(), m_dirtyCardIDs.end(), 
                               instance->m_surfaceCardId);
            if (it == m_dirtyCardIDs.end())
            {
                m_dirtyCardIDs.push_back(instance->m_surfaceCardId);
            }
        }
    }
}

// void Scene::CullEntities(const Frustum& frustum)
// {
//     // 网格视锥剔除
//     for (MeshEntity* mesh : m_meshObjects)
//     {
//         if (!mesh->IsActive())
//             continue;
//             
//         AABB3 bounds = mesh->GetWorldBounds();
//         if (frustum.Intersects(bounds))
//         {
//             m_visibleMeshes.push_back(mesh);
//         }
//     }
//     
//     // 光源剔除（只剔除有限范围的光源）
//     for (LightEntity* light : m_lightObjects)
//     {
//         if (!light->IsActive())
//             continue;
//             
//         if (light->GetLightType() == LightEntity::LIGHT_DIRECTIONAL)
//         {
//             // 方向光总是活动的
//             m_activeLights.push_back(light);
//         }
//         else
//         {
//             AABB3 bounds = light->GetWorldBounds();
//             if (frustum.Intersects(bounds))
//             {
//                 m_activeLights.push_back(light);
//             }
//         }
//     }
// }

std::vector<MeshObject*> Scene::GetInstancesUsingCard(uint32_t cardID)
{
    std::vector<MeshObject*> instances;
    
    SurfaceCard* card = GetSurfaceCardByID(cardID);
    if (!card)
        return instances;
    
    MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(card->m_meshObjectID));
    if (obj)
    {
        instances.push_back(obj);
    }
    
    return instances;
}

SurfaceCard* Scene::GetOrCreateSurfaceCard(uint32_t objectID, uint32_t templateIndex)
{
    MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
    if (!obj || !obj->GetMesh())
        return nullptr;
    
    StaticMesh* mesh = obj->GetMesh();
    if (templateIndex >= mesh->m_cardTemplates.size())
        return nullptr;
    
    CardInstanceData* instance = obj->GetCardInstance(templateIndex);
    if (!instance)
        return nullptr;
    
    // 如果已经有SurfaceCard，直接返回
    if (instance->m_surfaceCardId != UINT32_MAX)
    {
        auto it = m_cardIDToCardPtr.find(instance->m_surfaceCardId);
        if (it != m_cardIDToCardPtr.end())
            return it->second;
    }
    
    // 创建新的SurfaceCard
    SurfaceCard* card = new SurfaceCard();
    card->m_globalCardID = AllocateCardID();
    card->m_meshObjectID = objectID;
    card->m_templateIndex = templateIndex;
    
    // 从模板获取推荐分辨率 ->TODO：生成不同分辨率的card
    const SurfaceCardTemplate& templ = mesh->m_cardTemplates[templateIndex];
    card->m_pixelResolution = templ.m_recommendedResolution;
    
    // 初始状态：未分配atlas空间
    card->m_resident = false;
    card->m_atlasCoord = IntVec2(-1, -1);
    card->m_atlasTileSpan = IntVec2(0, 0);
    card->m_pendingUpdate = true;
    card->m_lastTouchedFrame = 0;
    card->m_priority = 1.0f;
    
    // 关联instance和card
    instance->m_surfaceCardId = card->m_globalCardID;
    
    // 添加到全局映射表
    m_cardIDToCardPtr[card->m_globalCardID] = card;
    
    DebuggerPrintf("[Scene] Created SurfaceCard %u for object %u, template %u\n",
                   card->m_globalCardID, objectID, templateIndex);
    
    return card;
}

SurfaceCard* Scene::GetSurfaceCardByID(uint32_t globalCardID)
{
    auto it = m_cardIDToCardPtr.find(globalCardID);
    if (it != m_cardIDToCardPtr.end())
        return it->second;
    return nullptr;
}

const SurfaceCard* Scene::GetSurfaceCardByID(uint32_t globalCardID) const
{
    auto it = m_cardIDToCardPtr.find(globalCardID);
    if (it != m_cardIDToCardPtr.end())
        return it->second;
    return nullptr;
}

std::vector<SurfaceCard*> Scene::GetSurfaceCardsForObject(uint32_t objectID)
{
    std::vector<SurfaceCard*> cards;
 
    MeshObject* obj = static_cast<MeshObject*>(GetSceneObject(objectID));
    if (!obj)
        return cards;
 
    for (size_t i = 0; i < obj->GetCardInstanceCount(); i++)
    {
        const CardInstanceData* instance = obj->GetCardInstance(i);
        if (instance && instance->m_surfaceCardId != UINT32_MAX)
        {
            SurfaceCard* card = GetSurfaceCardByID(instance->m_surfaceCardId);
            if (card)
                cards.push_back(card);
        }
    }
 
    return cards;
}

void Scene::CleanupSurfaceCardsForObject(uint32_t objectID)
{
    std::vector<SurfaceCard*> cards = GetSurfaceCardsForObject(objectID);

    for (SurfaceCard* card : cards)
    {
        if (card->m_resident && m_config.m_giSystem)
        {
            m_config.m_giSystem->FreeCardSpace(card->m_atlasCoord, card->m_atlasTileSpan);
        }
    
        m_cardIDToCardPtr.erase(card->m_globalCardID);
        delete card;
    }

    DebuggerPrintf("[Scene] Cleaned up %zu SurfaceCards for object %u\n",
                   cards.size(), objectID);
}