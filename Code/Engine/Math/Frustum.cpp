#include "Frustum.h"
#include "AABB3.hpp"
#include "MathUtils.hpp"
#include "Engine/Renderer/Camera.hpp"

Frustum::Frustum(Plane3 const& left, Plane3 const& right, Plane3 const& bottom, Plane3 const& top, Plane3 const& nearP, Plane3 const& farP, Camera* const& camera)
{
	m_planes[Left] = left;
	m_planes[Right] = right;
	m_planes[Bottom] = bottom;
	m_planes[Top] = top;
	m_planes[Near] = nearP;
	m_planes[Far] = farP;
	m_camera = camera;
	NormalizePlanes();
}

Frustum Frustum::FromCorners(Vec3 const corners[8], Camera* const& camera)
{
	Frustum frustum;

	// 假设corners的顺序是:
	// 0-3: near plane (左下, 右下, 右上, 左上)
	// 4-7: far plane (左下, 右下, 右上, 左上)

	// Left plane: 从 corners[0], corners[3], corners[4] 构建
	frustum.m_planes[Left] = Plane3::FromThreePoints(corners[0], corners[3], corners[4]);

	// Right plane: 从 corners[1], corners[5], corners[2] 构建
	frustum.m_planes[Right] = Plane3::FromThreePoints(corners[1], corners[5], corners[2]);

	// Bottom plane: 从 corners[0], corners[4], corners[1] 构建
	frustum.m_planes[Bottom] = Plane3::FromThreePoints(corners[0], corners[4], corners[1]);

	// Top plane: 从 corners[2], corners[6], corners[3] 构建
	frustum.m_planes[Top] = Plane3::FromThreePoints(corners[2], corners[6], corners[3]);

	// Near plane: 从 corners[0], corners[1], corners[2] 构建
	frustum.m_planes[Near] = Plane3::FromThreePoints(corners[0], corners[1], corners[2]);

	// Far plane: 从 corners[4], corners[6], corners[5] 构建
	frustum.m_planes[Far] = Plane3::FromThreePoints(corners[4], corners[6], corners[5]);

	frustum.m_camera = camera;
	return frustum;
}

Frustum Frustum::FromViewProjectionMatrix(const Mat44& viewProjectionMatrix, Camera* const& camera)
{
	Frustum frustum;

	// 对于列主序矩阵，提取frustum平面
	// 矩阵布局 (列主序):
	// [0  4  8  12]   [m00 m01 m02 m03]
	// [1  5  9  13] = [m10 m11 m12 m13]
	// [2  6  10 14]   [m20 m21 m22 m23]
	// [3  7  11 15]   [m30 m31 m32 m33]

	const float* m = viewProjectionMatrix.m_values;

	// 从列主序矩阵提取平面，需要访问行
	// Row 0 = [m[0], m[4], m[8],  m[12]]
	// Row 1 = [m[1], m[5], m[9],  m[13]]
	// Row 2 = [m[2], m[6], m[10], m[14]]
	// Row 3 = [m[3], m[7], m[11], m[15]]

	// Left plane: row3 + row0
	frustum.m_planes[Left].m_normal.x = m[3] + m[0];
	frustum.m_planes[Left].m_normal.y = m[7] + m[4];
	frustum.m_planes[Left].m_normal.z = m[11] + m[8];
	frustum.m_planes[Left].m_distToPlaneAloneNormalFromOrigin = m[15] + m[12];

	// Right plane: row3 - row0
	frustum.m_planes[Right].m_normal.x = m[3] - m[0];
	frustum.m_planes[Right].m_normal.y = m[7] - m[4];
	frustum.m_planes[Right].m_normal.z = m[11] - m[8];
	frustum.m_planes[Right].m_distToPlaneAloneNormalFromOrigin = m[15] - m[12];

	// Bottom plane: row3 + row1
	frustum.m_planes[Bottom].m_normal.x = m[3] + m[1];
	frustum.m_planes[Bottom].m_normal.y = m[7] + m[5];
	frustum.m_planes[Bottom].m_normal.z = m[11] + m[9];
	frustum.m_planes[Bottom].m_distToPlaneAloneNormalFromOrigin = m[15] + m[13];

	// Top plane: row3 - row1
	frustum.m_planes[Top].m_normal.x = m[3] - m[1];
	frustum.m_planes[Top].m_normal.y = m[7] - m[5];
	frustum.m_planes[Top].m_normal.z = m[11] - m[9];
	frustum.m_planes[Top].m_distToPlaneAloneNormalFromOrigin = m[15] - m[13];

	// Near plane: row2 (DX convention, z ∈ [0,1])
	frustum.m_planes[Near].m_normal.x = m[2];
	frustum.m_planes[Near].m_normal.y = m[6];
	frustum.m_planes[Near].m_normal.z = m[10];
	frustum.m_planes[Near].m_distToPlaneAloneNormalFromOrigin = m[14];

	// Far plane: row3 - row2
	frustum.m_planes[Far].m_normal.x = m[3] - m[2];
	frustum.m_planes[Far].m_normal.y = m[7] - m[6];
	frustum.m_planes[Far].m_normal.z = m[11] - m[10];
	frustum.m_planes[Far].m_distToPlaneAloneNormalFromOrigin = m[15] - m[14];

	frustum.NormalizePlanes();
	frustum.m_camera = camera;
	return frustum;
}

bool Frustum::IsPointInside(const Vec3& point) const
{
	// 使用内联的点积计算避免函数调用开销
	for (int i = 0; i < 6; ++i)
	{
		const Plane3& plane = m_planes[i];
		// 手动内联点积计算
		float distance = plane.m_normal.x * point.x + 
		                 plane.m_normal.y * point.y + 
		                 plane.m_normal.z * point.z + 
		                 plane.m_distToPlaneAloneNormalFromOrigin;
		if (distance < 0.0f)
		{
			return false;
		}
	}
	return true;
}

bool Frustum::IsAABBOutside(const AABB3& aabb) const
{
	// 高效的AABB-Frustum剔除测试
	// 对每个平面，测试AABB的"positive vertex"（p-vertex）
	// p-vertex是AABB沿着平面法线方向最远的顶点
	
	for (int i = 0; i < 6; ++i)
	{
		const Plane3& plane = m_planes[i];
		const Vec3& n = plane.m_normal;
		
		// 根据法线方向选择p-vertex
		// 如果法线分量为正，选择max；否则选择min
		float px = (n.x >= 0.0f) ? aabb.m_maxs.x : aabb.m_mins.x;
		float py = (n.y >= 0.0f) ? aabb.m_maxs.y : aabb.m_mins.y;
		float pz = (n.z >= 0.0f) ? aabb.m_maxs.z : aabb.m_mins.z;
		
		// 计算p-vertex到平面的距离（内联点积）
		float distance = n.x * px + n.y * py + n.z * pz + plane.m_distToPlaneAloneNormalFromOrigin;
		
		// 如果p-vertex在平面外侧，整个AABB都在外侧
		if (distance < 0.0f)
		{
			return true;
		}
	}
	
	// 通过了所有平面测试，AABB在frustum内部或与之相交
	return false;
}

ContainmentType Frustum::DetectContainmentWithAABB(const AABB3& aabb) const
{
	bool intersecting = false;
	
	for (int i = 0; i < 6; ++i)
	{
		const Plane3& plane = m_planes[i];
		const Vec3& n = plane.m_normal;
		
		// p-vertex: 沿法线方向最远的点
		float px = (n.x >= 0.0f) ? aabb.m_maxs.x : aabb.m_mins.x;
		float py = (n.y >= 0.0f) ? aabb.m_maxs.y : aabb.m_mins.y;
		float pz = (n.z >= 0.0f) ? aabb.m_maxs.z : aabb.m_mins.z;
		
		// n-vertex: 沿法线反方向最远的点
		float nx = (n.x >= 0.0f) ? aabb.m_mins.x : aabb.m_maxs.x;
		float ny = (n.y >= 0.0f) ? aabb.m_mins.y : aabb.m_maxs.y;
		float nz = (n.z >= 0.0f) ? aabb.m_mins.z : aabb.m_maxs.z;

		// 内联点积计算
		float pDistance = n.x * px + n.y * py + n.z * pz + plane.m_distToPlaneAloneNormalFromOrigin;
		float nDistance = n.x * nx + n.y * ny + n.z * nz + plane.m_distToPlaneAloneNormalFromOrigin;

		// 如果p-vertex在外侧，整个AABB在外侧
		if (pDistance < 0.0f)
		{
			return ContainmentType::OUTSIDE;
		}

		// 如果n-vertex在外侧而p-vertex在内侧，AABB与平面相交
		if (nDistance < 0.0f)
		{
			intersecting = true;
		}
	}

	return intersecting ? ContainmentType::INTERSECTS : ContainmentType::INSIDE;
}

ContainmentType Frustum::DetectContainmentWithSphere(const Vec3& center, float radius) const
{
	bool intersecting = false;

	for (int i = 0; i < 6; ++i)
	{
		const Plane3& plane = m_planes[i];
		const Vec3& n = plane.m_normal;
		
		// 内联点积计算
		float distance = n.x * center.x + n.y * center.y + n.z * center.z + 
		                 plane.m_distToPlaneAloneNormalFromOrigin;

		// 球心到平面的距离小于-radius，球完全在外侧
		if (distance < -radius)
		{
			return ContainmentType::OUTSIDE;
		}

		// 球心到平面的距离在(-radius, radius)之间，球与平面相交
		if (distance < radius)
		{
			intersecting = true;
		}
	}

	return intersecting ? ContainmentType::INTERSECTS : ContainmentType::INSIDE;
}

void Frustum::NormalizePlanes()
{
	for (int i = 0; i < 6; ++i)
	{
		Plane3& plane = m_planes[i];
		Vec3& n = plane.m_normal;
		
		// 内联长度计算
		float lengthSq = n.x * n.x + n.y * n.y + n.z * n.z;
		
		if (lengthSq > 0.0f)
		{
			float invLength = 1.0f / sqrtf(lengthSq);
			n.x *= invLength;
			n.y *= invLength;
			n.z *= invLength;
			plane.m_distToPlaneAloneNormalFromOrigin *= invLength;
		}
	}
}