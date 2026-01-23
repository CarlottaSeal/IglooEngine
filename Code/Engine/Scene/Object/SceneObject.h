#pragma once
#include <cstdint>
#include <string>

#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/EulerAngles.hpp"
#include "Engine/Math/Mat44.hpp"
#include "Engine/Math/Sphere.h"
#include "Engine/Math/Vec3.hpp"

class Scene;

enum SceneObjectType
{
    OBJECT_MESH,
    OBJECT_LIGHT,
    OBJECT_CAMERA,
    OBJECT_GI_PROBE,  // GI 探针
    OBJECT_VOLUME     // 体积（雾、后处理等）
};

class SceneObject
{
public:
    SceneObject(SceneObjectType type, uint32_t id, const std::string& name, Vec3 position, EulerAngles rotation = EulerAngles())
        : m_type(type), m_id(id), m_name(name), m_orientation(rotation), m_position(position) {}
    virtual ~SceneObject() = default;

    virtual void OnCreate(Scene* scene);
    virtual void OnDestroy() {}
    virtual void Update(float deltaTime) { UNUSED(deltaTime) }

    SceneObjectType GetType() const { return m_type; }
    uint32_t GetID() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    bool HasMoved() const { return m_worldMatrixDirty; }
    void ClearMoveFlag();
    void SetPosition(const Vec3& pos);
    void SetRotation(const EulerAngles& rot);
    void SetScale(const float& scale);
    void SetTransform(const Vec3& position, const EulerAngles& orientation, float scale);
    
    const Vec3& GetPosition() const { return m_position; }
    const float GetScale() const { return m_scale; }
    virtual const Mat44& GetWorldMatrix();
    const Mat44 GetWorldMatrixWithoutScaling();
    
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible && m_active; }
    
    void SetActive(bool active) { m_active = active; }
    bool IsActive() const { return m_active; }
    
    virtual AABB3 GetLocalBounds() const { return AABB3(); }
    virtual Sphere GetLocalBoundsSphere() const { return Sphere(); }
    virtual AABB3 GetWorldBounds() const;

public:
	mutable Mat44 m_cachedWorldMatrix;
    Mat44 m_cachedWorldMatrixWithoutMeshTransform;
    Rgba8 m_color = Rgba8::WHITE;
protected:
    virtual void OnTransformChanged() { m_worldMatrixDirty = true;}
    virtual void UpdateWorldMatrix();
    
protected:
    SceneObjectType m_type;
    uint32_t m_id;
    std::string m_name;
    Scene* m_scene = nullptr;

    EulerAngles m_orientation;
    Vec3 m_position;
    float m_scale = 1;
    Mat44 m_worldMatrix;
    Mat44 m_previousWorldMatrix;
	mutable bool m_worldMatrixDirty = true;
    
    bool m_visible = true;
    bool m_active = true;
    
    uint32_t m_renderLayers = 0xFFFFFFFF;  // 渲染层级掩码
};
