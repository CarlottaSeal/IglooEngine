#include "MeshObject.h"
#include "Engine/Renderer/Cache/SurfaceCard.h"
#include "Engine/Core/StaticMesh.h"
#include "Engine/Scene/Scene.h"

MeshObject::MeshObject(uint32_t id, const std::string& name, const std::string& path, Vec3 position, EulerAngles rotation)
    : SceneObject(OBJECT_MESH, id, name, position, rotation)
{
    m_path = path;
}

void MeshObject::OnCreate(Scene* scene)
{
    SceneObject::OnCreate(scene);
    m_mesh = m_scene->m_meshManager->GetOrLoadMesh(m_name, m_path);
	m_cachedWorldMatrix = GetWorldMatrix();
	InitializeCardInstances();
	//OnTransformChanged();
}

void MeshObject::OnDestroy()
{
    SceneObject::OnDestroy();
}

Sphere MeshObject::GetLocalBoundsSphere() const
{
	Sphere bs = m_mesh->GetTransformedBoundsSphere();
	bs.m_radius *= m_scale;
	return bs;
}

Sphere MeshObject::GetWorldBoundsSphere() const
{
	Sphere bounds = GetLocalBoundsSphere();
	bounds.m_center += m_position;
	return bounds;
}

AABB3 MeshObject::GetLocalBounds() const
{
    return m_mesh->GetScaledBounds(m_scale);
}

AABB3 MeshObject::GetLocalBoundsUntransformed() const
{
	return m_mesh->GetTransformedAABB3BoundsWithoutAxisTransform();
}

AABB3 MeshObject::GetWorldBounds() const
{
    AABB3 local = GetLocalBounds();
    Vec3 center = (local.m_maxs + local.m_mins) * 0.5f;
	Vec3 halfSize = (local.m_maxs - local.m_mins) * 0.5f;
	center += m_position;
	return AABB3(center - halfSize, center + halfSize);
}

float MeshObject::CalculateCaptureDepth(const CardInstanceData* instance, uint8_t direction)
{
	UNUSED(instance);
	//Vec3 boundsSize = objectBounds.m_maxs - objectBounds.m_mins;
	Vec3 boundsSize = GetWorldBounds().m_maxs - GetWorldBounds().m_mins;
	float objectMaxDim = MaxF(MaxF(boundsSize.x, boundsSize.y), boundsSize.z);
    
	float multiplier = 1.5f;
	if (direction == 4 || direction == 5)  // 垂直表面
		multiplier = 2.0f;
    
	// 3. 计算基础深度
	float depth = objectMaxDim * multiplier;
    
	return GetClamped(depth, 1.0f, 20.0f);
}

RenderItem MeshObject::GetRenderItem() const
{
    RenderItem item;
    item.m_worldMatrix = const_cast<MeshObject*>(this)->GetWorldMatrix();
    item.m_objectID = m_id;
    item.m_bounds = GetWorldBounds();
    item.m_visible = IsVisible();
    return item;
}

void MeshObject::UpdateWorldMatrix()
{
	SceneObject::UpdateWorldMatrix();

	if (m_mesh)
	{
		Mat44 meshTransform = m_mesh->m_transform;
		m_cachedWorldMatrix.Append(meshTransform);
	}
}

void MeshObject::OnTransformChanged()
{
    SceneObject::OnTransformChanged();

    // 更新所有CardInstance的世界姿态
    if (m_mesh && m_mesh->m_hasCardTemplates)
    {
		// 使用原始mesh空间的bounds，这样GetLocalOrigin内部的计算是一致的
		AABB3 rawBounds = m_mesh->GetAABB3Bounds();

		// 完整的世界变换矩阵
		Mat44 worldMatrix = m_cachedWorldMatrix;

		// 提取旋转部分（去掉缩放和平移）用于方向向量
		Mat44 worldRotation = worldMatrix;
		worldRotation.SetTranslation3D(Vec3(0, 0, 0));
		worldRotation.SetIJK3D(
			worldRotation.GetIBasis3D().GetNormalized(),
			worldRotation.GetJBasis3D().GetNormalized(),
			worldRotation.GetKBasis3D().GetNormalized()
		);

		// 总缩放 = mesh缩放 * SceneObject缩放
		float totalScale = m_mesh->m_modelRelativeScale * m_scale;

		for (size_t i = 0; i < m_cardInstances.size(); i++)
		{
			CardInstanceData& instance = m_cardInstances[i];
			const SurfaceCardTemplate& templ = m_mesh->m_cardTemplates[i];

			// 全部在原始mesh空间计算
			Vec3 localOrigin = templ.GetLocalOrigin(rawBounds);  // 原始空间
			Vec3 localNormal = templ.GetLocalNormal();           // 原始空间
			Vec3 localAxisX = templ.GetLocalAxisX();             // 原始空间
			Vec3 localAxisY = templ.GetLocalAxisY();             // 原始空间

			// 用完整worldMatrix变换位置到世界空间
			instance.m_worldOrigin = worldMatrix.TransformPosition3D(localOrigin);

			// 方向向量只需旋转（localNormal朝内，取反得到朝外的世界法线）
			instance.m_worldNormal = -worldRotation.TransformVectorQuantity3D(localNormal).GetNormalized();
			instance.m_worldAxisX = worldRotation.TransformVectorQuantity3D(localAxisX).GetNormalized();
			instance.m_worldAxisY = worldRotation.TransformVectorQuantity3D(localAxisY).GetNormalized();

			// WorldSize: 原始尺寸在原始轴上的投影 * 总缩放
			Vec3 rawSize = rawBounds.m_maxs - rawBounds.m_mins;
			instance.m_worldSize.x = fabsf(DotProduct3D(rawSize, localAxisX)) * totalScale;
			instance.m_worldSize.y = fabsf(DotProduct3D(rawSize, localAxisY)) * totalScale;

			instance.m_isDirty = true;

			// 通知Scene/GISystem标记对应的SurfaceCard为脏
			if (m_scene)
			{
				m_scene->MarkInstanceDirty(m_id, (uint32_t)i);
			}
		}
    }

	if (m_scene && IsStaticForGI())
	{
		m_scene->OnMeshObjectTransformChanged(m_id);
	}
}

void MeshObject::InitializeCardInstances()
{
	if (!m_mesh)
	{
		DebuggerPrintf("[MeshObject] Cannot initialize card instances: no mesh\n");
		return;
	}

	if (!m_mesh->m_hasCardTemplates)
	{
		DebuggerPrintf("[MeshObject] Cannot initialize card instances: mesh has no templates\n");
		return;
	}

	m_cardInstances.clear();
	m_cardInstances.resize(m_mesh->m_cardTemplates.size());

	// 使用原始mesh空间的bounds，这样GetLocalOrigin内部的计算是一致的
	AABB3 rawBounds = m_mesh->GetAABB3Bounds();

	// 完整的世界变换矩阵
	Mat44 worldMatrix = m_cachedWorldMatrix;

	// 提取旋转部分（去掉缩放和平移）用于方向向量
	Mat44 worldRotation = worldMatrix;
	worldRotation.SetTranslation3D(Vec3(0, 0, 0));
	worldRotation.SetIJK3D(
		worldRotation.GetIBasis3D().GetNormalized(),
		worldRotation.GetJBasis3D().GetNormalized(),
		worldRotation.GetKBasis3D().GetNormalized()
	);

	// 总缩放 = mesh缩放 * SceneObject缩放
	float totalScale = m_mesh->m_modelRelativeScale * m_scale;

	for (size_t i = 0; i < m_cardInstances.size(); i++)
	{
		CardInstanceData& instance = m_cardInstances[i];
		const SurfaceCardTemplate& templ = m_mesh->m_cardTemplates[i];

		// 设置身份
		instance.m_meshObjectID = m_id;
		instance.m_templateIndex = (uint32_t)i;

		// 全部在原始mesh空间计算
		Vec3 localOrigin = templ.GetLocalOrigin(rawBounds);  // 原始空间
		Vec3 localNormal = templ.GetLocalNormal();           // 原始空间
		Vec3 localAxisX = templ.GetLocalAxisX();             // 原始空间
		Vec3 localAxisY = templ.GetLocalAxisY();             // 原始空间

		// 用完整worldMatrix变换位置到世界空间
		instance.m_worldOrigin = worldMatrix.TransformPosition3D(localOrigin);

		// 方向向量只需旋转
		// 方向向量只需旋转（localNormal朝内，取反得到朝外的世界法线）
		instance.m_worldNormal = -worldRotation.TransformVectorQuantity3D(localNormal).GetNormalized();
		instance.m_worldAxisX = worldRotation.TransformVectorQuantity3D(localAxisX).GetNormalized();
		instance.m_worldAxisY = worldRotation.TransformVectorQuantity3D(localAxisY).GetNormalized();

		// WorldSize: 原始尺寸在原始轴上的投影 * 总缩放
		Vec3 rawSize = rawBounds.m_maxs - rawBounds.m_mins;
		instance.m_worldSize.x = fabsf(DotProduct3D(rawSize, localAxisX)) * totalScale;
		instance.m_worldSize.y = fabsf(DotProduct3D(rawSize, localAxisY)) * totalScale;

		// 初始化光照掩码和状态
		memset(instance.m_lightMask, 0, sizeof(instance.m_lightMask));
		instance.m_isDirty = true;
		instance.m_lastUpdateFrame = 0;
		instance.m_surfaceCardId = UINT32_MAX; // 稍后由Scene/GISystem分配
	}

	DebuggerPrintf("[MeshObject] Initialized %zu card instances for object %u ('%s')\n",
				   m_cardInstances.size(), m_id, m_name.c_str());
}

const CardInstanceData* MeshObject::GetCardInstance(size_t index) const
{
    if (index >= m_cardInstances.size())
        return nullptr;
    return &m_cardInstances[index];
}

CardInstanceData* MeshObject::GetCardInstance(size_t index)
{
    if (index >= m_cardInstances.size())
        return nullptr;
    return &m_cardInstances[index];
}

size_t MeshObject::GetCardInstanceCount() const
{
    return m_cardInstances.size();
}
