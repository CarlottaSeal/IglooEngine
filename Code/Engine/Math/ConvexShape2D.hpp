#pragma once
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Core/Rgba8.hpp"
#include <vector>

class RandomNumberGenerator;

struct ConvexShape2D
{
	std::vector<Vec2> m_points;           // CCW hull vertices (world space)
	Vec2 m_center;                        // centroid
	float m_orientationDegrees = 0.f;
	float m_scale = 1.f;
	Rgba8 m_color;

	// Bounding disc
	Vec2 m_boundingDiscCenter;
	float m_boundingDiscRadius = 0.f;

	// Computed on build
	std::vector<Vec2> m_normals;          // outward edge normals

	void Build();
	void Randomize(Vec2 const& worldMins, Vec2 const& worldMaxs, RandomNumberGenerator& rng);
	bool IsPointInside(Vec2 const& point) const;
	RaycastResult2D Raycast(Vec2 const& start, Vec2 const& fwdNormal, float maxDist) const;
	RaycastResult2D RaycastExit(Vec2 const& start, Vec2 const& fwdNormal, float maxDist) const;

	void RotateAbout(Vec2 const& pivot, float deltaDegrees);
	void ScaleAbout(Vec2 const& pivot, float scaleFactor);
	void Translate(Vec2 const& delta);
};
