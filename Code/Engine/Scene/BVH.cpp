#include "BVH.h"

#include <algorithm>
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include "Engine/Math/MathUtils.hpp"

void BVH::Build(const std::vector<Vertex_PCUTBN>& vertices, const std::vector<uint32_t>& indices)
{
	DebuggerPrintf("[BVH] Building with %zu vertices, %zu indices\n",
		vertices.size(), indices.size());

	if (indices.size() % 3 != 0)
	{
		DebuggerPrintf("[BVH] ERROR: Indices count not divisible by 3: %zu\n", indices.size());
		return;
	}

	if (indices.empty() || vertices.empty())
	{
		DebuggerPrintf("[BVH] ERROR: Empty data\n");
		return;
	}

    m_vertices = &vertices;
    m_indices = &indices;
    
    std::vector<int> allTriangles;
    allTriangles.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        allTriangles.push_back(static_cast<int>(i));
    }
    
	DebuggerPrintf("[BVH] Starting recursive build with %zu triangles\n", allTriangles.size());

    m_root = BuildRecursive(allTriangles, vertices, indices, 0);

    DebuggerPrintf("[BVH] Build complete\n");
}

void BVH::QueryNearbyTriangles(const Vec3& point, float radius, std::vector<int>& outTriangles) const
{
    if (!m_root)
        return;
    
    outTriangles.clear();
    QueryRecursive(m_root.get(), point, radius, outTriangles);
}

void BVH::QueryIntersectingTriangles(const AABB3& bounds, std::vector<int>& outTriangles) const
{
    if (!m_root)
        return;
    
    outTriangles.clear();
    QueryRecursive(m_root.get(), bounds, outTriangles);
}

void BVH::FlattenForGPU(std::vector<GPUBVHNode>& outNodes, std::vector<uint32_t>& outTriIndices) const
{
	if (!m_root)
	{
		DebuggerPrintf("[BVH] Cannot flatten: root is null\n");
		return;
	}

	outNodes.clear();
	outTriIndices.clear();

	// 预分配空间（估算）
	outNodes.reserve(1000);
	outTriIndices.reserve(m_indices->size() / 3);

	uint32_t nodeIndex = 0;
	FlattenRecursive(m_root.get(), outNodes, outTriIndices, nodeIndex);

	DebuggerPrintf("[BVH] Flattened: %zu nodes, %zu triangle indices\n",
		outNodes.size(), outTriIndices.size());
}

std::unique_ptr<BVHNode> BVH::BuildRecursive(const std::vector<int>& triangleIndices,
    const std::vector<Vertex_PCUTBN>& vertices, const std::vector<uint32_t>& indices, int depth)
{
    auto node = std::make_unique<BVHNode>();
    
    node->m_bounds = ComputeBounds(triangleIndices, vertices, indices);
    
    if (triangleIndices.size() <= MAX_TRIANGLES_PER_LEAF || depth >= MAX_DEPTH)
    {
        node->m_isLeaf = true;
        node->m_triangleIndices = triangleIndices;
        return node;
    }
    
    int axis = ChooseSplitAxis(triangleIndices, vertices, indices);
    
    std::vector<int> sortedTriangles = triangleIndices;
    std::sort(sortedTriangles.begin(), sortedTriangles.end(),
        [&](int a, int b) {
            Vec3 centerA = (vertices[indices[a]].m_position + 
                           vertices[indices[a+1]].m_position + 
                           vertices[indices[a+2]].m_position) / 3.0f;
            Vec3 centerB = (vertices[indices[b]].m_position + 
                           vertices[indices[b+1]].m_position + 
                           vertices[indices[b+2]].m_position) / 3.0f;
            
            if (axis == 0) return centerA.x < centerB.x;
            if (axis == 1) return centerA.y < centerB.y;
            return centerA.z < centerB.z;
        });
    
    // 分割成两组
    size_t mid = sortedTriangles.size() / 2;
    std::vector<int> leftTriangles(sortedTriangles.begin(), 
                                   sortedTriangles.begin() + mid);
    std::vector<int> rightTriangles(sortedTriangles.begin() + mid, 
                                    sortedTriangles.end());
    
    if (!leftTriangles.empty())
    {
        node->m_left = BuildRecursive(leftTriangles, vertices, indices, depth + 1);
    }
    if (!rightTriangles.empty())
    {
        node->m_right = BuildRecursive(rightTriangles, vertices, indices, depth + 1);
    }
    
    return node;
}

AABB3 BVH::ComputeTriangleBounds(int triIndex, const std::vector<Vertex_PCUTBN>& vertices,
    const std::vector<uint32_t>& indices) const
{
    AABB3 bounds;
    bounds.StretchToIncludePoint(vertices[indices[triIndex]].m_position);
    bounds.StretchToIncludePoint(vertices[indices[triIndex + 1]].m_position);
    bounds.StretchToIncludePoint(vertices[indices[triIndex + 2]].m_position);
    return bounds;
}

AABB3 BVH::ComputeBounds(const std::vector<int>& triangleIndices, const std::vector<Vertex_PCUTBN>& vertices,
    const std::vector<uint32_t>& indices) const
{
    AABB3 bounds;
    for (int triIdx : triangleIndices)
    {
        AABB3 triBounds = ComputeTriangleBounds(triIdx, vertices, indices);
        bounds.StretchToIncludePoint(triBounds.m_mins);
        bounds.StretchToIncludePoint(triBounds.m_maxs);
    }
    return bounds;
}

int BVH::ChooseSplitAxis(const std::vector<int>& triangleIndices, const std::vector<Vertex_PCUTBN>& vertices,
    const std::vector<uint32_t>& indices) const
{
    AABB3 bounds = ComputeBounds(triangleIndices, vertices, indices);
    Vec3 size = bounds.GetBoundsSize();
    
    //选最长的轴
    if (size.x >= size.y && size.x >= size.z)
        return 0; 
    if (size.y >= size.z)
        return 1; 
    return 2;    
}

void BVH::QueryRecursive(const BVHNode* node, const Vec3& point, float radius, std::vector<int>& outTriangles) const
{
    if (!node)
        return;
    
    AABB3 expandedBounds = node->m_bounds;
    expandedBounds.m_mins -= Vec3(radius, radius, radius);
    expandedBounds.m_maxs += Vec3(radius, radius, radius);
    
    if (!expandedBounds.IsPointInside(point))
        return;
    
    if (node->IsLeaf())
    {
        outTriangles.insert(outTriangles.end(), 
                          node->m_triangleIndices.begin(), 
                          node->m_triangleIndices.end());
        return;
    }
    
    if (node->m_left)
        QueryRecursive(node->m_left.get(), point, radius, outTriangles);
    if (node->m_right)
        QueryRecursive(node->m_right.get(), point, radius, outTriangles);
}

void BVH::QueryRecursive(const BVHNode* node, const AABB3& bounds, std::vector<int>& outTriangles) const
{
    if (!node)
        return;
    
    if (!DoAABBsOverlap3D(node->m_bounds, bounds))
        return;
    
    if (node->IsLeaf())
    {
        outTriangles.insert(outTriangles.end(), 
                          node->m_triangleIndices.begin(), 
                          node->m_triangleIndices.end());
        return;
    }
    
    QueryRecursive(node->m_left.get(), bounds, outTriangles);
    QueryRecursive(node->m_right.get(), bounds, outTriangles);
}

void BVH::FlattenRecursive(const BVHNode* node, std::vector<GPUBVHNode>& outNodes, std::vector<uint32_t>& outTriIndices, uint32_t& nodeIndex) const
{
    uint32_t currentIndex = nodeIndex++;
    
    if (currentIndex >= outNodes.size())
        outNodes.resize(currentIndex + 1);
    
    //GPUBVHNode& gpuNode = outNodes[currentIndex];
    outNodes[currentIndex].m_boundsMin = node->m_bounds.m_mins;
    outNodes[currentIndex].m_boundsMax = node->m_bounds.m_maxs;
    
    if (node->IsLeaf())
    {
        outNodes[currentIndex].m_leftFirst = (uint32_t)outTriIndices.size();
        outNodes[currentIndex].m_triCount = (uint32_t)node->m_triangleIndices.size();
        outNodes[currentIndex].m_rightChild = 0xFFFFFFFF;  // 无效值
        
        for (int triIdx : node->m_triangleIndices)
        {
            outTriIndices.push_back((uint32_t)triIdx);
        }
    }
    else
    {
        outNodes[currentIndex].m_triCount = 0;
        outNodes[currentIndex].m_leftFirst = nodeIndex; 
        
        uint32_t rightChildIndex = 0xFFFFFFFF;
        
        if (node->m_left)
            FlattenRecursive(node->m_left.get(), outNodes, outTriIndices, nodeIndex);
        
        if (node->m_right)
        {
            rightChildIndex = nodeIndex;
            FlattenRecursive(node->m_right.get(), outNodes, outTriIndices, nodeIndex);
        }
        
        outNodes[currentIndex].m_rightChild = rightChildIndex; 
    }
}
