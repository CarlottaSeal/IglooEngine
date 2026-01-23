#include "LightObject.h"

#include "Engine/Scene/Scene.h"

LightObject::LightObject(uint32_t id, const std::string& name, LightObjectType lightType, Vec3 position, Rgba8 sunColor, Vec3 sunDirection,
                         Rgba8 lightColor, float ambience, Vec3 spotForward, float innerRadius, float outerRadius,
                         float innerDotThreshold, float outerDotThreshold)
    : SceneObject(OBJECT_LIGHT, id, name, position, EulerAngles())
    , m_lightType(lightType) 
{
    if (lightType == LIGHT_DIRECTIONAL)
    {
        m_sunColor = sunColor;
        m_sunDirection = sunDirection;
        m_outerRadius = -1.f;
        m_innerRadius = -1.f;
        m_spotForward = Vec3();
    }
    if (lightType == LIGHT_POINT)
    {
        m_ambience = ambience;
        m_lightColor = lightColor;
        m_innerRadius = innerRadius;
        m_outerRadius = outerRadius;
        m_innerDotThresholds = -1.f;
        m_outerDotThresholds = -1.f;
        m_spotForward = Vec3();
    }
    if (lightType == LIGHT_SPOT)
    {
        m_ambience = ambience;
        m_lightColor = lightColor;  // 修复：LIGHT_SPOT 也需要设置 lightColor
        m_spotForward = spotForward;
        m_innerRadius = innerRadius;
        m_outerRadius = outerRadius;
        m_innerDotThresholds = innerDotThreshold;
        m_outerDotThresholds = outerDotThreshold;
    }
}

void LightObject::UpdateAffectedCards()
{
    if (!m_scene)
        return;
    for (uint32_t cardIndex : m_affectedCards)
    {
        m_scene->RemoveLightFromCard(m_id, cardIndex);
    }
    m_affectedCards.clear();
    
    AABB3 bounds = GetWorldBounds();
    if (bounds.GetVolume()>=0.001f)
    {
        m_affectedCards = m_scene->RegisterLightInfluence(m_id, bounds);
    }
}

GeneralLight LightObject::GetLightData() const
{
    GeneralLight data;
    data.LightType = m_lightType;
    data.WorldPosition[0] = m_position.x;
    data.WorldPosition[1] = m_position.y;
    data.WorldPosition[2] = m_position.z;
    data.Color[0] = m_lightColor.r;
    data.Color[1] = m_lightColor.g;
    data.Color[2] = m_lightColor.b;
    data.Color[3] = m_lightColor.a;
    data.LightType = m_lightType;
    data.SpotForward[0] = m_spotForward.x;
    data.SpotForward[1] = m_spotForward.y;
    data.SpotForward[2] = m_spotForward.z;
    data.Ambience = m_ambience;
    data.InnerRadius = m_innerRadius;
    data.OuterRadius = m_outerRadius;
    data.InnerDotThreshold = m_innerDotThresholds;
    data.OuterDotThreshold = m_outerDotThresholds;
    return data;
}

AABB3 LightObject::GetWorldBounds() const
{
    if (m_lightType == LIGHT_DIRECTIONAL)
    {
        return AABB3();
    }
    if (m_lightType == LIGHT_POINT || m_lightType == LIGHT_SPOT)
    {
        Vec3 center = m_position;
        Vec3 extent = Vec3(m_outerRadius, m_outerRadius, m_outerRadius);
        return AABB3(center - extent, center + extent);
    }
    return AABB3();
}

void LightObject::OnTransformChanged()
{
    UpdateAffectedCards();
    //m_hasMoved = true;
    //m_shadowMapDirty = true;
}

void LightObject::OnCreate(Scene* scene)
{
    SceneObject::OnCreate(scene);
    if (m_type == LIGHT_DIRECTIONAL)
    {
        m_scene->m_sunColor = m_sunColor;
        m_scene->m_sunDirection = m_sunDirection;
    }
    UpdateAffectedCards();
}
