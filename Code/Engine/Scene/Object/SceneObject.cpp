#include "SceneObject.h"

#include "Engine/Renderer/BitmapFont.hpp"

void SceneObject::OnCreate(Scene* scene)
{
    m_scene = scene;
    //OnTransformChanged();
}

void SceneObject::ClearMoveFlag()
{
    m_previousWorldMatrix = GetWorldMatrix();
}
    
void SceneObject::SetPosition(const Vec3& pos)
{
    if (m_position == pos)
        return;
    m_position = pos; 
    //m_transformDirty = true;
    OnTransformChanged();
}

void SceneObject::SetRotation(const EulerAngles& rot)
{
    if (m_orientation == rot)
        return;
    m_orientation = rot;
    //m_transformDirty = true;
    OnTransformChanged();
}

void SceneObject::SetScale(const float& scale)
{
    if (scale == m_scale)
        return;
    m_scale = scale;
    //m_transformDirty = true;
    OnTransformChanged();
}
void SceneObject::SetTransform(const Vec3& position, const EulerAngles& orientation, float scale)
{
	bool changed = false;
	if (m_position != position) { m_position = position; changed = true; }
	if (m_orientation != orientation) { m_orientation = orientation; changed = true; }
	if (m_scale != scale) { m_scale = scale; changed = true; }

	if (changed)
	{
		OnTransformChanged();
	}
}

const Mat44& SceneObject::GetWorldMatrix()
{
	if (m_worldMatrixDirty)
	{
		UpdateWorldMatrix();
		m_worldMatrixDirty = false;
	}
	return m_cachedWorldMatrix;
    /*if (m_transformDirty)
    {
        Mat44 rotate;
        rotate = m_orientation.GetAsMatrix_IFwd_JLeft_KUp();
        Mat44 translate;
        translate = Mat44::MakeTranslation3D(m_position);

        translate.Append(rotate); 
        m_worldMatrix = translate;
        
        m_transformDirty = false;
    }
    return m_worldMatrix;*/
}

const Mat44 SceneObject::GetWorldMatrixWithoutScaling()
{
    Mat44 mat = Mat44();
    //m_cachedWorldMatrix.AppendScaleUniform3D(m_scale);
    Mat44 rotation = m_orientation.GetAsMatrix_IFwd_JLeft_KUp();
    mat.Append(rotation);
    mat.SetTranslation3D(m_position);
    return mat;
}

AABB3 SceneObject::GetWorldBounds() const
{
    //AABB3 local = GetLocalBounds();
    //Vec3 center = (local.m_maxs + local.m_mins) * 0.5f;
    //Vec3 halfSize = (local.m_maxs - local.m_mins) * 0.5f;
    //return AABB3(center - halfSize * m_scale, center + halfSize * m_scale);
    return AABB3();
}

void SceneObject::UpdateWorldMatrix()
{
	m_cachedWorldMatrix = Mat44();

	m_cachedWorldMatrix.AppendScaleUniform3D(m_scale);

	Mat44 rotation = m_orientation.GetAsMatrix_IFwd_JLeft_KUp();
	m_cachedWorldMatrix.Append(rotation);

	m_cachedWorldMatrix.SetTranslation3D(m_position);

    m_cachedWorldMatrixWithoutMeshTransform = m_cachedWorldMatrix;
}
