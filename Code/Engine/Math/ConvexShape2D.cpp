#include "Engine/Math/ConvexShape2D.hpp"
#include "Engine/Math/RandomNumberGenerator.hpp"
#include <algorithm>
#include <cmath>

static std::vector<Vec2> ComputeConvexHull(std::vector<Vec2> pts)
{
	int n = (int)pts.size();
	if (n < 3) return pts;

	// Find lowest-y point (leftmost tiebreak)
	int pivot = 0;
	for (int i = 1; i < n; ++i)
	{
		if (pts[i].y < pts[pivot].y || (pts[i].y == pts[pivot].y && pts[i].x < pts[pivot].x))
			pivot = i;
	}
	std::swap(pts[0], pts[pivot]);
	Vec2 p0 = pts[0];

	// Sort by polar angle relative to p0
	std::sort(pts.begin() + 1, pts.end(), [&](Vec2 const& a, Vec2 const& b)
	{
		float cross = CrossProduct2D(a - p0, b - p0);
		if (cross != 0.f) return cross > 0.f;
		// Collinear: closer first
		return GetDistanceSquared2D(a, p0) < GetDistanceSquared2D(b, p0);
	});

	std::vector<Vec2> hull;
	hull.push_back(pts[0]);
	hull.push_back(pts[1]);

	for (int i = 2; i < n; ++i)
	{
		while (hull.size() > 1)
		{
			Vec2 top = hull.back();
			Vec2 second = hull[hull.size() - 2];
			float cross = CrossProduct2D(top - second, pts[i] - second);
			if (cross > 0.f)
				break;
			hull.pop_back();
		}
		hull.push_back(pts[i]);
	}
	return hull;
}

void ConvexShape2D::Build()
{
	int n = (int)m_points.size();
	if (n < 3) return;

	// Compute centroid
	m_center = Vec2(0.f, 0.f);
	for (Vec2 const& p : m_points)
		m_center += p;
	m_center *= (1.f / (float)n);

	// Compute outward edge normals
	m_normals.resize(n);
	for (int i = 0; i < n; ++i)
	{
		int j = (i + 1) % n;
		Vec2 edge = m_points[j] - m_points[i];
		// Outward normal for CCW winding: rotate edge CW 90 = (y, -x) then normalize
		Vec2 normal = Vec2(edge.y, -edge.x);
		normal.Normalize();
		m_normals[i] = normal;
	}

	// Compute bounding disc (centroid + max distance to any vertex)
	m_boundingDiscCenter = m_center;
	m_boundingDiscRadius = 0.f;
	for (Vec2 const& p : m_points)
	{
		float dist = GetDistance2D(m_center, p);
		if (dist > m_boundingDiscRadius)
			m_boundingDiscRadius = dist;
	}
}

void ConvexShape2D::Randomize(Vec2 const& worldMins, Vec2 const& worldMaxs, RandomNumberGenerator& rng)
{
	float rangeX = worldMaxs.x - worldMins.x;
	float rangeY = worldMaxs.y - worldMins.y;
	float maxRadius = (rangeX < rangeY ? rangeX : rangeY) * 0.12f;
	float minRadius = maxRadius * 0.3f;

	// Random center
	Vec2 center;
	center.x = rng.RollRandomFloatInRange(worldMins.x + maxRadius, worldMaxs.x - maxRadius);
	center.y = rng.RollRandomFloatInRange(worldMins.y + maxRadius, worldMaxs.y - maxRadius);

	// Generate 5-10 random points around center
	int numPts = rng.RollRandomIntInRange(5, 10);
	std::vector<Vec2> pts;
	pts.reserve(numPts);
	for (int i = 0; i < numPts; ++i)
	{
		float angle = rng.RollRandomFloatInRange(0.f, 360.f);
		float r = rng.RollRandomFloatInRange(minRadius, maxRadius);
		Vec2 p;
		p.x = center.x + r * CosDegrees(angle);
		p.y = center.y + r * SinDegrees(angle);
		pts.push_back(p);
	}

	// Compute convex hull
	m_points = ComputeConvexHull(pts);

	// Random color
	m_color = Rgba8(
		(unsigned char)rng.RollRandomIntInRange(80, 255),
		(unsigned char)rng.RollRandomIntInRange(80, 255),
		(unsigned char)rng.RollRandomIntInRange(80, 255),
		255
	);

	m_orientationDegrees = 0.f;
	m_scale = 1.f;

	Build();
}

bool ConvexShape2D::IsPointInside(Vec2 const& point) const
{
	int n = (int)m_points.size();
	if (n < 3) return false;

	// Half-plane test: for each edge, point must be on the interior side (left of CCW edge)
	for (int i = 0; i < n; ++i)
	{
		Vec2 edgeStart = m_points[i];
		// Outward normal dot (point - edgeStart) must be <= 0
		float d = DotProduct2D(m_normals[i], point - edgeStart);
		if (d > 0.f)
			return false;
	}
	return true;
}

RaycastResult2D ConvexShape2D::Raycast(Vec2 const& start, Vec2 const& fwdNormal, float maxDist) const
{
	RaycastResult2D result;
	result.m_didImpact = false;
	result.m_rayStartPos = start;
	result.m_rayFwdNormal = fwdNormal;
	result.m_rayMaxLength = maxDist;

	int n = (int)m_points.size();
	if (n < 3) return result;

	// Slab method for convex polygon defined by half-planes
	// Each edge i defines half-plane: normal_i . (P - vertex_i) <= 0
	// For ray P(t) = start + t*fwdNormal:
	//   normal_i . (start + t*fwdNormal - vertex_i) <= 0
	//   normal_i . (start - vertex_i) + t * (normal_i . fwdNormal) <= 0

	float tEntry = 0.f;
	float tExit = maxDist;
	int entryEdge = -1;

	constexpr float EPSILON = 1e-6f;

	for (int i = 0; i < n; ++i)
	{
		float numer = DotProduct2D(m_normals[i], start - m_points[i]);
		float denom = DotProduct2D(m_normals[i], fwdNormal);

		if (fabsf(denom) < EPSILON)
		{
			if (numer > 0.f)
				return result;
			continue;
		}

		float t = -numer / denom;

		if (denom < 0.f)
		{
			if (t > tEntry)
			{
				tEntry = t;
				entryEdge = i;
			}
		}
		else
		{
			if (t < tExit)
			{
				tExit = t;
			}
		}

		if (tEntry > tExit)
			return result;
	}

	if (entryEdge == -1 || tEntry > tExit || tEntry > maxDist)
		return result;

	result.m_didImpact = true;
	result.m_impactDist = tEntry;
	result.m_impactPos = start + fwdNormal * tEntry;
	result.m_impactNormal = m_normals[entryEdge];
	return result;
}

RaycastResult2D ConvexShape2D::RaycastExit(Vec2 const& start, Vec2 const& fwdNormal, float maxDist) const
{
	RaycastResult2D result;
	result.m_didImpact = false;
	result.m_rayStartPos = start;
	result.m_rayFwdNormal = fwdNormal;
	result.m_rayMaxLength = maxDist;

	int n = (int)m_points.size();
	if (n < 3) return result;

	float tEntry = 0.f;
	float tExit = maxDist;
	int exitEdge = -1;

	constexpr float EPSILON = 1e-6f;

	for (int i = 0; i < n; ++i)
	{
		float numer = DotProduct2D(m_normals[i], start - m_points[i]);
		float denom = DotProduct2D(m_normals[i], fwdNormal);

		if (fabsf(denom) < EPSILON)
		{
			if (numer > 0.f)
				return result;
			continue;
		}

		float t = -numer / denom;

		if (denom < 0.f)
		{
			if (t > tEntry)
				tEntry = t;
		}
		else
		{
			if (t < tExit)
			{
				tExit = t;
				exitEdge = i;
			}
		}

		if (tEntry > tExit)
			return result;
	}

	if (exitEdge == -1 || tEntry > tExit || tExit < 0.f || tExit > maxDist)
		return result;

	result.m_didImpact = true;
	result.m_impactDist = tExit;
	result.m_impactPos = start + fwdNormal * tExit;
	result.m_impactNormal = m_normals[exitEdge];
	return result;
}

void ConvexShape2D::RotateAbout(Vec2 const& pivot, float deltaDegrees)
{
	float c = CosDegrees(deltaDegrees);
	float s = SinDegrees(deltaDegrees);
	for (Vec2& p : m_points)
	{
		Vec2 offset = p - pivot;
		p.x = pivot.x + offset.x * c - offset.y * s;
		p.y = pivot.y + offset.x * s + offset.y * c;
	}
	m_orientationDegrees += deltaDegrees;
	Build();
}

void ConvexShape2D::ScaleAbout(Vec2 const& pivot, float scaleFactor)
{
	for (Vec2& p : m_points)
	{
		p = pivot + (p - pivot) * scaleFactor;
	}
	m_scale *= scaleFactor;
	Build();
}

void ConvexShape2D::Translate(Vec2 const& delta)
{
	for (Vec2& p : m_points)
	{
		p += delta;
	}
	m_center += delta;
	m_boundingDiscCenter += delta;
}
