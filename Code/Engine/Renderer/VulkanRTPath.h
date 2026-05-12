#pragma once

#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <vector>
#include "Engine/Renderer/VulkanRenderer.h"

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

struct VulkanTLAS
{
    VkAccelerationStructureKHR  handle      = VK_NULL_HANDLE;
    VkBuffer                    buffer      = VK_NULL_HANDLE;
    VkDeviceMemory              memory      = VK_NULL_HANDLE;
    VkBuffer                    instBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory              instMemory  = VK_NULL_HANDLE;
    VkDeviceAddress             address     = 0;
    uint32_t                    instanceCount = 0;
};

struct VulkanSBT
{
    VkBuffer        buffer = VK_NULL_HANDLE;
    VkDeviceMemory  memory = VK_NULL_HANDLE;

    VkStridedDeviceAddressRegionKHR raygenRegion{};
    VkStridedDeviceAddressRegionKHR missRegion{};
    VkStridedDeviceAddressRegionKHR hitRegion{};
    VkStridedDeviceAddressRegionKHR callableRegion{};
};

class VulkanRTPath
{
public:
    void Init(VulkanRenderer* renderer);
    void Shutdown();

    void OnSwapchainResized();

    VulkanBLAS BuildBLAS(const float* vertexPositions, uint32_t vertexCount,
                         const uint32_t* indices,        uint32_t indexCount);
    VulkanTLAS BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances);

    void DestroyBLAS(VulkanBLAS& blas);
    void DestroyTLAS(VulkanTLAS& tlas);

    void CreateRTPipeline(const char* rgenSpvPath,
                          const char* rchitSpvPath,
                          const char* rmissSpvPath,
                          const char* rshadowMissSpvPath);
    void CreateSBT();

    void UpdateDescriptors(const VulkanTLAS& tlas, const VulkanBLAS& geomBLAS);

    // matColorsRGB: 3 floats per material. triMatIds: one uint per triangle.
    void SetMaterialBuffers(const float*    matColorsRGB, uint32_t numMaterials,
                            const uint32_t* triMatIds,    uint32_t numTriangles);

    // uvCoords: 2 floats per UV vertex. uvIndices: 3 uints per triangle.
    void SetUVs(const float*    uvCoords,   uint32_t numUVs,
                const uint32_t* uvIndices,  uint32_t numTriangles);

    // Loads TGAs from disk into a bindless texture array (binding 7).
    // matDiffuseSlot[mat] / matNormalSlot[mat] = -1 means "no texture".
    // Up to kMaxRTTextures unique textures supported.
    void SetTextures(const std::vector<std::string>& texturePaths,
                     const int32_t* matDiffuseSlot,
                     const int32_t* matNormalSlot,
                     uint32_t numMaterials);

    static constexpr uint32_t kMaxRTTextures = 64;

    void UpdateCameraVectors(const float eye[3],
                             const float forward[3],
                             const float right[3],
                             const float up[3],
                             float fovTan,
                             float aspect,
                             uint32_t frameId,
                             uint32_t numLights);

    // 8 floats per light: vec4(pos.xyz, intensity), vec4(color.rgb, _pad).
    void SetLights(const float* lightData, uint32_t numLights);

    void RecreateOutput(uint32_t width, uint32_t height);

    void TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height);

    // Loads atrous.comp + composite.comp, builds compute pipelines + DSs.
    // Call once after CreateRTPipeline. Image bindings reference the output /
    // history / albedo / ping / pong images created by RecreateOutput.
    void CreateDenoisePipelines(const char* atrousSpvPath, const char* compositeSpvPath);

    // Run the atrous chain (3 passes: stride 1, 2, 4) + composite. Caller
    // must have already submitted TraceRays in the same cmd buffer.
    void RunDenoise(VkCommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameId);

    // finalLayout = COLOR_ATTACHMENT_OPTIMAL when an overlay pass loads after.
    void BlitToSwapImage(VkCommandBuffer cmd, uint32_t swapW, uint32_t swapH,
                         VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VkImage     GetOutputImage() const { return m_outputImage; }
    VkImageView GetOutputImageView() const { return m_outputImageView; }
    VkImage     GetDepthImage()     const { return m_depthImage; }
    VkImageView GetDepthImageView() const { return m_depthImageView; }

private:
    VulkanRenderer* m_renderer = nullptr;
    VkDevice        m_device   = VK_NULL_HANDLE;

    VkImage        m_outputImage     = VK_NULL_HANDLE;
    VkImageView    m_outputImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_outputImageMem  = VK_NULL_HANDLE;
    uint32_t       m_outputWidth     = 0;
    uint32_t       m_outputHeight    = 0;

    // TAA history (RGBA16F storage image, GENERAL layout). Raygen reads
    // prev-frame color, blends with new, writes back.
    VkImage        m_historyImage    = VK_NULL_HANDLE;
    VkImageView    m_historyImageView= VK_NULL_HANDLE;
    VkDeviceMemory m_historyImageMem = VK_NULL_HANDLE;

    // Albedo G-buffer (rgba8 storage). Closesthit writes baseColor; raygen
    // multiplies after filtering the un-modulated lighting term.
    VkImage        m_albedoImage     = VK_NULL_HANDLE;
    VkImageView    m_albedoImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_albedoImageMem  = VK_NULL_HANDLE;

    // A-Trous ping-pong buffers (rgba16f, GENERAL). Compute passes filter
    // history → ping → pong → ping → ...; final composite reads the last
    // pass and modulates by albedo into outImage.
    VkImage        m_atrousPing      = VK_NULL_HANDLE;
    VkImageView    m_atrousPingView  = VK_NULL_HANDLE;
    VkDeviceMemory m_atrousPingMem   = VK_NULL_HANDLE;
    VkImage        m_atrousPong      = VK_NULL_HANDLE;
    VkImageView    m_atrousPongView  = VK_NULL_HANDLE;
    VkDeviceMemory m_atrousPongMem   = VK_NULL_HANDLE;

    // SVGF moments (rgba16f): r=M1, g=M2, b=accumulated sample count.
    // Persistent across frames — raygen samples at reprojected pixel.
    VkImage        m_momentsImage     = VK_NULL_HANDLE;
    VkImageView    m_momentsImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_momentsImageMem  = VK_NULL_HANDLE;

    // Material-ID G-buffer (R32UI). closesthit stamps the hit triangle's
    // material id; raygen compares prev-frame matId at the reprojected
    // pixel against current matId to reject cross-material history.
    VkImage        m_matIdImage      = VK_NULL_HANDLE;
    VkImageView    m_matIdImageView  = VK_NULL_HANDLE;
    VkDeviceMemory m_matIdImageMem   = VK_NULL_HANDLE;

    // RT depth output (D32_SFLOAT, storage + depth-stencil-attachment).
    // raygen writes clip-space depth so a downstream forward pass can
    // depth-test debug primitives against the RT scene.
    VkImage        m_depthImage      = VK_NULL_HANDLE;
    VkImageView    m_depthImageView  = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMem   = VK_NULL_HANDLE;

    // Compute pipelines + descriptor machinery for the denoise chain.
    VkPipelineLayout      m_atrousPipelineLayout    = VK_NULL_HANDLE;
    VkPipeline            m_atrousPipeline          = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_atrousDescSetLayout     = VK_NULL_HANDLE;
    VkDescriptorPool      m_atrousDescPool          = VK_NULL_HANDLE;
    VkDescriptorSet       m_atrousSet_HtoPing       = VK_NULL_HANDLE;
    VkDescriptorSet       m_atrousSet_PingToPong    = VK_NULL_HANDLE;
    VkDescriptorSet       m_atrousSet_PongToPing    = VK_NULL_HANDLE;

    VkPipelineLayout      m_compositePipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_compositePipeline       = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compositeDescSetLayout  = VK_NULL_HANDLE;
    VkDescriptorPool      m_compositeDescPool       = VK_NULL_HANDLE;
    VkDescriptorSet       m_compositeSet            = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_rtPipeline     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet  = VK_NULL_HANDLE;

    VulkanSBT m_sbt;

    VkBuffer       m_cameraUBO       = VK_NULL_HANDLE;
    VkDeviceMemory m_cameraUBOMem    = VK_NULL_HANDLE;
    void*          m_cameraUBOMapped = nullptr;

    VkBuffer       m_matColorsBuf    = VK_NULL_HANDLE;
    VkDeviceMemory m_matColorsMem    = VK_NULL_HANDLE;
    VkBuffer       m_triMatIdsBuf    = VK_NULL_HANDLE;
    VkDeviceMemory m_triMatIdsMem    = VK_NULL_HANDLE;

    VkBuffer       m_uvCoordsBuf     = VK_NULL_HANDLE;
    VkDeviceMemory m_uvCoordsMem     = VK_NULL_HANDLE;
    VkBuffer       m_uvIdxBuf        = VK_NULL_HANDLE;
    VkDeviceMemory m_uvIdxMem        = VK_NULL_HANDLE;
    VkBuffer       m_matTexSlotBuf   = VK_NULL_HANDLE;
    VkDeviceMemory m_matTexSlotMem   = VK_NULL_HANDLE;
    VkBuffer       m_matNormalSlotBuf = VK_NULL_HANDLE;
    VkDeviceMemory m_matNormalSlotMem = VK_NULL_HANDLE;

    VkBuffer       m_lightsBuf       = VK_NULL_HANDLE;
    VkDeviceMemory m_lightsMem       = VK_NULL_HANDLE;

    // ReSTIR reservoir SSBOs: 16 bytes per pixel. Two buffers ping-pong for
    // spatial reuse (so neighbor reads see last frame's data, not partially-
    // written current frame). Shader picks read/write half via frameId & 1.
    VkBuffer       m_reservoirBuf    = VK_NULL_HANDLE;
    VkDeviceMemory m_reservoirMem    = VK_NULL_HANDLE;
    VkBuffer       m_reservoirBuf2   = VK_NULL_HANDLE;
    VkDeviceMemory m_reservoirMem2   = VK_NULL_HANDLE;

    struct LoadedTexture {
        VkImage        image = VK_NULL_HANDLE;
        VkDeviceMemory mem   = VK_NULL_HANDLE;
        VkImageView    view  = VK_NULL_HANDLE;
    };
    std::vector<LoadedTexture> m_textures;
    VkSampler                  m_textureSampler = VK_NULL_HANDLE;

    VkAccelerationStructureKHR m_lastBoundTLAS = VK_NULL_HANDLE;

    // Prev-frame camera cache for raygen reprojection.
    bool  m_havePrevCam     = false;
    float m_prevEye[3]      = {0};
    float m_prevForward[3]  = {0};
    float m_prevRight[3]    = {0};
    float m_prevUp[3]       = {0};
    float m_prevFovTan      = 0.f;
    float m_prevAspect      = 0.f;

    VkCommandPool m_oneShotPool = VK_NULL_HANDLE;

    void           CreateOutputImage(uint32_t width, uint32_t height);
    void           DestroyOutputImage();
    uint32_t       FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props);
    VkDeviceAddress GetBufferDeviceAddress(VkBuffer buffer);
    void           CreateAndAllocateBuffer(VkDeviceSize size,
                                           VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags memProps,
                                           VkBuffer& outBuffer, VkDeviceMemory& outMemory);
    VkDeviceAddress GetASDeviceAddress(VkAccelerationStructureKHR as);

    VkCommandBuffer BeginOneShotCmd();
    void            EndAndSubmitOneShotCmd(VkCommandBuffer cmd);
};

#endif // ENGINE_VULKAN_RENDERER
