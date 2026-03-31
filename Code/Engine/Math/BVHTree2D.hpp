#pragma once
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/AABB2.hpp"
#include <vector>

struct ConvexShape2D;

struct BVHNode2D
{
	bool m_isLeaf = true;
	AABB2 m_bounds;

	int m_leftChild = -1;
	int m_rightChild = -1;

	std::vector<int> m_shapeIndices;
};

class BVHTree2D
{
public:
	void Build(Vec2 const& worldMins, Vec2 const& worldMaxs,
		std::vector<ConvexShape2D> const& shapes, int maxDepth = 8, int maxLeafSize = 4);

	void RaycastCandidates(Vec2 const& start, Vec2 const& fwdNormal, float maxDist,
		std::vector<int>& outCandidates) const;

	// Debug accessors
	int GetNodeCount() const { return (int)m_nodes.size(); }
	BVHNode2D const& GetNode(int i) const { return m_nodes[i]; }

private:
	void BuildRecursive(int nodeIndex, std::vector<ConvexShape2D> const& shapes, int depth, int maxDepth, int maxLeafSize);
	void RaycastNode(int nodeIndex, Vec2 const& start, Vec2 const& invDir, Vec2 const& fwdNormal,
		float maxDist, std::vector<bool>& visited, std::vector<int>& outCandidates) const;
	bool RayVsAABB2D(Vec2 const& start, Vec2 const& invDir, AABB2 const& box, float maxDist) const;

	std::vector<BVHNode2D> m_nodes;
};
