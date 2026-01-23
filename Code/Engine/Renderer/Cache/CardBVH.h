#pragma once

#include "Engine/Math/Vec3.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Renderer/Cache/CacheCommon.h"
#include <vector>
#include <memory>
#include <cstdint>

// ========================================
// GPU 端的 Card BVH 节点结构
// ========================================
struct GPUCardBVHNode
{
    float m_boundsMinX, m_boundsMinY, m_boundsMinZ;
    float m_padding0;
    
    float m_boundsMaxX, m_boundsMaxY, m_boundsMaxZ;
    float m_padding1;
    
    uint32_t m_leftFirst;   // 内部节点：左孩子索引；叶子节点：Card 起始索引
    uint32_t m_cardCount;   // 叶子节点：Card 数量；内部节点：0
    uint32_t m_padding2;
    uint32_t m_padding3;
    
    GPUCardBVHNode()
        : m_boundsMinX(0), m_boundsMinY(0), m_boundsMinZ(0), m_padding0(0)
        , m_boundsMaxX(0), m_boundsMaxY(0), m_boundsMaxZ(0), m_padding1(0)
        , m_leftFirst(0), m_cardCount(0), m_padding2(0), m_padding3(0)
    {}
};

// ========================================
// CPU 端的 Card BVH 节点结构
// ========================================
struct CardBVHNode
{
    AABB3 m_bounds;
    bool m_isLeaf;
    
    // 叶子节点：存储 Card 索引
    std::vector<uint32_t> m_cardIndices;
    
    // 内部节点：子节点
    std::unique_ptr<CardBVHNode> m_left;
    std::unique_ptr<CardBVHNode> m_right;
    
    CardBVHNode()
        : m_bounds(Vec3(0, 0, 0), Vec3(0, 0, 0))
        , m_isLeaf(false)
    {}
    
    bool IsLeaf() const { return m_isLeaf; }
};

// ========================================
// Card BVH 类
// ========================================
class CardBVH
{
public:
    CardBVH() = default;
    ~CardBVH() = default;
    
    // ========== 核心功能 ==========
    
    // 从 Card Metadata 列表构建 BVH
    void Build(const std::vector<SurfaceCardMetadata>& cards);
    
    // 清空 BVH
    void Clear();
    
    // ========== 查询功能 ==========
    
    // 查询与 AABB 相交的 Card 索引
    void QueryIntersecting(const AABB3& bounds, std::vector<uint32_t>& outCardIndices) const;
    
    // 查询与射线相交的 Card 索引
    void QueryRay(const Vec3& origin, const Vec3& dir, float maxDist, std::vector<uint32_t>& outCardIndices) const;
    
    // 查询点附近的 Card 索引
    void QueryNearby(const Vec3& point, float radius, std::vector<uint32_t>& outCardIndices) const;
    
    // ========== GPU 相关 ==========
    
    // 扁平化为 GPU 格式
    void FlattenForGPU(std::vector<GPUCardBVHNode>& outNodes, std::vector<uint32_t>& outCardIndices) const;
    
    // ========== 统计信息 ==========
    
    int GetMaxDepth() const { return m_maxDepth; }
    int GetNodeCount() const { return m_nodeCount; }
    int GetLeafCount() const { return m_leafCount; }
    AABB3 GetRootBounds() const { return m_root ? m_root->m_bounds : AABB3(); }
    
private:
    // ========== 构建相关 ==========
    
    std::unique_ptr<CardBVHNode> BuildRecursive(
        const std::vector<uint32_t>& cardIndices,
        int depth
    );
    
    // 计算单个 Card 的 AABB
    AABB3 ComputeCardBounds(uint32_t cardIndex) const;
    
    // 计算 Card 集合的 AABB
    AABB3 ComputeBounds(const std::vector<uint32_t>& cardIndices) const;
    
    // 选择分割轴（0=X, 1=Y, 2=Z）
    int ChooseSplitAxis(const std::vector<uint32_t>& cardIndices) const;
    
    // 计算 Card 的中心点
    Vec3 GetCardCenter(uint32_t cardIndex) const;
    
    // ========== 查询相关 ==========
    
    void QueryRecursive(
        const CardBVHNode* node,
        const AABB3& bounds,
        std::vector<uint32_t>& outCardIndices
    ) const;
    
    void QueryRayRecursive(
        const CardBVHNode* node,
        const Vec3& origin,
        const Vec3& dir,
        float maxDist,
        std::vector<uint32_t>& outCardIndices
    ) const;
    
    void QueryNearbyRecursive(
        const CardBVHNode* node,
        const Vec3& point,
        float radius,
        std::vector<uint32_t>& outCardIndices
    ) const;
    
    // ========== GPU 扁平化相关 ==========
    
    void FlattenRecursive(
        const CardBVHNode* node,
        std::vector<GPUCardBVHNode>& outNodes,
        std::vector<uint32_t>& outCardIndices,
        uint32_t& nodeIndex
    ) const;
    
    // ========== 辅助函数 ==========
    
    // 射线 vs AABB 相交测试
    bool RayAABBIntersect(
        const Vec3& origin,
        const Vec3& dir,
        const AABB3& bounds,
        float maxDist
    ) const;
    
    // ========== 成员变量 ==========
    
    std::unique_ptr<CardBVHNode> m_root;
    const std::vector<SurfaceCardMetadata>* m_cards = nullptr;
    
    int m_maxDepth = 0;
    int m_nodeCount = 0;
    int m_leafCount = 0;
    
    // ========== 配置参数 ==========
    
    static constexpr int MAX_CARDS_PER_LEAF = 4;    // 每个叶子节点最多 4 个 Cards
    static constexpr int MAX_DEPTH = 24;             // 最大深度
};