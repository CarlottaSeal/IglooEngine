#pragma once
#pragma warning(error: 4389)
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "SceneCommon.h"
#include "Object/SceneObject.h"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Object/Light/LightObject.h"
#include "Object/Mesh/MeshManager.h"

struct CardInstanceData;
class MeshObject;
struct Vec3;
struct SDFInstance;
struct Mat44;
class SDFGenerator;
class GISystem;

struct SceneConfig
{
    Renderer* m_renderer;
    GISystem* m_giSystem;
};

class Scene
{
public:
    Scene(SceneConfig const config);
    ~Scene();

    void InitializeRoughly();
    void InitializeBoundsAndMeshSDF();
    void Update(float deltaTime);
    void CheckMemoryPressure();

    virtual void Render() const;
    
    MeshObject* CreateMeshEntity(const std::string& path, const std::string& name = "", Vec3 position = {}, EulerAngles orientation = {});
    LightObject* CreateLightEntity(const std::string& name, LightObjectType type, Vec3 position, Rgba8 sunColor = Rgba8::WHITE, Vec3 sunDirection = Vec3(),
                         Rgba8 lightColor = Rgba8::WHITE, float ambience = 0.f, Vec3 spotForward= Vec3(), float innerRadius = 0.f, float outerRadius = 0.f,
                         float innerDotThreshold = 0.f, float outerDotThreshold = 0.f);
    //CreateEntity(SceneObjectType type, const std::string& name);

    void UpdateCardMetadata();
    void DestroyObject(uint32_t entityID);
    SceneObject* GetSceneObject(uint32_t entityID);
    const SceneObject* GetSceneObject(uint32_t entityID) const;
    uint32_t FindClosestObject(const Vec3& pos);

    //GI initialization
    void PrepareStaticGI();
    void RegisterMeshObjectForGI(MeshObject* object);
    void RegisterCardsToLightSystem(uint32_t objectID);
    void UnregisterMeshObjectFromGI(uint32_t objectID);
    
    void OnMeshObjectTransformChanged(uint32_t objectID);
    void ProcessGIUpdates();
    
    uint32_t AllocateCardID();
    void MarkInstanceDirty(uint32_t objectID, uint32_t templateIndex);
    void MarkCardsDirty(std::vector<uint32_t>& cardIDs);
    void MarkCardDirty(uint32_t cardID);
    AABB3 ComputeCardWorldBounds(const CardInstanceData* instance, const SurfaceCard* card);
    void ClearDirtyCards();

    std::vector<uint32_t> RegisterLightInfluence(uint32_t lightID, const AABB3& bounds);
    void RemoveLightFromCard(uint32_t lightID, uint32_t tileIndex);

    std::vector<MeshObject*> GetInstancesUsingCard(uint32_t cardID);
    SurfaceCard* GetOrCreateSurfaceCard(uint32_t objectID, uint32_t templateIndex);
    SurfaceCard* GetSurfaceCardByID(uint32_t globalCardID);
    const SurfaceCard* GetSurfaceCardByID(uint32_t globalCardID) const;
    std::vector<SurfaceCard*> GetSurfaceCardsForObject(uint32_t objectID);
    void CleanupSurfaceCardsForObject(uint32_t objectID);
    const std::vector<uint32_t>& GetLightsForCard(uint32_t cardID) const;

    std::vector<MeshObject*> GetStaticObjects() const;
    std::vector<MeshObject*> GetVisibleMeshes(const Camera& camera);
    const GIObjectEntry* GetGIEntry(uint32_t objectID) const;

    void ProduceLightVariables();
    void ReassignLightIDs();
    void SetLightConstants() const;
    void PrepareRenderData(const Camera& camera);
    std::vector<MeshObject*> GetVisibleObjects() const;
    const std::vector<RenderItem>& GetOpaqueRenderItems() const { return m_opaqueRenderItems; }
    const std::vector<RenderItem>& GetTransparentRenderItems() const { return m_transparentRenderItems; }

    
#ifdef ENGINE_DX12_RENDERER
    DX12Renderer* GetRenderer() { return m_config.m_renderer->GetSubRenderer(); }
#endif
    GISystem* GetGISystem() { return m_config.m_giSystem; }
    
private:
    void AddObjectToLists(SceneObject* object);
    void RemoveObjectFromLists(SceneObject* object);
    //void CullEntities(const Frustum& frustum);

    bool ShouldObjectHaveGI(const MeshObject* object) const;
    uint32_t HashWorldPosition(const Vec3& pos) const;

    void EvictLowPriorityCards();
    float CalculateCardPriority(SurfaceCard* card, CardInstanceData* instance, const Vec3& cameraPos);
    void EvictCards(const std::vector<SurfaceCard*>& cards);
    void EvictLowPriorityCards_Advanced(uint32_t targetTilesToFree);
    void EvictLowPriorityCards_Tiered();

    void UpdateSceneBounds();
    void CalculateSceneBounds();
    void BuildMeshSDFInfos();
    
public:
    SceneConfig m_config;
    MeshManager* m_meshManager;

    std::unordered_map<uint32_t, std::unique_ptr<SceneObject>> m_objects;
    uint32_t m_nextEntityID = 1;
    
    std::vector<MeshObject*> m_meshObjects;
    std::vector<LightObject*> m_lightObjects;
    std::vector<SceneObject*> m_allObjects;
    AABB3 m_sceneBounds;
    bool m_needsRebuildGlobalLighting;
    std::vector<MeshSDFInfoGPU> m_meshInfos;
    
    uint32_t m_nextCardID = 0;
    std::unordered_map<uint32_t, GIObjectEntry> m_giRegistry;
    std::vector<uint32_t> m_dirtyCardIDs;  
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_cardToLightObjects;
    std::unordered_map<uint32_t, SurfaceCard*> m_cardIDToCardPtr; 
    //std::unordered_map<uint32_t, SurfaceCardTemplate*> m_cardIDToTemplatePtr; 
    
    uint32_t m_currentFrame = 0;
    static constexpr uint32_t MAX_TILES = 4096;
    
    std::vector<RenderItem> m_opaqueRenderItems;
    std::vector<RenderItem> m_transparentRenderItems;

    //Sun light应该不是一个物体。<-还是统一管理吧
    Vec3 m_sunDirection = Vec3(3.f, 1.f, -2.f);
    Rgba8 m_sunColor = Rgba8(90,90,90,255);
    int m_numLights = 0;
    std::vector<Rgba8> m_lightColors;
    std::vector<Vec3> m_worldLightPositions;
    std::vector<Vec3> m_spotForwards;
    std::vector<float> m_ambiences;
    std::vector<float> m_innerRadii;
    std::vector<float> m_outerRadii;
    std::vector<float> m_innerDotThresholds;
    std::vector<float> m_outerDotThresholds;

    
    // no use
    std::vector<MeshObject*> m_visibleMeshes;
    std::vector<LightObject*> m_activeLights;
    bool m_transformsDirty = false;
    bool m_renderDataDirty = false;
};






