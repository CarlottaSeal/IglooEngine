#include "Engine/Math/KDTree2D.hpp"
#include "Engine/Math/ConvexShape2D.hpp"
#include "Engine/Math/MathUtils.hpp"
#include <algorithm>
#include <cmath>

void KDTree2D::Build(Vec2 const& worldMins, Vec2 const& worldMaxs,
	std::vector<ConvexShape2D> const& shapes, int maxDepth, int maxLeafSize)
{
	m_nodes.clear();

	// Create root node
	KDNode2D root;
	root.m_bounds = AABB2(worldMins, worldMaxs);
	root.m_isLeaf = true;
	for (int i = 0; i < (int)shapes.size(); ++i)
		root.m_shapeIndices.push_back(i);
	m_nodes.push_back(root);

	BuildRecursive(0, shapes, 0, maxDepth, maxLeafSize);
}

void KDTree2D::BuildRecursive(int nodeIndex, std::vector<ConvexShape2D> const& shapes,
	int depth, int maxDepth, int maxLeafSize)
{
	KDNode2D& node = m_nodes[nodeIndex];

	if ((int)node.m_shapeIndices.size() <= maxLeafSize || depth >= maxDepth)
		return; // stay as leaf

	// Choose split axis: alternate, or use longest axis
	Vec2 dims = node.m_bounds.m_maxs - node.m_bounds.m_mins;
	int axis = (dims.x >= dims.y) ? 0 : 1;

	// Find median bounding disc center along chosen axis
	std::vector<float> centers;
	centers.reserve(node.m_shapeIndices.size());
	for (int idx : node.m_shapeIndices)
	{
		float c = (axis == 0) ? shapes[idx].m_boundingDiscCenter.x : shapes[idx].m_boundingDiscCenter.y;
		centers.push_back(c);
	}
	std::sort(centers.begin(), centers.end());
	float splitPos = centers[centers.size() / 2];

	// Partition shapes into left/right (shapes can go to both if their bounding disc straddles the split)
	std::vector<int> leftIndices, rightIndices;
	for (int idx : node.m_shapeIndices)
	{
		float c = (axis == 0) ? shapes[idx].m_boundingDiscCenter.x : shapes[idx].m_boundingDiscCenter.y;
		float r = shapes[idx].m_boundingDiscRadius;

		if (c - r <= splitPos)
			leftIndices.push_back(idx);
		if (c + r >= splitPos)
			rightIndices.push_back(idx);
	}

	// Don't split if it doesn't reduce — all shapes ended up in both children
	if ((int)leftIndices.size() == (int)node.m_shapeIndices.size() &&
		(int)rightIndices.size() == (int)node.m_shapeIndices.size())
		return;

	// Convert to internal node
	node.m_isLeaf = false;
	node.m_splitAxis = axis;
	node.m_splitPos = splitPos;

	// Create left child
	KDNode2D leftNode;
	leftNode.m_isLeaf = true;
	leftNode.m_shapeIndices = leftIndices;
	leftNode.m_bounds = node.m_bounds;
	if (axis == 0)
		leftNode.m_bounds.m_maxs.x = splitPos;
	else
		leftNode.m_bounds.m_maxs.y = splitPos;

	// Create right child
	KDNode2D rightNode;
	rightNode.m_isLeaf = true;
	rightNode.m_shapeIndices = rightIndices;
	rightNode.m_bounds = node.m_bounds;
	if (axis == 0)
		rightNode.m_bounds.m_mins.x = splitPos;
	else
		rightNode.m_bounds.m_mins.y = splitPos;

	int leftIdx = (int)m_nodes.size();
	m_nodes.push_back(leftNode);
	int rightIdx = (int)m_nodes.size();
	m_nodes.push_back(rightNode);

	// Must re-fetch node reference after push_back (may have reallocated)
	m_nodes[nodeIndex].m_leftChild = leftIdx;
	m_nodes[nodeIndex].m_rightChild = rightIdx;
	m_nodes[nodeIndex].m_shapeIndices.clear();

	BuildRecursive(leftIdx, shapes, depth + 1, maxDepth, maxLeafSize);
	BuildRecursive(rightIdx, shapes, depth + 1, maxDepth, maxLeafSize);
}

void KDTree2D::RaycastCandidates(Vec2 const& start, Vec2 const& fwdNormal, float maxDist,
	std::vector<int>& outCandidates) const
{
	outCandidates.clear();
	if (m_nodes.empty()) return;

	// Find max shape index for visited array
	int maxIdx = 0;
	for (auto const& node : m_nodes)
		for (int idx : node.m_shapeIndices)
			if (idx > maxIdx) maxIdx = idx;

	std::vector<bool> visited(maxIdx + 1, false);
	RaycastNode(0, start, fwdNormal, 0.f, maxDist, visited, outCandidates);
}

void KDTree2D::RaycastNode(int nodeIndex, Vec2 const& start, Vec2 const& fwdNormal,
	float tMin, float tMax, std::vector<bool>& visited, std::vector<int>& outCandidates) const
{
	if (nodeIndex < 0 || nodeIndex >= (int)m_nodes.size()) return;
	KDNode2D const& node = m_nodes[nodeIndex];

	// Clip ray against this node's AABB
	float entryT = tMin;
	float exitT = tMax;

	// X slab
	if (fabsf(fwdNormal.x) > 1e-8f)
	{
		float invDx = 1.f / fwdNormal.x;
		float t1 = (node.m_bounds.m_mins.x - start.x) * invDx;
		float t2 = (node.m_bounds.m_maxs.x - start.x) * invDx;
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		entryT = (t1 > entryT) ? t1 : entryT;
		exitT = (t2 < exitT) ? t2 : exitT;
		if (entryT > exitT) return;
	}
	else
	{
		if (start.x < node.m_bounds.m_mins.x || start.x > node.m_bounds.m_maxs.x)
			return;
	}

	// Y slab
	if (fabsf(fwdNormal.y) > 1e-8f)
	{
		float invDy = 1.f / fwdNormal.y;
		float t1 = (node.m_bounds.m_mins.y - start.y) * invDy;
		float t2 = (node.m_bounds.m_maxs.y - start.y) * invDy;
		if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
		entryT = (t1 > entryT) ? t1 : entryT;
		exitT = (t2 < exitT) ? t2 : exitT;
		if (entryT > exitT) return;
	}
	else
	{
		if (start.y < node.m_bounds.m_mins.y || start.y > node.m_bounds.m_maxs.y)
			return;
	}

	if (node.m_isLeaf)
	{
		for (int idx : node.m_shapeIndices)
		{
			if (!visited[idx])
			{
				visited[idx] = true;
				outCandidates.push_back(idx);
			}
		}
		return;
	}

	// Determine traversal order: visit near side first
	float splitCoord = (node.m_splitAxis == 0) ? start.x : start.y;
	float splitDir = (node.m_splitAxis == 0) ? fwdNormal.x : fwdNormal.y;

	int nearChild, farChild;
	if (splitCoord < node.m_splitPos || (splitCoord == node.m_splitPos && splitDir <= 0.f))
	{
		nearChild = node.m_leftChild;
		farChild = node.m_rightChild;
	}
	else
	{
		nearChild = node.m_rightChild;
		farChild = node.m_leftChild;
	}

	// Compute t at which ray crosses the split plane
	float tSplit;
	if (fabsf(splitDir) > 1e-8f)
		tSplit = (node.m_splitPos - ((node.m_splitAxis == 0) ? start.x : start.y)) / splitDir;
	else
		tSplit = -1.f; // parallel — only visit near side

	if (tSplit < entryT || tSplit < 0.f)
	{
		// Split plane is behind or before entry — only visit far side
		RaycastNode(farChild, start, fwdNormal, entryT, exitT, visited, outCandidates);
	}
	else if (tSplit > exitT)
	{
		// Split plane is beyond exit — only visit near side
		RaycastNode(nearChild, start, fwdNormal, entryT, exitT, visited, outCandidates);
	}
	else
	{
		// Ray crosses split — visit both, near first
		RaycastNode(nearChild, start, fwdNormal, entryT, tSplit, visited, outCandidates);
		RaycastNode(farChild, start, fwdNormal, tSplit, exitT, visited, outCandidates);
	}
}
