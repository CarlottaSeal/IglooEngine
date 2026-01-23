#pragma once
#include "Engine/Math/AABB3.hpp"
#include <vector>
#include <memory>

struct Vertex_PCUTBN;

struct BVHNode
{
    AABB3 m_bounds;                      // 当前节点的包围盒
    std::unique_ptr<BVHNode> m_left;     // 左子节点
    std::unique_ptr<BVHNode> m_right;    // 右子节点
    std::vector<int> m_triangleIndices;  // 叶子节点：存储三角形索引
    bool m_isLeaf = false;
    
    BVHNode() = default;
    
    bool IsLeaf() const { return m_isLeaf; }
};

struct GPUBVHNode
{
	Vec3 m_boundsMin;
	uint32_t m_leftFirst;     

	Vec3 m_boundsMax;
	uint32_t m_triCount;       
	uint32_t m_rightChild;
	float padding[3];
	bool IsLeaf() const { return m_triCount > 0; }
};

class BVH
{
public:
    BVH() = default;
    ~BVH() = default;

	BVH(const BVH&) = delete;
	BVH& operator=(const BVH&) = delete;

	BVH(BVH&&) noexcept = default;
	BVH& operator=(BVH&&) noexcept = default;
    
    void Build(const std::vector<Vertex_PCUTBN>& vertices, 
               const std::vector<uint32_t>& indices);
    
    void QueryNearbyTriangles(const Vec3& point, float radius, 
                             std::vector<int>& outTriangles) const;
    
    void QueryIntersectingTriangles(const AABB3& bounds, 
                                   std::vector<int>& outTriangles) const;

	void FlattenForGPU(std::vector<GPUBVHNode>& outNodes,
		std::vector<uint32_t>& outTriIndices) const;

private:
    std::unique_ptr<BVHNode> BuildRecursive(
        const std::vector<int>& triangleIndices,
        const std::vector<Vertex_PCUTBN>& vertices,
        const std::vector<uint32_t>& indices,
        int depth);
    
    AABB3 ComputeTriangleBounds(int triIndex, 
                                const std::vector<Vertex_PCUTBN>& vertices,
                                const std::vector<uint32_t>& indices) const;
    
    AABB3 ComputeBounds(const std::vector<int>& triangleIndices,
                       const std::vector<Vertex_PCUTBN>& vertices,
                       const std::vector<uint32_t>& indices) const;
    
    int ChooseSplitAxis(const std::vector<int>& triangleIndices,
                       const std::vector<Vertex_PCUTBN>& vertices,
                       const std::vector<uint32_t>& indices) const;
    
    void QueryRecursive(const BVHNode* node, const Vec3& point, float radius,
                       std::vector<int>& outTriangles) const;
    
    void QueryRecursive(const BVHNode* node, const AABB3& bounds,
                       std::vector<int>& outTriangles) const;

	void FlattenRecursive(const BVHNode* node,
		std::vector<GPUBVHNode>& outNodes,
		std::vector<uint32_t>& outTriIndices,
		uint32_t& nodeIndex) const;

private:
    std::unique_ptr<BVHNode> m_root;
    const std::vector<Vertex_PCUTBN>* m_vertices = nullptr;
    const std::vector<uint32_t>* m_indices = nullptr;
    
    static constexpr int MAX_TRIANGLES_PER_LEAF = 8;  // 叶子节点最多三角形数
    static constexpr int MAX_DEPTH = 16;              // 最大深度
};
