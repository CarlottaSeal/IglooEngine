#pragma once
#include "Engine/Math/Vec3.hpp"
#include <vector>

struct AABB3
{
public:
	Vec3 m_mins;
	Vec3 m_maxs;

public:
	AABB3() {}
	~AABB3() {}
	AABB3(AABB3 const& copyFrom); //copy constructor(from another Vec2
	explicit AABB3(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
	explicit AABB3(Vec3 const& mins, Vec3 const& maxs);

	Vec3 GetBoundsSize() const;
	float GetVolume() const;
	Vec3 GetCenter() const;
	std::vector<Vec3> GetCorners() const;

	bool IsPointInside(const Vec3& point);
	void StretchToIncludePoint(const Vec3& point);
	void StretchToIncludeAABB(const AABB3& other);

	void Translate(const Vec3& translation);
};