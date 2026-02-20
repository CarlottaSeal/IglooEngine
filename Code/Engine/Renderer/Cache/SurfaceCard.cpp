#include "SurfaceCard.h"

#include "Engine/Math/MathUtils.hpp"

Vec3 SurfaceCardTemplate::GetLocalNormal() const //改为朝向模型内部
{
	switch (m_direction)
	{
	case 0: return Vec3(-1, 0, 0);   // +X (东面)
	case 1: return Vec3(1, 0, 0);  // -X (西面)
	case 2: return Vec3(0, -1, 0);   // +Y (北面)
	case 3: return Vec3(0, 1, 0);  // -Y (南面)
	case 4: return Vec3(0, 0, -1);   // +Z (顶面/上面) Z-up
	case 5: return Vec3(0, 0, 1);  // -Z (底面/下面)
	default: return Vec3(0, 0, -1);  // 默认朝上（Z-up）
	}
}

Vec3 SurfaceCardTemplate::GetLocalAxisX() const
{
	switch (m_direction)
	{
	case 0: return Vec3(0, 1, 0);   // +X面: 北方向
	case 1: return Vec3(0, -1, 0);  // -X面: 南方向
	case 2: return Vec3(1, 0, 0);   // +Y面: 东方向
	case 3: return Vec3(-1, 0, 0);  // -Y面: 西方向 
	case 4: return Vec3(1, 0, 0);   // +Z面: 东方向
	case 5: return Vec3(1, 0, 0);   // -Z面: 东方向
	default: return Vec3(1, 0, 0);
	}
}

Vec3 SurfaceCardTemplate::GetLocalAxisY() const
{
	Vec3 localNormal = GetLocalNormal();
	Vec3 localAxisX = GetLocalAxisX();
	return CrossProduct3D(localAxisX, localNormal).GetNormalized();
}

Vec3 SurfaceCardTemplate::GetLocalOrigin(const AABB3& localBounds) const 
{
	Vec3 localNormal = GetLocalNormal();
	Vec3 center = (localBounds.m_maxs + localBounds.m_mins) * 0.5f;
	Vec3 size = localBounds.m_maxs - localBounds.m_mins;

	// 沿着法线方向偏移到表面
	float offset = 0.5f * fabs(DotProduct3D(size, localNormal));
	
	// 关键修改：添加一个小的额外偏移，让Card在mesh外部
	//offset *= 1.1f; 
    
	return center - localNormal * offset;
}
