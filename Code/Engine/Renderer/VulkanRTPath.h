#pragma once

#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <vector>
#include "Engine/Renderer/VulkanRenderer.h"

// Single bottom-level acceleration structure (one mesh) backing data.
// One BLAS per unique mesh; multiple TLAS instances can reference the same BLAS.
// Owns the vertex / index buffer pair the AS was built from (kept alive so
// rebuild / refit can reference them; release on DestroyBLAS).
struct VulkanBLAS
{
    VkAccelerationStructureKHR  handle      = VK_NULL_HANDLE;
    VkBuffer                    asBuffer    = VK_NULL_HANDLE;
    VkDeviceMemory              asMemory    = VK_NULL_HANDLE;
    VkBuffer                    vertexBuf   = VK_NULL_HANDLE;
    VkDeviceMemory              vertexMem   = VK_NULL_HANDLE;
    VkBuffer                    indexBuf    = VK_NULL_HANDLE;
    VkDeviceMemory              indexMem    = VK_NULL_HANDLE;
    VkDeviceAddress             address     = 0;
};

// Top-level acceleration structure — one per scene (or per layer).
struct VulkanTLAS
{
    VkAccelerationStructureKHR  handle      = VK_NULL_HANDLE;
    VkBuffer                    buffer      = VK_NULL_HANDLE;
    VkDeviceMemory              memory      = VK_NULL_HANDLE;
    VkBuffer                    instBuffer  = VK_NULL_HANDLE;     // VkAccelerationStructureInstanceKHR[]
    VkDeviceMemory              instMemory  = VK_NULL_HANDLE;
    VkDeviceAddress             address     = 0;
    uint32_t                    instanceCount = 0;
};

// Shader Binding Table — packed handles for raygen / miss / hit groups.
// Layout (per Vulkan RT spec): contiguous regions, each aligned to
// shaderGroupBaseAlignment, each entry sized shaderGroupHandleAlignment up.
struct VulkanSBT
{
    VkBuffer        buffer = VK_NULL_HANDLE;
    VkDeviceMemory  memory = VK_NULL_HANDLE;

    VkStridedDeviceAddressRegionKHR raygenRegion{};
    VkStridedDeviceAddressRegionKHR missRegion{};
    VkStridedDeviceAddressRegionKHR hitRegion{};
    VkStridedDeviceAddressRegionKHR callableRegion{};   // unused, zeroed
};

class VulkanRTPath
{
public:
    void Init(VulkanRenderer* renderer);
    void Shutdown();

    void OnSwapchainResized();

    // App calls these once per scene change. Vertex layout is float[3] positions
    // only (stride = 12). For the first cut we don't need normals/UVs in the AS —
    // attribute fetch happens in the closest-hit shader from a separate buffer.
    VulkanBLAS BuildBLAS(const float* vertexPositions, uint32_t vertexCount,
                         const uint32_t* indices,        uint32_t indexCount);
    VulkanTLAS BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances);

    void DestroyBLAS(VulkanBLAS& blas);
    void DestroyTLAS(VulkanTLAS& tlas);

    // App calls these once at startup (after RT pipeline + SBT need updating
    // when shaders change, but for the demo we build once).
    void CreateRTPipeline(const char* rgenSpvPath,
                          const char* rchitSpvPath,
                          const char* rmissSpvPath);
    void CreateSBT();

    // Hook the descriptor set to a freshly-built TLAS (binds output image,
    // TLAS, camera UBO into bindings 0/1/2). Call once after BuildTLAS +
    // CreateRTPipeline + CreateOutputImage are all done.
    void UpdateDescriptors(const VulkanTLAS& tlas);

    // Per-frame: copy 2 column-major mat4s (view-inverse, proj-inverse) to
    // the host-mapped camera UBO. 128 bytes total.
    void UpdateCamera(const float* viewInverse_4x4, const float* projInverse_4x4);

    // Recreate output storage image at the given extent. Called once at
    // startup, again on swapchain resize. Re-runs UpdateDescriptors-equivalent
    // re-binding for binding 0 (image view) using the most recent TLAS handle.
    void RecreateOutput(uint32_t width, uint32_t height);

    // App calls this per-frame to dispatch ray gen.
    // Output is m_outputImage (storage image, layout GENERAL) which the caller
    // must transition + blit to swapchain after this call.
    void TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height);

    // Issues a raygen-write -> transfer-read barrier on the output image,
    // transitions the current swapchain image UNDEFINED -> TRANSFER_DST,
    // blits, then transitions to PRESENT_SRC_KHR. Reads m_currentFrame /
    // m_imageIndex / m_swapChainImages off the friend renderer.
    // Call immediately after TraceRays, before VulkanRenderer::EndFrame.
    void BlitToSwapImage(VkCommandBuffer cmd, uint32_t swapW, uint32_t swapH);

    VkImage     GetOutputImage() const { return m_outputImage; }
    VkImageView GetOutputImageView() const { return m_outputImageView; }

private:
    VulkanRenderer* m_renderer = nullptr;
    VkDevice        m_device   = VK_NULL_HANDLE;

    // Output image (storage image — raygen writes here, frame loop blits to swapchain).
    VkImage        m_outputImage     = VK_NULL_HANDLE;
    VkImageView    m_outputImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_outputImageMem  = VK_NULL_HANDLE;
    uint32_t       m_outputWidth     = 0;
    uint32_t       m_outputHeight    = 0;

    // RT pipeline + descriptors.
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_rtPipeline     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet  = VK_NULL_HANDLE;

    // Shader binding table.
    VulkanSBT m_sbt;

    // Camera UBO (RTPath-owned; mapped persistent).
    VkBuffer       m_cameraUBO       = VK_NULL_HANDLE;
    VkDeviceMemory m_cameraUBOMem    = VK_NULL_HANDLE;
    void*          m_cameraUBOMapped = nullptr;

    // Last bound TLAS handle (cached so RecreateOutput can re-bind binding 0
    // without the caller re-passing the TLAS).
    VkAccelerationStructureKHR m_lastBoundTLAS = VK_NULL_HANDLE;

    // One-shot graphics-family cmd pool for AS builds + blits at init time.
    // Distinct from the renderer's per-frame pools so we can submit blocking
    // builds without disturbing the frame loop.
    VkCommandPool m_oneShotPool = VK_NULL_HANDLE;

    // Helpers.
    void           CreateOutputImage(uint32_t width, uint32_t height);
    void           DestroyOutputImage();
    uint32_t       FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);
    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer);
    void           CreateAndAllocateBuffer(VkDeviceSize size,
                                           VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags memProps,
                                           VkBuffer& outBuffer, VkDeviceMemory& outMemory);
    VkDeviceAddress GetASDeviceAddress(VkAccelerationStructureKHR as);

    // One-shot cmd buffer for AS build / blit. Allocates from graphics pool,
    // submits to graphics queue, waits, frees. OK for init-time use.
    VkCommandBuffer BeginOneShotCmd();
    void            EndAndSubmitOneShotCmd(VkCommandBuffer cmd);
};

#endif // ENGINE_VULKAN_RENDERER
