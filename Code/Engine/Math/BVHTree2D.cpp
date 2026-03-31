#include "Engine/Math/BVHTree2D.hpp"
#include "Engine/Math/ConvexShape2D.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>

void BVHTree2D::Build(Vec2 const& worldMins, Vec2 const& worldMaxs,
	std::vector<ConvexShape2D> const& shapes, int maxDepth, int maxLeafSize)
{
	m_nodes.clear();
	if (shapes.empty())
		return;

	// Create root node with all shape indices
	m_nodes.emplace_back();
	BVHNode2D& root = m_nodes[0];
	root.m_bounds = AABB2(worldMins, worldMaxs);
	root.m_shapeIndices.resize(shapes.size());
	for (int i = 0; i < (int)shapes.size(); ++i)
		root.m_shapeIndices[i] = i;

	BuildRecursive(0, shapes, 0, maxDepth, maxLeafSize);
}

void BVHTree2D::BuildRecursive(int nodeIndex, std::vector<ConvexShape2D> const& shapes, int depth, int maxDepth, int maxLeafSize)
{
	BVHNode2D& node = m_nodes[nodeIndex];
	if ((int)node.m_shapeIndices.size() <= maxLeafSize || depth >= maxDepth)
	{
		node.m_isLeaf = true;
		// Tighten bounds to actual AABB of contained shapes
		if (!node.m_shapeIndices.empty())
		{
			float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
			for (int idx : node.m_shapeIndices)
			{
				Vec2 c = shapes[idx].m_boundingDiscCenter;
				float r = shapes[idx].m_boundingDiscRadius;
				if (c.x - r < minX) minX = c.x - r;
				if (c.y - r < minY) minY = c.y - r;
				if (c.x + r > maxX) maxX = c.x + r;
				if (c.y + r > maxY) maxY = c.y + r;
			}
			node.m_bounds = AABB2(Vec2(minX, minY), Vec2(maxX, maxY));
		}
		return;
	}

	// Compute tight AABB of shape bounding-disc centers for splitting
	float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
	for (int idx : node.m_shapeIndices)
	{
		Vec2 c = shapes[idx].m_boundingDiscCenter;
		if (c.x < minX) minX = c.x;
		if (c.y < minY) minY = c.y;
		if (c.x > maxX) maxX = c.x;
		if (c.y > maxY) maxY = c.y;
	}

	// Split along longest axis of center spread
	int splitAxis = ((maxX - minX) >= (maxY - minY)) ? 0 : 1;

	// Sort by center along split axis and find median
	std::vector<int> sorted = node.m_shapeIndices;
	if (splitAxis == 0)
	{
		std::sort(sorted.begin(), sorted.end(), [&](int a, int b)
		{
			return shapes[a].m_boundingDiscCenter.x < shapes[b].m_boundingDiscCenter.x;
		});
	}
	else
	{
		std::sort(sorted.begin(), sorted.end(), [&](int a, int b)
		{
			return shapes[a].m_boundingDiscCenter.y < shapes[b].m_boundingDiscCenter.y;
		});
	}

	int mid = (int)sorted.size() / 2;
	if (mid == 0) mid = 1; // ensure at least 1 in left

	// Compute child AABBs from actual shape bounding discs
	auto computeBounds = [&](std::vector<int> const& indices) -> AABB2
	{
		float bMinX = FLT_MAX, bMinY = FLT_MAX, bMaxX = -FLT_MAX, bMaxY = -FLT_MAX;
		for (int idx : indices)
		{
			Vec2 c = shapes[idx].m_boundingDiscCenter;
			float r = shapes[idx].m_boundingDiscRadius;
			if (c.x - r < bMinX) bMinX = c.x - r;
			if (c.y - r < bMinY) bMinY = c.y - r;
			if (c.x + r > bMaxX) bMaxX = c.x + r;
			if (c.y + r > bMaxY) bMaxY = c.y + r;
		}
		return AABB2(Vec2(bMinX, bMinY), Vec2(bMaxX, bMaxY));
	};

	std::vector<int> leftIndices(sorted.begin(), sorted.begin() + mid);
	std::vector<int> rightIndices(sorted.begin() + mid, sorted.end());

	// Create child nodes
	int leftIdx = (int)m_nodes.size();
	m_nodes.emplace_back();
	int rightIdx = (int)m_nodes.size();
	m_nodes.emplace_back();

	// Re-fetch node reference (vector may have reallocated)
	m_nodes[nodeIndex].m_isLeaf = false;
	m_nodes[nodeIndex].m_leftChild = leftIdx;
	m_nodes[nodeIndex].m_rightChild = rightIdx;

	// Compute parent tight bounds
	AABB2 leftBounds = computeBounds(leftIndices);
	AABB2 rightBounds = computeBounds(rightIndices);

	m_nodes[nodeIndex].m_bounds = AABB2(
		Vec2(fminf(leftBounds.m_mins.x, rightBounds.m_mins.x), fminf(leftBounds.m_mins.y, rightBounds.m_mins.y)),
		Vec2(fmaxf(leftBounds.m_maxs.x, rightBounds.m_maxs.x), fmaxf(leftBounds.m_maxs.y, rightBounds.m_maxs.y))
	);

	m_nodes[leftIdx].m_bounds = leftBounds;
	m_nodes[leftIdx].m_shapeIndices = std::move(leftIndices);

	m_nodes[rightIdx].m_bounds = rightBounds;
	m_nodes[rightIdx].m_shapeIndices = std::move(rightIndices);

	// Clear parent shape indices (no longer a leaf)
	m_nodes[nodeIndex].m_shapeIndices.clear();

	BuildRecursive(leftIdx, shapes, depth + 1, maxDepth, maxLeafSize);
	BuildRecursive(rightIdx, shapes, depth + 1, maxDepth, maxLeafSize);
}

bool BVHTree2D::RayVsAABB2D(Vec2 const& start, Vec2 const& invDir, AABB2 const& box, float maxDist) const
{
	float tx1 = (box.m_mins.x - start.x) * invDir.x;
	float tx2 = (box.m_maxs.x - start.x) * invDir.x;
	float tmin = fminf(tx1, tx2);
	float tmax = fmaxf(tx1, tx2);

	float ty1 = (box.m_mins.y - start.y) * invDir.y;
	float ty2 = (box.m_maxs.y - start.y) * invDir.y;
	tmin = fmaxf(tmin, fminf(ty1, ty2));
	tmax = fminf(tmax, fmaxf(ty1, ty2));

	return tmax >= fmaxf(tmin, 0.f) && tmin <= maxDist;
}

void BVHTree2D::RaycastCandidates(Vec2 const& start, Vec2 const& fwdNormal, float maxDist,
	std::vector<int>& outCandidates) const
{
	outCandidates.clear();
	if (m_nodes.empty())
		return;

	// Compute inverse direction (handle near-zero with large value)
	Vec2 invDir;
	invDir.x = (fabsf(fwdNormal.x) > 1e-8f) ? (1.f / fwdNormal.x) : ((fwdNormal.x >= 0.f) ? 1e8f : -1e8f);
	invDir.y = (fabsf(fwdNormal.y) > 1e-8f) ? (1.f / fwdNormal.y) : ((fwdNormal.y >= 0.f) ? 1e8f : -1e8f);

	// Track visited shape indices to avoid duplicates (not needed for BVH since shapes are in exactly one leaf, but kept for safety)
	std::vector<bool> visited;
	// Find max shape index for visited array size
	// Just use a reasonable upper bound - we'll check bounds inline
	RaycastNode(0, start, invDir, fwdNormal, maxDist, visited, outCandidates);
}

void BVHTree2D::RaycastNode(int nodeIndex, Vec2 const& start, Vec2 const& invDir, Vec2 const& fwdNormal,
	float maxDist, std::vector<bool>& visited, std::vector<int>& outCandidates) const
{
	BVHNode2D const& node = m_nodes[nodeIndex];

	if (!RayVsAABB2D(start, invDir, node.m_bounds, maxDist))
		return;

	if (node.m_isLeaf)
	{
		for (int idx : node.m_shapeIndices)
		{
			outCandidates.push_back(idx);
		}
		return;
	}

	RaycastNode(node.m_leftChild, start, invDir, fwdNormal, maxDist, visited, outCandidates);
	RaycastNode(node.m_rightChild, start, invDir, fwdNormal, maxDist, visited, outCandidates);
}
