#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER

#include "VulkanMemoryPool.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include <algorithm>

VulkanMemoryPool* g_vulkanMemoryPool = nullptr;

VulkanMemoryBlock::VulkanMemoryBlock(VkDevice device, VkDeviceSize size, uint32_t memoryTypeIndex, bool canMap)
    : m_device(device)
    , m_size(size)
    , m_memoryTypeIndex(memoryTypeIndex)
    , m_canMap(canMap)
    , m_totalFreeSpace(size)
{
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory);
    if (result != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to allocate Vulkan memory block!");
    }

    // Map memory if possible for persistent mapping
    if (m_canMap)
    {
        result = vkMapMemory(m_device, m_memory, 0, size, 0, &m_mappedPtr);
        if (result != VK_SUCCESS)
        {
            DebuggerPrintf("Warning: Failed to map Vulkan memory block\n");
            m_mappedPtr = nullptr;
        }
    }

    // Initially, entire block is free
    FreeRegion initialRegion;
    initialRegion.offset = 0;
    initialRegion.size = size;
    m_freeRegions.push_back(initialRegion);

    DebuggerPrintf("VulkanMemoryBlock: Allocated %llu bytes (type %u)\n", size, memoryTypeIndex);
}

VulkanMemoryBlock::~VulkanMemoryBlock()
{
    if (m_mappedPtr)
    {
        vkUnmapMemory(m_device, m_memory);
        m_mappedPtr = nullptr;
    }

    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

bool VulkanMemoryBlock::Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAllocation)
{
    // Find a free region that can satisfy this allocation
    for (auto it = m_freeRegions.begin(); it != m_freeRegions.end(); ++it)
    {
        // Calculate aligned offset
        VkDeviceSize alignedOffset = (it->offset + alignment - 1) & ~(alignment - 1);
        VkDeviceSize alignmentPadding = alignedOffset - it->offset;
        VkDeviceSize totalNeeded = alignmentPadding + size;

        if (it->size >= totalNeeded)
        {
            // Found a suitable region
            outAllocation.memory = m_memory;
            outAllocation.offset = alignedOffset;
            outAllocation.size = size;
            outAllocation.isValid = true;

            // Set mapped pointer if available
            if (m_mappedPtr)
            {
                outAllocation.mappedPtr = static_cast<uint8_t*>(m_mappedPtr) + alignedOffset;
            }

            // Update free region
            if (alignmentPadding > 0)
            {
                // There's padding before the allocation - keep it as free
                FreeRegion paddingRegion;
                paddingRegion.offset = it->offset;
                paddingRegion.size = alignmentPadding;

                // Remaining space after allocation
                VkDeviceSize remaining = it->size - totalNeeded;
                if (remaining > 0)
                {
                    it->offset = alignedOffset + size;
                    it->size = remaining;
                    m_freeRegions.insert(it, paddingRegion);
                }
                else
                {
                    it->offset = paddingRegion.offset;
                    it->size = paddingRegion.size;
                }
            }
            else
            {
                // No padding needed
                VkDeviceSize remaining = it->size - size;
                if (remaining > 0)
                {
                    it->offset += size;
                    it->size = remaining;
                }
                else
                {
                    m_freeRegions.erase(it);
                }
            }

            m_totalFreeSpace -= size;
            return true;
        }
    }

    return false;  // No suitable region found
}

void VulkanMemoryBlock::Free(const VulkanAllocation& allocation)
{
    // Add the region back to free list
    FreeRegion region;
    region.offset = allocation.offset;
    region.size = allocation.size;

    // Insert in sorted order
    auto it = m_freeRegions.begin();
    while (it != m_freeRegions.end() && it->offset < region.offset)
    {
        ++it;
    }
    m_freeRegions.insert(it, region);

    m_totalFreeSpace += allocation.size;

    // Merge adjacent regions
    MergeFreeRegions();
}

bool VulkanMemoryBlock::IsEmpty() const
{
    return m_freeRegions.size() == 1 && m_freeRegions.front().size == m_size;
}

VkDeviceSize VulkanMemoryBlock::GetAFreeSpace() const
{
    return m_totalFreeSpace;
}

void VulkanMemoryBlock::MergeFreeRegions()
{
    if (m_freeRegions.size() < 2)
        return;

    auto it = m_freeRegions.begin();
    while (it != m_freeRegions.end())
    {
        auto next = std::next(it);
        if (next == m_freeRegions.end())
            break;

        // Check if regions are adjacent
        if (it->offset + it->size == next->offset)
        {
            // Merge
            it->size += next->size;
            m_freeRegions.erase(next);
            // Don't advance - check if we can merge again
        }
        else
        {
            ++it;
        }
    }
}

//==============================================================================
// VulkanMemoryTypePool Implementation
//==============================================================================
VulkanMemoryTypePool::VulkanMemoryTypePool(VkDevice device, VkPhysicalDevice physicalDevice,
                                           uint32_t memoryTypeIndex, VkDeviceSize blockSize, bool canMap)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_memoryTypeIndex(memoryTypeIndex)
    , m_blockSize(blockSize)
    , m_canMap(canMap)
{
}

VulkanMemoryTypePool::~VulkanMemoryTypePool()
{
    for (auto* block : m_blocks)
    {
        delete block;
    }
    m_blocks.clear();
}

bool VulkanMemoryTypePool::Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAllocation)
{
    // Try existing blocks first
    for (size_t i = 0; i < m_blocks.size(); ++i)
    {
        if (m_blocks[i]->Allocate(size, alignment, outAllocation))
        {
            outAllocation.poolIndex = m_memoryTypeIndex;
            outAllocation.blockIndex = static_cast<uint32_t>(i);
            return true;
        }
    }

    // Need a new block
    VkDeviceSize newBlockSize = m_blockSize;

    // If allocation is larger than default block size, create a dedicated block
    if (size > m_blockSize)
    {
        newBlockSize = size;
    }

    VulkanMemoryBlock* newBlock = new VulkanMemoryBlock(m_device, newBlockSize, m_memoryTypeIndex, m_canMap);
    m_blocks.push_back(newBlock);

    if (newBlock->Allocate(size, alignment, outAllocation))
    {
        outAllocation.poolIndex = m_memoryTypeIndex;
        outAllocation.blockIndex = static_cast<uint32_t>(m_blocks.size() - 1);
        return true;
    }

    return false;
}

void VulkanMemoryTypePool::Free(const VulkanAllocation& allocation)
{
    if (allocation.blockIndex < m_blocks.size())
    {
        m_blocks[allocation.blockIndex]->Free(allocation);

        // Optionally: remove empty blocks (but keep at least one)
        // This prevents excessive block creation/destruction
    }
}

VkDeviceSize VulkanMemoryTypePool::GetTotalAllocated() const
{
    // Note: blocks can have different sizes (dedicated allocations may be larger than m_blockSize)
    // For simplicity, we use m_blockSize * count as an approximation
    // A more accurate implementation would track actual block sizes
    return m_blocks.size() * m_blockSize;
}

VkDeviceSize VulkanMemoryTypePool::GetTotalUsed() const
{
    VkDeviceSize totalFree = 0;
    for (const auto* block : m_blocks)
    {
        totalFree += block->GetAFreeSpace();
    }
    return (m_blocks.size() * m_blockSize) - totalFree;
}

//==============================================================================
// VulkanMemoryPool Implementation
//==============================================================================
VulkanMemoryPool::VulkanMemoryPool()
{
}

VulkanMemoryPool::~VulkanMemoryPool()
{
    Shutdown();
}

void VulkanMemoryPool::Initialize(VkDevice device, VkPhysicalDevice physicalDevice)
{
    m_device = device;
    m_physicalDevice = physicalDevice;

    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProperties);

    DebuggerPrintf("VulkanMemoryPool: Initialized with %u memory types, %u heaps\n",
                   m_memProperties.memoryTypeCount, m_memProperties.memoryHeapCount);

    // Print memory info
    for (uint32_t i = 0; i < m_memProperties.memoryHeapCount; ++i)
    {
        const VkMemoryHeap& heap = m_memProperties.memoryHeaps[i];
        bool isDeviceLocal = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        DebuggerPrintf("  Heap %u: %llu MB (%s)\n", i, heap.size / (1024 * 1024),
                       isDeviceLocal ? "Device Local" : "Host");
    }
}

void VulkanMemoryPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& pair : m_pools)
    {
        delete pair.second;
    }
    m_pools.clear();

    DebuggerPrintf("VulkanMemoryPool: Shutdown. Total allocations: %zu, Total bytes: %llu\n",
                   m_allocationCount, m_totalAllocatedBytes);

    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
}

VulkanAllocation VulkanMemoryPool::AllocateBufferMemory(VkBuffer buffer, VulkanMemoryUsage usage)
{
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    return Allocate(memRequirements, usage);
}

VulkanAllocation VulkanMemoryPool::AllocateImageMemory(VkImage image, VulkanMemoryUsage usage)
{
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    return Allocate(memRequirements, usage);
}

VulkanAllocation VulkanMemoryPool::Allocate(VkMemoryRequirements requirements, VulkanMemoryUsage usage)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    VulkanAllocation allocation{};

    VkMemoryPropertyFlags properties = GetMemoryProperties(usage);
    uint32_t memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);

    if (memoryTypeIndex == UINT32_MAX)
    {
        ERROR_AND_DIE("Failed to find suitable memory type!");
        return allocation;
    }

    bool canMap = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    VulkanMemoryTypePool* pool = GetOrCreatePool(memoryTypeIndex, canMap);

    if (pool->Allocate(requirements.size, requirements.alignment, allocation))
    {
        m_allocationCount++;
        m_totalAllocatedBytes += requirements.size;
        return allocation;
    }

    ERROR_AND_DIE("Failed to allocate memory from pool!");
    return allocation;
}

void VulkanMemoryPool::Free(VulkanAllocation& allocation)
{
    if (!allocation.isValid)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_pools.find(allocation.poolIndex);
    if (it != m_pools.end())
    {
        it->second->Free(allocation);
        m_allocationCount--;
        m_totalAllocatedBytes -= allocation.size;
    }

    allocation.isValid = false;
    allocation.memory = VK_NULL_HANDLE;
    allocation.mappedPtr = nullptr;
}

void VulkanMemoryPool::PrintStats() const
{
    DebuggerPrintf("=== VulkanMemoryPool Stats ===\n");
    DebuggerPrintf("Total allocations: %zu\n", m_allocationCount);
    DebuggerPrintf("Total allocated bytes: %llu MB\n", m_totalAllocatedBytes / (1024 * 1024));
    DebuggerPrintf("Number of pools: %zu\n", m_pools.size());

    for (const auto& pair : m_pools)
    {
        DebuggerPrintf("  Pool (type %u): %zu blocks, %llu MB used\n",
                       pair.first, pair.second->GetBlockCount(),
                       pair.second->GetTotalUsed() / (1024 * 1024));
    }
    DebuggerPrintf("==============================\n");
}

uint32_t VulkanMemoryPool::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    for (uint32_t i = 0; i < m_memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (m_memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

VulkanMemoryTypePool* VulkanMemoryPool::GetOrCreatePool(uint32_t memoryTypeIndex, bool canMap)
{
    auto it = m_pools.find(memoryTypeIndex);
    if (it != m_pools.end())
    {
        return it->second;
    }

    // Determine block size based on memory type
    VkDeviceSize blockSize = canMap ? DEFAULT_BLOCK_SIZE_HOST : DEFAULT_BLOCK_SIZE_GPU;

    VulkanMemoryTypePool* pool = new VulkanMemoryTypePool(
        m_device, m_physicalDevice, memoryTypeIndex, blockSize, canMap);

    m_pools[memoryTypeIndex] = pool;

    DebuggerPrintf("VulkanMemoryPool: Created pool for memory type %u (block size: %llu MB)\n",
                   memoryTypeIndex, blockSize / (1024 * 1024));

    return pool;
}

VkMemoryPropertyFlags VulkanMemoryPool::GetMemoryProperties(VulkanMemoryUsage usage) const
{
    switch (usage)
    {
    case VulkanMemoryUsage::GPU_ONLY:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    case VulkanMemoryUsage::CPU_TO_GPU:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    case VulkanMemoryUsage::GPU_TO_CPU:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    case VulkanMemoryUsage::CPU_ONLY:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
               VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    default:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
}

#endif // ENGINE_VULKAN_RENDERER
