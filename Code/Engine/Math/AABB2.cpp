#include "AABB2.hpp"
#include "Engine/Math/MathUtils.hpp"

const AABB2 AABB2::ZERO_TO_ONE = AABB2(Vec2(0.f, 0.f), Vec2(1.f, 1.f));

AABB2::AABB2()
{
}

AABB2::~AABB2()
{
}

AABB2::AABB2(AABB2 const& copyFrom)
{
	m_mins = copyFrom.m_mins;
	m_maxs = copyFrom.m_maxs;
}

AABB2::AABB2(float minX, float minY, float maxX, float maxY)
{
	m_mins = Vec2(minX, minY);
	m_maxs = Vec2(maxX, maxY);
}

AABB2::AABB2(Vec2 const& mins, Vec2 const& maxs)
{
	m_mins = mins;
	m_maxs = maxs;
}

bool AABB2::IsPointInside(Vec2 const& point) const
{
	return (point.x >= m_mins.x && point.x <= m_maxs.x) &&
		   (point.y >= m_mins.y && point.y <= m_maxs.y);
}

Vec2 const AABB2::GetCenter() const
{
	return Vec2((m_mins.x + m_maxs.x) * 0.5f, (m_mins.y + m_maxs.y) * 0.5f);
}

Vec2 const AABB2::GetDimensions() const
{
	return Vec2(m_maxs.x - m_mins.x, m_maxs.y - m_mins.y);
}

float const AABB2::GetWidth() const
{
	return (m_maxs.x-m_mins.x);
}

float const AABB2::GetHeight() const
{
	return (m_maxs.y - m_mins.y);
}

Vec2 const AABB2::GetBottomCenter() const
{
	return (m_mins + m_maxs)/2.f - Vec2(0.f, GetHeight() / 2.f);
}

Vec2 const AABB2::GetBottomRight() const
{
	return Vec2(m_maxs.x, m_mins.y);
}

Vec2 const AABB2::GetTopLeft() const
{
	return Vec2(m_mins.x, m_maxs.y);
}

Vec2 const AABB2::GetNearestPoint(Vec2 const& uv) const
{
	float nearestX = GetClamped(uv.x, m_mins.x, m_maxs.x);
	float nearestY = GetClamped(uv.y, m_mins.y, m_maxs.y);
	return Vec2(nearestX, nearestY);
}

Vec2 const AABB2::GetPointAtUV(Vec2 const& uv) const
{
	return Vec2(
		Interpolate(m_mins.x, m_maxs.x, uv.x),
		Interpolate(m_mins.y, m_maxs.y, uv.y)
	);
}

Vec2 const AABB2::GetUVForPoint(Vec2 const& point) const
{
	return Vec2(
		GetFractionWithinRange(point.x, m_mins.x, m_maxs.x),
		GetFractionWithinRange(point.y, m_mins.y, m_maxs.y)
	);
}

void AABB2::Translate(Vec2 const& translationToApply)
{
	m_mins += translationToApply;
	m_maxs += translationToApply;
}

void AABB2::SetCenter(Vec2 const& newCenter)
{
	Vec2 dimensions = GetDimensions();
	m_mins = newCenter - dimensions * 0.5f;
	m_maxs = newCenter + dimensions * 0.5f;
}

void AABB2::SetDimensions(Vec2 const& newDimensions)
{
	Vec2 center = GetCenter();
	m_mins = center - newDimensions * 0.5f;
	m_maxs = center + newDimensions * 0.5f;
}

void AABB2::StretchToIncludePoint(Vec2 const& point)
{
	if (point.x < m_mins.x) m_mins.x = point.x;
	if (point.x > m_maxs.x) m_maxs.x = point.x;
	if (point.y < m_mins.y) m_mins.y = point.y;
	if (point.y > m_maxs.y) m_maxs.y = point.y;
}

void AABB2::ClampWithin(AABB2 const& clampedAABB)
{
	Vec2 size = GetDimensions();
	m_mins.x = GetClamped(m_mins.x, clampedAABB.m_mins.x, clampedAABB.m_maxs.x - size.x);
	m_mins.y = GetClamped(m_mins.y, clampedAABB.m_mins.y, clampedAABB.m_maxs.y - size.y);
	m_maxs = m_mins + size;
}

void AABB2::ReduceToAspect(float newAspect)
{
	Vec2 center = GetCenter();
	Vec2 size = GetDimensions();
	float currentAspect = size.x / size.y;

	if (currentAspect > newAspect)
	{
		float newWidth = size.y * newAspect;
		float halfWidth = newWidth * 0.5f;
		m_mins.x = center.x - halfWidth;
		m_maxs.x = center.x + halfWidth;
	}
	else
	{
		float newHeight = size.x / newAspect;
		float halfHeight = newHeight * 0.5f;
		m_mins.y = center.y - halfHeight;
		m_maxs.y = center.y + halfHeight;
	}
}

void AABB2::EnlargeToAspect(float newAspect)
{
	Vec2 center = GetCenter();
	Vec2 size = GetDimensions();
	float currentAspect = size.x / size.y;

	if (currentAspect > newAspect)
	{
		float newHeight = size.x / newAspect;
		float halfHeight = newHeight * 0.5f;
		m_mins.y = center.y - halfHeight;
		m_maxs.y = center.y + halfHeight;
	}
	else
	{
		float newWidth = size.y * newAspect;
		float halfWidth = newWidth * 0.5f;
		m_mins.x = center.x - halfWidth;
		m_maxs.x = center.x + halfWidth;
	}
}

AABB2 AABB2::GetNormalizedAABB2(AABB2 const& standardBounds)
{
	Vec2 baseSize = standardBounds.GetDimensions();
	Vec2 normalizedMin = Vec2(
		(m_mins.x - standardBounds.m_mins.x) / baseSize.x,
		(m_mins.y - standardBounds.m_mins.y) / baseSize.y
	);
	Vec2 normalizedMax = Vec2(
		(m_maxs.x - standardBounds.m_mins.x) / baseSize.x,
		(m_maxs.y - standardBounds.m_mins.y) / baseSize.y
	);
	return AABB2(normalizedMin, normalizedMax);
}

AABB2 AABB2::ChopOffTop(float percentOfOriginalToChop, float extraHeightOfOriginalToChop)
{
	Vec2 size = GetDimensions();
	float chopHeight = size.y * percentOfOriginalToChop + extraHeightOfOriginalToChop;
	Vec2 newMaxs = m_maxs;
	Vec2 newMins = Vec2(m_mins.x, m_maxs.y - chopHeight);
	m_maxs.y -= chopHeight;
	return AABB2(newMins, newMaxs);
}

AABB2 AABB2::ChopOffBottom(float percentOfOriginalToChop, float extraHeightOfOriginalToChop)
{
	Vec2 size = GetDimensions();
	float chopHeight = size.y * percentOfOriginalToChop + extraHeightOfOriginalToChop;
	Vec2 newMins = m_mins;
	Vec2 newMaxs = Vec2(m_maxs.x, m_mins.y + chopHeight);
	m_mins.y += chopHeight;
	return AABB2(newMins, newMaxs);
}

AABB2 AABB2::ChopOffLeft(float percentOfOriginalToChop, float extraHeightOfOriginalToChop)
{
	Vec2 size = GetDimensions();
	float chopWidth = size.x * percentOfOriginalToChop + extraHeightOfOriginalToChop;
	Vec2 newMins = m_mins;
	Vec2 newMaxs = Vec2(m_mins.x + chopWidth, m_maxs.y);
	m_mins.x += chopWidth;
	return AABB2(newMins, newMaxs);
}

AABB2 AABB2::ChopOffRight(float percentOfOriginalToChop, float extraWidthToChop)
{
	Vec2 size = GetDimensions();
	float chopWidth = size.x * percentOfOriginalToChop + extraWidthToChop;
    
	// 计算切掉的右侧部分
	Vec2 choppedMins = Vec2(m_maxs.x - chopWidth, m_mins.y);
	Vec2 choppedMaxs = m_maxs;
    
	// 返回切掉的部分，不修改原矩形
	return AABB2(choppedMins, choppedMaxs);
}

AABB2 AABB2::GetBoxWithUV(Vec2 uvMin, Vec2 uvMax) const
{
	Vec2 dimensions = m_maxs - m_mins;
	
	Vec2 newMins = m_mins + (uvMin * dimensions);
	Vec2 newMaxs = m_mins + (uvMax * dimensions);

	return AABB2(newMins, newMaxs);
}

AABB2 AABB2::GetBoxWithinIndex(int index, int count, bool horizontal) const
{
	Vec2 dims = GetDimensions();
	Vec2 mins = m_mins;

	if (horizontal)
	{
		float boxWidth = dims.x / count;
		float xMin = mins.x + index * boxWidth;
		float xMax = xMin + boxWidth;
		return AABB2(Vec2(xMin, m_mins.y), Vec2(xMax, m_maxs.y));
	}
	else
	{
		float boxHeight = dims.y / count;
		float yMin = mins.y + index * boxHeight;
		float yMax = yMin + boxHeight;
		return AABB2(Vec2(m_mins.x, yMin), Vec2(m_maxs.x, yMax));
	}
}

AABB2 AABB2::AddPadding(float paddingX, float paddingY) const
{
	return AABB2(
		Vec2(m_mins.x + paddingX, m_mins.y + paddingY),
		Vec2(m_maxs.x - paddingX, m_maxs.y - paddingY)
	);
}
