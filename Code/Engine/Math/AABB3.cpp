#include "AABB3.hpp"
#include "Engine/Math/Vec3.hpp"
#include <array>

#include "MathUtils.hpp"

AABB3::AABB3(AABB3 const& copyFrom)
	:m_mins(copyFrom.m_mins)
	,m_maxs(copyFrom.m_maxs)
{
}

AABB3::AABB3(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
	:m_mins(Vec3(minX, minY, minZ))
	,m_maxs(Vec3(maxX, maxY, maxZ))
{
}

AABB3::AABB3(Vec3 const& mins, Vec3 const& maxs)
	:m_mins(mins)
	,m_maxs(maxs)
{
}

Vec3 AABB3::GetBoundsSize() const
{
	return Vec3(
			m_maxs.x - m_mins.x,
			m_maxs.y - m_mins.y,
			m_maxs.z - m_mins.z
		);
}

float AABB3::GetVolume() const
{
	return (m_maxs.x - m_mins.x) * (m_maxs.y - m_mins.y) * (m_maxs.z - m_mins.z);
}

Vec3 AABB3::GetCenter() const
{
	return (m_mins + m_maxs)*0.5f;
}

std::vector<Vec3> AABB3::GetCorners() const
{
	std::vector<Vec3> corners;
	corners.push_back(Vec3(m_mins.x, m_mins.y, m_mins.z));
	corners.push_back(Vec3(m_maxs.x, m_mins.y, m_mins.z));
	corners.push_back(Vec3(m_mins.x, m_maxs.y, m_mins.z));
	corners.push_back(Vec3(m_maxs.x, m_maxs.y, m_mins.z));
	corners.push_back(Vec3(m_mins.x, m_mins.y, m_maxs.z));
	corners.push_back(Vec3(m_maxs.x, m_mins.y, m_maxs.z));
	corners.push_back(Vec3(m_mins.x, m_maxs.y, m_maxs.z));
	corners.push_back(Vec3(m_maxs.x, m_maxs.y, m_maxs.z));

	return corners;
}

bool AABB3::IsPointInside(const Vec3& point)
{
	return (point.x >= m_mins.x && point.x <= m_maxs.x &&
		point.y >= m_mins.y && point.y <= m_maxs.y &&
		point.z >= m_mins.z && point.z <= m_maxs.z);
}

void AABB3::StretchToIncludePoint(const Vec3& point)
{
	m_mins.x = MinF(m_mins.x, point.x);
	m_mins.y = MinF(m_mins.y, point.y);
	m_mins.z = MinF(m_mins.z, point.z);
    
	m_maxs.x = MaxF(m_maxs.x, point.x);
	m_maxs.y = MaxF(m_maxs.y, point.y);
	m_maxs.z = MaxF(m_maxs.z, point.z);
}

void AABB3::StretchToIncludeAABB(const AABB3& other)
{
	StretchToIncludePoint(other.m_mins);
	StretchToIncludePoint(other.m_maxs);
}

void AABB3::Translate(const Vec3& translation)
{
	m_mins += translation;
	m_maxs += translation;
}
