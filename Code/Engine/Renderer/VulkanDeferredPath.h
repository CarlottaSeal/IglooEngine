#pragma once

#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <vector>
#include "Engine/Renderer/VulkanRenderer.h"

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

    // Multithreaded subpass-0 recording. When enabled, BeginGBuffer opens subpass 0 with
    // SECONDARY contents and the caller drives recording via the per-thread secondaries:
    //   thread 0 = main (engine-API draws via SetActiveRecordingTarget override)
    //   thread 1..N-1 = worker threads (raw Vulkan recording)
    // EndGBufferAndRunLighting then calls vkCmdExecuteCommands on the primary.
    static constexpr int kNumThreads = 3; // 1 main + 2 workers
    void SetMultithreadingEnabled(bool on) { m_multithreadingEnabled = on; }
    bool IsMultithreadingEnabled() const   { return m_multithreadingEnabled; }
    VkCommandBuffer BeginThreadSecondary(int threadIdx, uint32_t swapImageIdx);
    void            EndThreadSecondary(int threadIdx);

    // Pre-staged draw for worker-thread playback. Main thread captures all the per-draw
    // state (model UBO offset, material indices, pipeline) at prepare time; worker
    // threads replay vkCmd* sequences into their secondary cmd buffers using only this struct.
    struct PreparedDraw
    {
        VkPipeline     pipeline           = VK_NULL_HANDLE;
        VkBuffer       vbo                = VK_NULL_HANDLE;
        VkBuffer       ibo                = VK_NULL_HANDLE;
        uint32_t       indexCount         = 0;
        uint32_t       cameraDynamicOffset = 0;
        uint32_t       modelDynamicOffset  = 0;
        VulkanRenderer::VkMaterialPC material;
    };
    void RecordPreparedDraws(VkCommandBuffer secondary, const PreparedDraw* draws, int count);
    VkPipeline GetGBufferPCUTBNPipeline() const { return m_gbufferPipelinePCUTBN; }

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

    // Per-thread secondary cmd buffer + pool, doubled for in-flight frame slots so
    // a slot's pool can be reset only after its last submission has GPU-completed.
    struct PerThread
    {
        VkCommandPool   pool[2]      = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkCommandBuffer secondary[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        bool            begunThisFrame = false;
    };
    PerThread m_threads[kNumThreads];
    bool      m_multithreadingEnabled = false;
    void CreateThreadResources();
    void DestroyThreadResources();

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
