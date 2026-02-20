#pragma once
#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER

#include <vulkan/vulkan.h>
#include <vector>
#include <list>
#include <mutex>
#include <unordered_map>

//==============================================================================
// Memory allocation handle returned to users
//==============================================================================
struct VulkanAllocation
{
    VkDeviceMemory memory = VK_NULL_HANDLE;     // The device memory block
    VkDeviceSize offset = 0;                     // Offset within the memory block
    VkDeviceSize size = 0;                       // Size of this allocation
    void* mappedPtr = nullptr;                   // Persistent mapped pointer (if HOST_VISIBLE)
    uint32_t poolIndex = 0;                      // Which pool this came from
    uint32_t blockIndex = 0;                     // Which block within the pool
    bool isValid = false;
};

//==============================================================================
// Memory type categories for easier usage
//==============================================================================
enum class VulkanMemoryUsage
{
    GPU_ONLY,           // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT (best for static GPU data)
    CPU_TO_GPU,         // HOST_VISIBLE | HOST_COHERENT (for dynamic data uploaded from CPU)
    GPU_TO_CPU,         // HOST_VISIBLE | HOST_CACHED (for readback)
    CPU_ONLY            // HOST_VISIBLE | HOST_COHERENT | HOST_CACHED
};

//==============================================================================
// A single large memory block that contains multiple sub-allocations
//==============================================================================
class VulkanMemoryBlock
{
public:
    VulkanMemoryBlock(VkDevice device, VkDeviceSize size, uint32_t memoryTypeIndex, bool canMap);
    ~VulkanMemoryBlock();

    // Try to allocate from this block, returns false if not enough space
    bool Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAllocation);

    // Free a sub-allocation
    void Free(const VulkanAllocation& allocation);

    // Check if block is completely empty
    bool IsEmpty() const;

    // Get total free space
    VkDeviceSize GetAFreeSpace() const;

    VkDeviceMemory GetMemory() const { return m_memory; }
    void* GetMappedPtr() const { return m_mappedPtr; }
    uint32_t GetMemoryTypeIndex() const { return m_memoryTypeIndex; }

private:
    struct FreeRegion
    {
        VkDeviceSize offset;
        VkDeviceSize size;
    };

    VkDevice m_device = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    uint32_t m_memoryTypeIndex = 0;
    void* m_mappedPtr = nullptr;
    bool m_canMap = false;

    std::list<FreeRegion> m_freeRegions;  // Sorted list of free regions
    VkDeviceSize m_totalFreeSpace = 0;

    // Merge adjacent free regions
    void MergeFreeRegions();
};

//==============================================================================
// Memory pool for a specific memory type
//==============================================================================
class VulkanMemoryTypePool
{
public:
    VulkanMemoryTypePool(VkDevice device, VkPhysicalDevice physicalDevice,
                         uint32_t memoryTypeIndex, VkDeviceSize blockSize, bool canMap);
    ~VulkanMemoryTypePool();

    // Allocate memory
    bool Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAllocation);

    // Free memory
    void Free(const VulkanAllocation& allocation);

    // Get stats
    size_t GetBlockCount() const { return m_blocks.size(); }
    VkDeviceSize GetTotalAllocated() const;
    VkDeviceSize GetTotalUsed() const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    uint32_t m_memoryTypeIndex = 0;
    VkDeviceSize m_blockSize = 0;
    bool m_canMap = false;

    std::vector<VulkanMemoryBlock*> m_blocks;
};

//==============================================================================
// Main memory pool manager
//==============================================================================
class VulkanMemoryPool
{
public:
    VulkanMemoryPool();
    ~VulkanMemoryPool();

    // Initialize with device and physical device
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void Shutdown();

    // Allocate memory for a buffer
    VulkanAllocation AllocateBufferMemory(VkBuffer buffer, VulkanMemoryUsage usage);

    // Allocate memory for an image
    VulkanAllocation AllocateImageMemory(VkImage image, VulkanMemoryUsage usage);

    // Allocate with specific requirements
    VulkanAllocation Allocate(VkMemoryRequirements requirements, VulkanMemoryUsage usage);

    // Free an allocation
    void Free(VulkanAllocation& allocation);

    // Get statistics
    void PrintStats() const;
    size_t GetTotalAllocationCount() const { return m_allocationCount; }
    VkDeviceSize GetTotalAllocatedBytes() const { return m_totalAllocatedBytes; }

    // Find memory type index
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties m_memProperties{};

    // Default block sizes
    static constexpr VkDeviceSize DEFAULT_BLOCK_SIZE_GPU = 256 * 1024 * 1024;      // 256 MB for GPU memory
    static constexpr VkDeviceSize DEFAULT_BLOCK_SIZE_HOST = 64 * 1024 * 1024;      // 64 MB for host memory
    static constexpr VkDeviceSize SMALL_ALLOCATION_THRESHOLD = 1024 * 1024;        // 1 MB

    // Pools indexed by memory type index
    std::unordered_map<uint32_t, VulkanMemoryTypePool*> m_pools;

    // Statistics
    size_t m_allocationCount = 0;
    VkDeviceSize m_totalAllocatedBytes = 0;

    std::mutex m_mutex;  // Thread safety

    // Get or create pool for memory type
    VulkanMemoryTypePool* GetOrCreatePool(uint32_t memoryTypeIndex, bool canMap);

    // Convert usage to memory properties
    VkMemoryPropertyFlags GetMemoryProperties(VulkanMemoryUsage usage) const;
};

// Global memory pool instance (will be managed by VulkanRenderer)
extern VulkanMemoryPool* g_vulkanMemoryPool;

#endif // ENGINE_VULKAN_RENDERER
