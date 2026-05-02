#pragma once

#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <vector>

class VulkanRenderer;

// 8-point-light constants for the deferred lighting subpass. Binding: set 1, binding 3.
struct DeferredPointLight
{
    float posAndRadius[4];        // xyz = world pos, w = radius
    float colorAndIntensity[4];   // rgb = color, a = intensity
};

struct DeferredLightConstants
{
    DeferredPointLight lights[8] = {};
    int numLights = 0;
    int _pad0 = 0;
    int _pad1 = 0;
    int _pad2 = 0;
};

class VulkanDeferredPath
{
public:
    void Init(VulkanRenderer* renderer);
    void Shutdown();

    void OnSwapchainResized();

    void BeginGBuffer(VkCommandBuffer cmd, uint32_t swapImageIdx);
    void EndGBufferAndRunLighting(VkCommandBuffer cmd, const DeferredLightConstants& lights);

    // Forward A/B path: a single-pass equivalent that evaluates the same Blinn-Phong
    // lighting per fragment with no G-buffer. Used to compare bandwidth / perf cost.
    void BeginForwardLit(VkCommandBuffer cmd, uint32_t swapImageIdx, const DeferredLightConstants& lights);
    void EndForwardLit(VkCommandBuffer cmd);

    // Forward overlay pass (LOAD_OP_LOAD on swap image): for HUD, DevConsole, ImGui-style draws.
    void BeginForwardOverlay(VkCommandBuffer cmd, uint32_t swapImageIdx);
    void EndForwardOverlay(VkCommandBuffer cmd);

    bool TryGetLastFrameTimings(double& outGBufferMs, double& outLightingMs);
    bool TryGetLastForwardMs(double& outForwardMs);

    VkPipelineLayout GetGBufferPipelineLayout() const { return m_gbufferPipelineLayout; }

private:
    VulkanRenderer* m_renderer = nullptr;
    VkDevice        m_device   = VK_NULL_HANDLE;

    VkRenderPass    m_deferredRenderPass = VK_NULL_HANDLE;
    VkRenderPass    m_forwardRenderPass  = VK_NULL_HANDLE;
    VkRenderPass    m_overlayRenderPass  = VK_NULL_HANDLE;
    uint32_t        m_width  = 0;
    uint32_t        m_height = 0;

    struct PerSwap
    {
        VkImage         gAlbedoImage = VK_NULL_HANDLE;
        VkImageView     gAlbedoView  = VK_NULL_HANDLE;
        VkDeviceMemory  gAlbedoMem   = VK_NULL_HANDLE;
        VkImage         gNormalImage = VK_NULL_HANDLE;
        VkImageView     gNormalView  = VK_NULL_HANDLE;
        VkDeviceMemory  gNormalMem   = VK_NULL_HANDLE;
        VkImage         gDepthImage  = VK_NULL_HANDLE;
        VkImageView     gDepthView   = VK_NULL_HANDLE;
        VkDeviceMemory  gDepthMem    = VK_NULL_HANDLE;
        VkImage         fwdDepthImage = VK_NULL_HANDLE;
        VkImageView     fwdDepthView  = VK_NULL_HANDLE;
        VkDeviceMemory  fwdDepthMem   = VK_NULL_HANDLE;
        VkFramebuffer   framebuffer        = VK_NULL_HANDLE;
        VkFramebuffer   forwardFramebuffer = VK_NULL_HANDLE;
        VkFramebuffer   overlayFramebuffer = VK_NULL_HANDLE;
        VkDescriptorSet set1               = VK_NULL_HANDLE;
    };
    std::vector<PerSwap> m_perSwap;

    VkDescriptorSetLayout m_set1Layout         = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_forwardSet1Layout  = VK_NULL_HANDLE;
    VkDescriptorSet       m_forwardSet1        = VK_NULL_HANDLE;

    VkBuffer        m_lightUBO       = VK_NULL_HANDLE;
    VkDeviceMemory  m_lightUBOMem    = VK_NULL_HANDLE;
    void*           m_lightUBOMapped = nullptr;

    VkPipelineLayout m_gbufferPipelineLayout       = VK_NULL_HANDLE;
    VkPipeline       m_gbufferPipeline             = VK_NULL_HANDLE;
    VkPipeline       m_gbufferPipelinePCUTBN       = VK_NULL_HANDLE;
    VkPipelineLayout m_lightingPipelineLayout      = VK_NULL_HANDLE;
    VkPipeline       m_lightingPipeline            = VK_NULL_HANDLE;
    VkPipelineLayout m_forwardPipelineLayout       = VK_NULL_HANDLE;
    VkPipeline       m_forwardPipeline             = VK_NULL_HANDLE;
    VkPipeline       m_forwardPipelinePCUTBN       = VK_NULL_HANDLE;
    VkPipeline       m_overlayPipeline             = VK_NULL_HANDLE;

    // Timestamp pool: 5 queries per in-flight frame slot.
    //   [0] top-of-pipe, [1] end-gbuffer, [2] end-lighting, [3] forward-start, [4] forward-end.
    // Slot f uses queries [5f, 5f+4]. Total pool = 5 * MAX_FRAMES_IN_FLIGHT.
    enum class TimingMode { None, Deferred, Forward };
    VkQueryPool m_queryPool          = VK_NULL_HANDLE;
    double      m_timestampPeriodNs  = 1.0;
    TimingMode  m_perSlotMode[2]     = { TimingMode::None, TimingMode::None };
    double      m_lastGbufMs         = 0.0;
    double      m_lastLightMs        = 0.0;
    double      m_lastForwardMs      = 0.0;
    bool        m_timestampsValid    = false;
    bool        m_forwardTimingValid = false;

    void ResetSlotAndReadPrevious(VkCommandBuffer cmd, TimingMode newMode);

    void CreateRenderPass();
    void CreatePerSwapResources(); // creates G-buffer images + framebuffer + set1 for every swap image
    void DestroyPerSwapResources();
    void CreateSet1LayoutAndLightUBO();
    void CreatePipelines();
    void CreateQueryPool();

    VkShaderModule LoadShaderModule(const char* spvPath);
    uint32_t       FindMemoryTypeWithFallback(uint32_t typeBits, VkMemoryPropertyFlags preferred, VkMemoryPropertyFlags fallback);
};

#endif // ENGINE_VULKAN_RENDERER
