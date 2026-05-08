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

    void UpdateCameraVectors(const float eye[3],
                             const float forward[3],
                             const float right[3],
                             const float up[3],
                             float fovTan,
                             float aspect);

    void RecreateOutput(uint32_t width, uint32_t height);

    void TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height);

    // finalLayout = COLOR_ATTACHMENT_OPTIMAL when an overlay pass loads after.
    void BlitToSwapImage(VkCommandBuffer cmd, uint32_t swapW, uint32_t swapH,
                         VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VkImage     GetOutputImage() const { return m_outputImage; }
    VkImageView GetOutputImageView() const { return m_outputImageView; }

private:
    VulkanRenderer* m_renderer = nullptr;
    VkDevice        m_device   = VK_NULL_HANDLE;

    VkImage        m_outputImage     = VK_NULL_HANDLE;
    VkImageView    m_outputImageView = VK_NULL_HANDLE;
    VkDeviceMemory m_outputImageMem  = VK_NULL_HANDLE;
    uint32_t       m_outputWidth     = 0;
    uint32_t       m_outputHeight    = 0;

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

    VkAccelerationStructureKHR m_lastBoundTLAS = VK_NULL_HANDLE;

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
