#pragma once
#include "Engine/Scene/Object/SceneObject.h"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Scene/Object/SceneObject.h"

enum LightObjectType
{
    LIGHT_DIRECTIONAL, //暂时当作太阳光处理
    LIGHT_POINT,
    LIGHT_SPOT,
    LIGHT_AREA, //面光源，用于GI
    LIGHT_COUNT
};

class LightObject : public SceneObject
{
    friend class Scene;
    
public:
    LightObject(uint32_t id, const std::string& name, LightObjectType lightType, Vec3 position, Rgba8 sunColor = Rgba8::WHITE, Vec3 sunDirection = Vec3(),
                         Rgba8 lightColor = Rgba8::WHITE, float ambience = 0.f, Vec3 spotForward= Vec3(), float innerRadius = 0.f, float outerRadius = 0.f,
                         float innerDotThreshold = 0.f, float outerDotThreshold = 0.f);
    
    //void SetCastShadows(bool cast) { m_castShadows = cast; }
    //bool CastsShadows() const { return m_castShadows; }

    void UpdateAffectedCards();

    GeneralLight GetLightData() const;
    
    virtual AABB3 GetWorldBounds() const override;
    
    LightObjectType GetLightType() const { return m_lightType; }
    int GetGeneralLightID() const { return m_generalLightID; }
    
protected:
    virtual void OnTransformChanged() override;
    virtual void OnCreate(Scene* scene) override;
    
protected:
    LightObjectType m_lightType;
    int m_generalLightID = -1;  // -1 表示未分配（方向光或尚未注册）
    Vec3 m_sunDirection;
    Rgba8 m_sunColor;
    Rgba8 m_lightColor;
    //m_position -> SceneObject
    Vec3 m_spotForward;
    float m_ambience;
    float m_innerRadius;
    float m_outerRadius;
    float m_innerDotThresholds = 30.0f;
    float m_outerDotThresholds = 45.0f;

    //std::vector<uint32_t> m_affectedTiles;
    std::vector<uint32_t> m_affectedCards;
    
    //bool m_castShadows = true;
    //bool m_shadowMapDirty = true;
    //uint32_t m_shadowMapIndex = UINT32_MAX;
};
