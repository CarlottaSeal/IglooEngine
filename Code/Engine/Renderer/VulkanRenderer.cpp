#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER

#define VK_USE_PLATFORM_WIN32_KHR
#include "Engine/Renderer/VulkanRenderer.h"

#include "Engine/Renderer/Texture.hpp"
#include "Engine/Renderer/Shader.hpp"
#include "Engine/Renderer/VertexBuffer.hpp"
#include "Engine/Renderer/IndexBuffer.hpp"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Renderer/VulkanMemoryPool.hpp"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Core/Image.hpp"
#include "Engine/Renderer/Camera.hpp"

#include <set>
#include <array>
#include <algorithm>
#include <fstream>
#include <cmath>

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    UNUSED(messageType);
    UNUSED(pUserData);

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        DebuggerPrintf("Vulkan Validation Layer: %s\n", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

VulkanRenderer::VulkanRenderer(RendererConfig config)
    : m_config(config)
{
}

VulkanRenderer::~VulkanRenderer()
{
    ShutDown();
}

void VulkanRenderer::Startup()
{
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();

    // Initialize memory pool after logical device is created
    g_vulkanMemoryPool = new VulkanMemoryPool();
    g_vulkanMemoryPool->Initialize(m_device, m_physicalDevice);

    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateDescriptorSetLayout();
    CreateDepthResources();
    CreateFramebuffers();
    CreateCommandPools();
    CreateDescriptorPool();  // Must be before CreateUniformBuffers
    CreateUniformBuffers();  // Allocates descriptor sets from the pool
    CreateCommandBuffers();
    CreateSyncObjects();

    // Create transfer command pool and fence for texture uploads
    {
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_transferCommandPool) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create transfer command pool!");
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0; // Not signaled initially

        if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_transferFence) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create transfer fence!");
        }
    }

    // Create default resources
    SetDefaultTexture();

    // Update descriptor sets with default textures
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(3);  // Reserve space to prevent reallocation

        // Diffuse texture (binding 1)
        if (m_defaultTexture && m_defaultTexture->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo diffuseImageInfo{};
            diffuseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            diffuseImageInfo.imageView = m_defaultTexture->m_vkImageView;
            diffuseImageInfo.sampler = m_defaultTexture->m_vkSampler;
            imageInfos.push_back(diffuseImageInfo);

            VkWriteDescriptorSet diffuseWrite{};
            diffuseWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            diffuseWrite.dstSet = m_descriptorSets[i];
            diffuseWrite.dstBinding = 1;
            diffuseWrite.dstArrayElement = 0;
            diffuseWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            diffuseWrite.descriptorCount = 1;
            diffuseWrite.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(diffuseWrite);
        }

        // Normal texture (binding 2)
        if (m_defaultNormalTexture && m_defaultNormalTexture->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo normalImageInfo{};
            normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normalImageInfo.imageView = m_defaultNormalTexture->m_vkImageView;
            normalImageInfo.sampler = m_defaultNormalTexture->m_vkSampler;
            imageInfos.push_back(normalImageInfo);

            VkWriteDescriptorSet normalWrite{};
            normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            normalWrite.dstSet = m_descriptorSets[i];
            normalWrite.dstBinding = 2;
            normalWrite.dstArrayElement = 0;
            normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            normalWrite.descriptorCount = 1;
            normalWrite.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(normalWrite);
        }

        // Specular texture (binding 3)
        if (m_defaultSpecTexture && m_defaultSpecTexture->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo specImageInfo{};
            specImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            specImageInfo.imageView = m_defaultSpecTexture->m_vkImageView;
            specImageInfo.sampler = m_defaultSpecTexture->m_vkSampler;
            imageInfos.push_back(specImageInfo);

            VkWriteDescriptorSet specWrite{};
            specWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            specWrite.dstSet = m_descriptorSets[i];
            specWrite.dstBinding = 3;
            specWrite.dstArrayElement = 0;
            specWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            specWrite.descriptorCount = 1;
            specWrite.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(specWrite);
        }

        if (!descriptorWrites.empty())
        {
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(), 0, nullptr);
        }
    }

    CreateAndBindDefaultShader();

    // Create immediate mode ring buffers (matches DX12's 128 MB VERTEX_RING_BUFFER_SIZE).
    // Each DrawVertexArray AppendDataVulkan's into this ring at an advancing offset, so
    // multiple draws in the same frame keep their own data slot until ring wraps.
    constexpr unsigned int kImmediateRingBytes = 128u * 1024u * 1024u;
    m_immediateVBO = CreateVertexBuffer(kImmediateRingBytes, sizeof(Vertex_PCU));
    m_immediateVBO->m_vkIsRingBuffer = true;
    m_immediateVBOForVertex_PCUTBN = CreateVertexBuffer(kImmediateRingBytes, sizeof(Vertex_PCUTBN));
    m_immediateVBOForVertex_PCUTBN->m_vkIsRingBuffer = true;
    m_immediateIBO = CreateIndexBuffer(sizeof(unsigned int) * 30000, sizeof(unsigned int));

    // Create constant buffers
    m_cameraCBO = CreateConstantBuffer(sizeof(Mat44) * 2);
    m_modelCBO = CreateConstantBuffer(256);
    m_generalLightCBO = CreateConstantBuffer(1024);
    m_shadowCBO = CreateConstantBuffer(sizeof(Mat44));
    m_perFrameCBO = CreateConstantBuffer(256);
}

void VulkanRenderer::ShutDown()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
    }

    // Print memory pool stats before shutdown
    if (g_vulkanMemoryPool)
    {
        g_vulkanMemoryPool->PrintStats();
    }

    // Delete fonts
    for (BitmapFont* font : m_loadedFonts)
    {
        delete font;
    }
    m_loadedFonts.clear();

    // Delete textures and their Vulkan resources
    for (Texture* texture : m_loadedTextures)
    {
        if (texture)
        {
            if (texture->m_vkSampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_device, texture->m_vkSampler, nullptr);
            }
            if (texture->m_vkImageView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device, texture->m_vkImageView, nullptr);
            }
            if (texture->m_vkImage != VK_NULL_HANDLE)
            {
                vkDestroyImage(m_device, texture->m_vkImage, nullptr);
            }
            if (texture->m_vkImageMemory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, texture->m_vkImageMemory, nullptr);
            }
            delete texture;
        }
    }
    m_loadedTextures.clear();

    // Delete shaders and their Vulkan resources
    for (Shader* shader : m_loadedShaders)
    {
        if (shader)
        {
            if (shader->m_vkPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_device, shader->m_vkPipeline, nullptr);
            }
            if (shader->m_vkVertexShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device, shader->m_vkVertexShaderModule, nullptr);
            }
            if (shader->m_vkFragmentShaderModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device, shader->m_vkFragmentShaderModule, nullptr);
            }
            delete shader;
        }
    }
    m_loadedShaders.clear();

    // Delete buffers before memory pool shutdown
    delete m_immediateVBO;
    m_immediateVBO = nullptr;
    delete m_immediateVBOForVertex_PCUTBN;
    m_immediateVBOForVertex_PCUTBN = nullptr;
    delete m_immediateIBO;
    m_immediateIBO = nullptr;
    delete m_cameraCBO;
    m_cameraCBO = nullptr;
    delete m_modelCBO;
    m_modelCBO = nullptr;
    delete m_generalLightCBO;
    m_generalLightCBO = nullptr;
    delete m_shadowCBO;
    m_shadowCBO = nullptr;
    delete m_perFrameCBO;
    m_perFrameCBO = nullptr;

    CleanupSwapChain();

    for (size_t i = 0; i < m_frameData.size(); i++)
    {
        vkDestroySemaphore(m_device, m_frameData[i].renderFinishedSemaphore, nullptr);
        vkDestroySemaphore(m_device, m_frameData[i].imageAvailableSemaphore, nullptr);
        vkDestroyFence(m_device, m_frameData[i].inFlightFence, nullptr);
        vkDestroyCommandPool(m_device, m_frameData[i].commandPool, nullptr);
    }

    // Cleanup per-swapchain-image semaphores
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++)
    {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();

    // Destroy transfer command pool and fence
    if (m_transferFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(m_device, m_transferFence, nullptr);
        m_transferFence = VK_NULL_HANDLE;
    }
    if (m_transferCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_device, m_transferCommandPool, nullptr);
        m_transferCommandPool = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < m_uniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
    }

    // Cleanup camera uniform buffers
    for (size_t i = 0; i < m_cameraUniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_cameraUniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_cameraUniformBuffersMemory[i], nullptr);
    }

    // Cleanup model uniform buffers
    for (size_t i = 0; i < m_modelUniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_modelUniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_modelUniformBuffersMemory[i], nullptr);
    }

    // Cleanup light uniform buffers
    for (size_t i = 0; i < m_lightUniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_lightUniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_lightUniformBuffersMemory[i], nullptr);
    }

    // Cleanup shadow uniform buffers
    for (size_t i = 0; i < m_shadowUniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_shadowUniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_shadowUniformBuffersMemory[i], nullptr);
    }

    // Cleanup perframe uniform buffers
    for (size_t i = 0; i < m_perFrameUniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_perFrameUniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_perFrameUniformBuffersMemory[i], nullptr);
    }

    // Cleanup samplers
    if (m_samplerPointClamp != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_samplerPointClamp, nullptr);
        m_samplerPointClamp = VK_NULL_HANDLE;
    }
    if (m_samplerPointWrap != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_samplerPointWrap, nullptr);
        m_samplerPointWrap = VK_NULL_HANDLE;
    }
    if (m_samplerBilinearClamp != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_samplerBilinearClamp, nullptr);
        m_samplerBilinearClamp = VK_NULL_HANDLE;
    }
    if (m_samplerBilinearWrap != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_device, m_samplerBilinearWrap, nullptr);
        m_samplerBilinearWrap = VK_NULL_HANDLE;
    }
    m_defaultSampler = VK_NULL_HANDLE; // Points to one of the above

    // Cleanup graphics pipelines
    if (m_graphicsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelinePCUTBN != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_graphicsPipelinePCUTBN, nullptr);
        m_graphicsPipelinePCUTBN = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

    if (m_depthImage != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        vkDestroyImage(m_device, m_depthImage, nullptr);
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
    }

    vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    // Shutdown memory pool before device destruction
    if (g_vulkanMemoryPool)
    {
        g_vulkanMemoryPool->Shutdown();
        delete g_vulkanMemoryPool;
        g_vulkanMemoryPool = nullptr;
    }

    vkDestroyDevice(m_device, nullptr);

    if (m_enableValidationLayers)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(m_instance, m_debugMessenger, nullptr);
        }
    }

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

//==============================================================================
// Frame Functions
//==============================================================================
void VulkanRenderer::BeginFrame()
{
    // Reset render pass state at the start of each frame
    m_isRenderPassActive = false;

    // Reset UBO ring buffer offsets and re-seed slot 0 of the model UBO every frame
    // with identity + white. Otherwise any draw before a SetModelConstants in this frame
    // (e.g. attract-mode ring) inherits the previous frame's leftover model (color tint, transform).
    m_cameraRingOffset           = 0;
    m_currentCameraDynamicOffset = 0;
    m_modelRingOffset            = 0;
    m_currentModelDynamicOffset  = 0;

    if (!m_modelUniformBuffersMapped.empty() && m_modelUniformBuffersMapped[m_currentFrame])
    {
        ModelConstants identityModel;
        identityModel.ModelToWorldTransform = Mat44();
        identityModel.ModelColor[0] = 1.0f;
        identityModel.ModelColor[1] = 1.0f;
        identityModel.ModelColor[2] = 1.0f;
        identityModel.ModelColor[3] = 1.0f;
        memcpy(m_modelUniformBuffersMapped[m_currentFrame], &identityModel, sizeof(ModelConstants));

        // Reserve slot 0 for the identity seed; SetModelConstants writes to slot 1 onwards.
        const uint32_t alignedSlotSize =
            (uint32_t)(((sizeof(ModelConstants) + m_minUboAlignment - 1) / m_minUboAlignment) * m_minUboAlignment);
        m_modelRingOffset = alignedSlotSize;
        m_currentModelDynamicOffset = 0; // any draw before SetModelConstants reads the identity slot
    }

    // Reset bound textures to defaults for new frame.
    // With bindless we don't redo descriptor writes per-frame: the atlas is populated once at
    // texture creation. Just reset the per-draw push-constant index back to 0 (default white).
    for (int i = 0; i < MAX_TEXTURE_SLOTS; ++i)
    {
        m_boundTextures[i] = nullptr;
    }
    m_currentMaterial = {};
    m_textureBindingDirty = false;

    vkWaitForFences(m_device, 1, &m_frameData[m_currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

    // Use per-image semaphores to avoid reuse issues
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_semaphoreIndex], VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        ERROR_AND_DIE("Failed to acquire swap chain image!");
    }

    vkResetFences(m_device, 1, &m_frameData[m_currentFrame].inFlightFence);

    // Notify ring buffers that a new frame is starting
    // This allows them to track per-frame usage for safe data management
    if (m_immediateVBO)
    {
        m_immediateVBO->BeginFrameVulkan(static_cast<int>(m_currentFrame));
    }
    if (m_immediateVBOForVertex_PCUTBN)
    {
        m_immediateVBOForVertex_PCUTBN->BeginFrameVulkan(static_cast<int>(m_currentFrame));
    }
    if (m_immediateIBO)
    {
        m_immediateIBO->BeginFrameVulkan(static_cast<int>(m_currentFrame));
    }

    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to begin recording command buffer!");
    }
}

void VulkanRenderer::EndFrame()
{
    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

    // End render pass if still active before ending command buffer
    if (m_isRenderPassActive)
    {
        vkCmdEndRenderPass(cmd);
        m_isRenderPassActive = false;
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to record command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Use per-image semaphores
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_semaphoreIndex] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_semaphoreIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_frameData[m_currentFrame].inFlightFence) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { m_swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        RecreateSwapChain();
    }
    else if (result != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to present swap chain image!");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_semaphoreIndex = (m_semaphoreIndex + 1) % static_cast<uint32_t>(m_imageAvailableSemaphores.size());
}

//==============================================================================
// Screen and Camera
//==============================================================================
void VulkanRenderer::ClearScreen(const Rgba8& clearColor)
{
    // End any existing render pass first
    if (m_isRenderPassActive)
    {
        VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;
        vkCmdEndRenderPass(cmd);
        m_isRenderPassActive = false;
    }

    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { {clearColor.r / 255.0f, clearColor.g / 255.0f, clearColor.b / 255.0f, clearColor.a / 255.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_isRenderPassActive = true;
}

void VulkanRenderer::ClearScreen()
{
    ClearScreen(Rgba8::MAGENTA);
}

void VulkanRenderer::BeginCamera(const Camera& camera)
{
    // Update camera uniform buffer
    CameraConstants cameraConstants;
    cameraConstants.WorldToCameraTransform = camera.GetWorldToCameraTransform();
    cameraConstants.CameraToRenderTransform = camera.GetCameraToRenderTransform();
    cameraConstants.RenderToClipTransform = camera.GetRenderToClipTransform();
    cameraConstants.CameraWorldPosition = camera.GetPosition();
    cameraConstants.padding = 0.0f;


    // Write into the camera ring buffer at an aligned offset, then advance.
    // The dynamic offset captured here is what subsequent draws bind via vkCmdBindDescriptorSets.
    const uint32_t alignedSlotSize =
        (uint32_t)(((sizeof(CameraConstants) + m_minUboAlignment - 1) / m_minUboAlignment) * m_minUboAlignment);

    if (m_cameraRingOffset + alignedSlotSize > kCameraRingBufferSize)
    {
        DebuggerPrintf("BeginCamera: camera ring buffer overflow, wrapping (offset=%u size=%u)\n",
            m_cameraRingOffset, alignedSlotSize);
        m_cameraRingOffset = 0; // wrap; tolerable for debug, in production we'd grow the buffer
    }

    uint8_t* dst = (uint8_t*)m_cameraUniformBuffersMapped[m_currentFrame] + m_cameraRingOffset;
    memcpy(dst, &cameraConstants, sizeof(CameraConstants));
    m_currentCameraDynamicOffset = m_cameraRingOffset;
    m_cameraRingOffset += alignedSlotSize;

    // If no render pass is active, start one (for DebugRender calls)
    if (!m_isRenderPassActive)
    {
        VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.framebuffer = m_swapChainFramebuffers[m_imageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = m_swapChainExtent;

        // Don't clear - just continue rendering
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
        clearValues[1].depthStencil = { 1.0f, 0 };

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_isRenderPassActive = true;
    }
}

void VulkanRenderer::EndCamera(const Camera& camera)
{
    UNUSED(camera);
    // Intentionally do NOT end the render pass here.
    // Render pass uses LOAD_OP_CLEAR, so each vkCmdBeginRenderPass would wipe the
    // framebuffer. Subsequent BeginCamera calls (DebugRenderWorld, DebugRenderScreen,
    // DevConsole etc.) inside the same frame should accumulate into the same pass.
    // EndFrame ends the render pass before queue submit.
}

void VulkanRenderer::SetViewport(const AABB2& normalizedViewport)
{
    // Viewport is set dynamically during draw calls
    // Store the normalized viewport for use during draw
    // Note: The actual viewport setting happens in DrawVertexBuffer/DrawIndexBuffer
    // where we set dynamic viewport based on swap chain extent
    UNUSED(normalizedViewport);
    // TODO: Implement custom viewport support if needed
}

//==============================================================================
// Drawing Functions (Stubs)
//==============================================================================
void VulkanRenderer::DrawVertexArray(const std::vector<Vertex_PCU>& verts)
{
    DrawVertexArray((int)verts.size(), verts.data());
}

void VulkanRenderer::DrawVertexArray(int numVerts, const Vertex_PCU* verts)
{
    if (numVerts == 0 || verts == nullptr)
    {
        return;
    }

    unsigned int vertexBufferSize = numVerts * sizeof(Vertex_PCU);
    unsigned int byteOffset = m_immediateVBO->AppendDataVulkan(verts, vertexBufferSize);
    DrawVertexBuffer(m_immediateVBO, numVerts, byteOffset);
}

void VulkanRenderer::DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts)
{
    DrawVertexArray((int)verts.size(), verts.data());
}

void VulkanRenderer::DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts)
{
    if (numVerts == 0 || verts == nullptr || !m_isRenderPassActive)
    {
        return;
    }

    if (m_graphicsPipelinePCUTBN == VK_NULL_HANDLE)
    {
        return;
    }

    // Append vertex data to PCUTBN ring buffer; capture byte offset for the draw.
    unsigned int pcutbnByteOffset =
        m_immediateVBOForVertex_PCUTBN->AppendDataVulkan(verts, numVerts * sizeof(Vertex_PCUTBN));

    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

    // Bind PCUTBN pipeline (or the deferred override if active)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineOverridePCUTBN != VK_NULL_HANDLE ? m_pipelineOverridePCUTBN : m_graphicsPipelinePCUTBN);

    // Update texture descriptor sets if dirty
    if (m_textureBindingDirty && !m_descriptorSets.empty())
    {
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(3);

        const Texture* diffuseTex = m_boundTextures[0] ? m_boundTextures[0] : m_defaultTexture;
        if (diffuseTex && diffuseTex->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = diffuseTex->m_vkImageView;
            imageInfo.sampler = diffuseTex->m_vkSampler ? diffuseTex->m_vkSampler : m_defaultSampler;
            imageInfos.push_back(imageInfo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptorSets[m_currentFrame];
            write.dstBinding = 1;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(write);
        }

        const Texture* normalTex = m_boundTextures[1] ? m_boundTextures[1] : m_defaultNormalTexture;
        if (normalTex && normalTex->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = normalTex->m_vkImageView;
            imageInfo.sampler = normalTex->m_vkSampler ? normalTex->m_vkSampler : m_defaultSampler;
            imageInfos.push_back(imageInfo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptorSets[m_currentFrame];
            write.dstBinding = 2;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(write);
        }

        const Texture* specTex = m_boundTextures[2] ? m_boundTextures[2] : m_defaultSpecTexture;
        if (specTex && specTex->m_vkImageView != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = specTex->m_vkImageView;
            imageInfo.sampler = specTex->m_vkSampler ? specTex->m_vkSampler : m_defaultSampler;
            imageInfos.push_back(imageInfo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_descriptorSets[m_currentFrame];
            write.dstBinding = 3;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();
            descriptorWrites.push_back(write);
        }

        if (!descriptorWrites.empty())
        {
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                descriptorWrites.data(), 0, nullptr);
        }
        m_textureBindingDirty = false;
    }

    // Bind descriptor set with current camera+model dynamic offsets (binding 0, then binding 4).
    if (!m_descriptorSets.empty())
    {
        const uint32_t dynOffsets[2] = { m_currentCameraDynamicOffset, m_currentModelDynamicOffset };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout,
            0, 1, &m_descriptorSets[m_currentFrame], 2, dynOffsets);
    }

    vkCmdPushConstants(cmd, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(VkMaterialPC), &m_currentMaterial);

    // Set dynamic viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(m_swapChainExtent.height);
    viewport.width = static_cast<float>(m_swapChainExtent.width);
    viewport.height = -static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapChainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind vertex buffer at the per-draw ring offset.
    VkBuffer vertexBuffers[] = { m_immediateVBOForVertex_PCUTBN->m_vkBuffer };
    VkDeviceSize offsets[]   = { (VkDeviceSize)pcutbnByteOffset };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    // Draw
    vkCmdDraw(cmd, numVerts, 1, 0, 0);
}

void VulkanRenderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices)
{
    DrawVertexIndexArray((int)verts.size(), verts.data(), (int)indices.size(), indices.data());
}

void VulkanRenderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices,
    VertexBuffer* vbo, IndexBuffer* ibo)
{
    DrawVertexIndexArray((int)verts.size(), verts.data(), (int)indices.size(), indices.data(), vbo, ibo);
}

void VulkanRenderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices)
{
    if (numVerts <= 0 || !verts || numIndices <= 0 || !indices || !m_isRenderPassActive)
    {
        return;
    }

    // Copy vertex data to immediate VBO
    CopyCPUToGPU(verts, numVerts * sizeof(Vertex_PCUTBN), m_immediateVBOForVertex_PCUTBN);

    // Copy index data to immediate IBO
    CopyCPUToGPU(indices, numIndices * sizeof(unsigned int), m_immediateIBO);

    // Draw using index buffer
    DrawIndexBuffer(m_immediateVBOForVertex_PCUTBN, m_immediateIBO, numIndices, PRIMITIVE_TRIANGLES);
}

void VulkanRenderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices,
    VertexBuffer* vbo, IndexBuffer* ibo)
{
    if (numVerts <= 0 || !verts || numIndices <= 0 || !indices || !m_isRenderPassActive)
    {
        return;
    }

    // Use provided buffers or fall back to immediate buffers
    VertexBuffer* vertexBuffer = vbo ? vbo : m_immediateVBOForVertex_PCUTBN;
    IndexBuffer* indexBuffer = ibo ? ibo : m_immediateIBO;

    // Copy vertex data
    CopyCPUToGPU(verts, numVerts * sizeof(Vertex_PCUTBN), vertexBuffer);

    // Copy index data
    CopyCPUToGPU(indices, numIndices * sizeof(unsigned int), indexBuffer);

    // Draw using index buffer
    DrawIndexBuffer(vertexBuffer, indexBuffer, numIndices, PRIMITIVE_TRIANGLES);
}

//==============================================================================
// Resource Creation (Stubs)
//==============================================================================
Image* VulkanRenderer::CreateImageFromFile(char const* imageFilePath)
{
    Image* newImage = new Image(imageFilePath);
    return newImage;
}

Texture* VulkanRenderer::CreateTextureFromImage(const Image& image, bool usingMipmaps)
{
    IntVec2 dimensions = image.GetDimensions();
    VkDeviceSize imageSize = dimensions.x * dimensions.y * 4; // RGBA = 4 bytes per pixel

    // Calculate mip levels
    uint32_t mipLevels = 1;
    if (usingMipmaps)
    {
        int maxDim = (dimensions.x > dimensions.y) ? dimensions.x : dimensions.y;
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim)))) + 1;
    }

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    // Copy image data to staging buffer
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, image.GetRawData(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create Vulkan image
    Texture* newTexture = new Texture();
    newTexture->m_name = image.GetImageFilePath();
    newTexture->m_dimensions = dimensions;

    // Create image with mip levels and transfer src usage for mipmap generation
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usingMipmaps)
    {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = dimensions.x;
    imageInfo.extent.height = dimensions.y;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &newTexture->m_vkImage) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, newTexture->m_vkImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &newTexture->m_vkImageMemory) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to allocate image memory!");
    }

    vkBindImageMemory(m_device, newTexture->m_vkImage, newTexture->m_vkImageMemory, 0);

    // Transition image layout and copy data
    TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, newTexture->m_vkImage, static_cast<uint32_t>(dimensions.x), static_cast<uint32_t>(dimensions.y));

    // Generate mipmaps (this also transitions to SHADER_READ_ONLY_OPTIMAL)
    if (usingMipmaps && mipLevels > 1)
    {
        // Check if format supports linear blitting
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);

        if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            // Fall back to non-mipmapped
            TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        else
        {
            // Generate mipmaps
            VkCommandBuffer commandBuffer;
            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandPool = m_transferCommandPool;
            cmdAllocInfo.commandBufferCount = 1;

            vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = newTexture->m_vkImage;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.subresourceRange.levelCount = 1;

            int32_t mipWidth = dimensions.x;
            int32_t mipHeight = dimensions.y;

            for (uint32_t i = 1; i < mipLevels; i++)
            {
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);

                VkImageBlit blit{};
                blit.srcOffsets[0] = { 0, 0, 0 };
                blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = 1;
                blit.dstOffsets[0] = { 0, 0, 0 };
                blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 1;

                vkCmdBlitImage(commandBuffer,
                    newTexture->m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    newTexture->m_vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit,
                    VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(commandBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier);

                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }

            // Transition last mip level
            barrier.subresourceRange.baseMipLevel = mipLevels - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_transferFence);
            vkWaitForFences(m_device, 1, &m_transferFence, VK_TRUE, UINT64_MAX);
            vkResetFences(m_device, 1, &m_transferFence);

            vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
        }
    }
    else
    {
        TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Cleanup staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Create image view with mip levels
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = newTexture->m_vkImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &newTexture->m_vkImageView) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create texture image view!");
    }

    // Create sampler with mipmap support
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &newTexture->m_vkSampler) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create texture sampler!");
    }

    m_loadedTextures.push_back(newTexture);
    RegisterTextureBindless(newTexture);
    return newTexture;
}

Texture* VulkanRenderer::CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
    // Check if texture already exists
    Texture* existingTexture = GetTextureForFileName(imageFilePath);
    if (existingTexture)
    {
        return existingTexture;
    }

    // Create new texture using dedicated transfer command pool
    Texture* newTexture = CreateTextureFromFile(imageFilePath, usingMipmaps);
    return newTexture;
}

Texture* VulkanRenderer::CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel,
    uint8_t* texelData, bool usingMipmaps)
{
    UNUSED(usingMipmaps); // TODO: Implement mipmap generation

    if (!texelData || dimensions.x <= 0 || dimensions.y <= 0)
    {
        return nullptr;
    }

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    if (bytesPerTexel == 1)
    {
        format = VK_FORMAT_R8_UNORM;
    }
    else if (bytesPerTexel == 3)
    {
        // Convert RGB to RGBA since Vulkan doesn't support RGB8 well
        format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    VkDeviceSize imageSize = dimensions.x * dimensions.y * 4; // Always use RGBA

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    // Copy data to staging buffer (converting if necessary)
    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, imageSize, 0, &data);

    if (bytesPerTexel == 4)
    {
        memcpy(data, texelData, static_cast<size_t>(imageSize));
    }
    else if (bytesPerTexel == 3)
    {
        // Convert RGB to RGBA
        uint8_t* dst = static_cast<uint8_t*>(data);
        for (int i = 0; i < dimensions.x * dimensions.y; ++i)
        {
            dst[i * 4 + 0] = texelData[i * 3 + 0];
            dst[i * 4 + 1] = texelData[i * 3 + 1];
            dst[i * 4 + 2] = texelData[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    }
    else if (bytesPerTexel == 1)
    {
        // Convert grayscale to RGBA
        uint8_t* dst = static_cast<uint8_t*>(data);
        for (int i = 0; i < dimensions.x * dimensions.y; ++i)
        {
            dst[i * 4 + 0] = texelData[i];
            dst[i * 4 + 1] = texelData[i];
            dst[i * 4 + 2] = texelData[i];
            dst[i * 4 + 3] = 255;
        }
    }

    vkUnmapMemory(m_device, stagingBufferMemory);

    // Create texture
    Texture* newTexture = new Texture();
    newTexture->m_name = name ? name : "FromData";
    newTexture->m_dimensions = dimensions;

    // Create Vulkan image
    CreateImage(dimensions.x, dimensions.y, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newTexture->m_vkImage, newTexture->m_vkImageMemory);

    // Transition image layout and copy data
    TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, newTexture->m_vkImage, dimensions.x, dimensions.y);
    TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging buffer
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    // Create image view
    newTexture->m_vkImageView = CreateImageView(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &newTexture->m_vkSampler) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create texture sampler!");
    }

    m_loadedTextures.push_back(newTexture);
    RegisterTextureBindless(newTexture);
    return newTexture;
}

Texture* VulkanRenderer::CreateTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
    Image* image = CreateImageFromFile(imageFilePath);
    if (!image)
    {
        return nullptr;
    }

    Texture* newTexture = CreateTextureFromImage(*image, usingMipmaps);
    delete image;
    return newTexture;
}

Texture* VulkanRenderer::GetTextureForFileName(const char* imageFilePath)
{
    for (Texture* texture : m_loadedTextures)
    {
        if (texture && texture->GetImageFilePath() == imageFilePath)
        {
            return texture;
        }
    }
    return nullptr;
}

void VulkanRenderer::BindTexture(const Texture* texture, int slot)
{
    if (slot < 0 || slot >= MAX_TEXTURE_SLOTS) return;

    const Texture* textureToBind = texture ? texture : m_defaultTexture;
    m_boundTextures[slot] = textureToBind;

    int idx = (textureToBind && textureToBind->m_vkDescriptorIndex >= 0)
            ? textureToBind->m_vkDescriptorIndex : 0;
    if (slot == 0)      m_currentMaterial.DiffuseId  = idx;
    else if (slot == 1) m_currentMaterial.NormalId   = idx;
    else if (slot == 2) m_currentMaterial.SpecularId = idx;
}

void VulkanRenderer::SetMaterialConstants(const Texture* diffuseTex, const Texture* normalTex, const Texture* specTex)
{
    m_currentMaterial.DiffuseId  = (diffuseTex && diffuseTex->m_vkDescriptorIndex >= 0) ? diffuseTex->m_vkDescriptorIndex : 0;
    m_currentMaterial.NormalId   = (normalTex  && normalTex->m_vkDescriptorIndex  >= 0) ? normalTex->m_vkDescriptorIndex  : 1;
    m_currentMaterial.SpecularId = (specTex    && specTex->m_vkDescriptorIndex    >= 0) ? specTex->m_vkDescriptorIndex    : 2;
}

void VulkanRenderer::RegisterTextureBindless(Texture* tex)
{
    if (!tex || tex->m_vkImageView == VK_NULL_HANDLE) return;
    if (tex->m_vkDescriptorIndex >= 0) return; // already registered

    if (m_nextBindlessIndex >= kMaxBindlessTextures)
    {
        DebuggerPrintf("RegisterTextureBindless: atlas full (%u slots), texture '%s' will use slot 0 fallback\n",
            kMaxBindlessTextures, tex->m_name.c_str());
        tex->m_vkDescriptorIndex = 0;
        return;
    }

    int idx = (int)m_nextBindlessIndex++;
    tex->m_vkDescriptorIndex = idx;
    DebuggerPrintf("Bindless register #%d  '%s'  view=0x%p\n",
        idx, tex->m_name.c_str(), (void*)tex->m_vkImageView);

    if (m_descriptorSets.empty()) return;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView   = tex->m_vkImageView;
    imgInfo.sampler     = tex->m_vkSampler ? tex->m_vkSampler : m_defaultSampler;

    // Validation requires every slot the shader could statically access to be valid.
    // The shader declares sampler2D g_textures[256] and indexes via a push constant —
    // validation can't trace which slot will be picked, so it wants ALL 256 valid.
    // Solution: when the very first texture is registered (idx 0 = default white),
    // also fill every other slot with that default. Later RegisterTextureBindless calls
    // overwrite their assigned slot only.
    if (idx == 0)
    {
        std::vector<VkDescriptorImageInfo> all(kMaxBindlessTextures, imgInfo);
        for (size_t f = 0; f < m_descriptorSets.size(); ++f)
        {
            VkWriteDescriptorSet write{};
            write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet           = m_descriptorSets[f];
            write.dstBinding       = 1;
            write.dstArrayElement  = 0;
            write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount  = kMaxBindlessTextures;
            write.pImageInfo       = all.data();
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }
        return;
    }

    for (size_t f = 0; f < m_descriptorSets.size(); ++f)
    {
        VkWriteDescriptorSet write{};
        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet           = m_descriptorSets[f];
        write.dstBinding       = 1;
        write.dstArrayElement  = (uint32_t)idx;
        write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount  = 1;
        write.pImageInfo       = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

BitmapFont* VulkanRenderer::CreateOrGetBitmapFont(const char* bitmapFontFilePathWithNoExtension)
{
    // Check if font already exists
    for (BitmapFont* font : m_loadedFonts)
    {
        if (font && font->m_fontFilePathNameWithNoExtension == bitmapFontFilePathWithNoExtension)
        {
            return font;
        }
    }

    // Create texture for font
    std::string fontTextureFilePath = std::string(bitmapFontFilePathWithNoExtension) + ".png";
    Texture* bitmapTexture = CreateOrGetTextureFromFile(fontTextureFilePath.c_str());
    if (!bitmapTexture)
    {
        return nullptr;
    }

    // Create new font
    BitmapFont* bitmapFont = new BitmapFont(bitmapFontFilePathWithNoExtension, *bitmapTexture);
    m_loadedFonts.push_back(bitmapFont);

    return bitmapFont;
}

Shader* VulkanRenderer::CreateShader(char const* shaderName, char const* shaderSource, VertexType vertexType)
{
    UNUSED(shaderSource); // Vulkan uses SPIR-V files, not source strings
    return CreateShader(shaderName, vertexType);
}

Shader* VulkanRenderer::CreateShader(char const* shaderName, VertexType vertexType)
{
    if (!shaderName)
    {
        return nullptr;
    }

    // Helper lambda to read SPIR-V file
    auto readSpirVFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            return {};
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    };

    // Build shader file paths (look for pre-compiled SPIR-V)
    std::string basePath = "Data/Shaders/Vulkan/";
    std::string vertPath = basePath + std::string(shaderName) + ".vert.spv";
    std::string fragPath = basePath + std::string(shaderName) + ".frag.spv";

    std::vector<char> vertShaderCode = readSpirVFile(vertPath);
    std::vector<char> fragShaderCode = readSpirVFile(fragPath);

    if (vertShaderCode.empty() || fragShaderCode.empty())
    {
        DebuggerPrintf("Warning: Vulkan shaders not found for '%s'. Using default shader.\n", shaderName);
        return m_defaultShader;
    }

    // Create shader config
    ShaderConfig config;
    config.m_name = shaderName;

    Shader* newShader = new Shader(config);

    // Create vertex shader module
    VkShaderModuleCreateInfo vertCreateInfo{};
    vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertCreateInfo.codeSize = vertShaderCode.size();
    vertCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());

    if (vkCreateShaderModule(m_device, &vertCreateInfo, nullptr, &newShader->m_vkVertexShaderModule) != VK_SUCCESS)
    {
        delete newShader;
        return nullptr;
    }

    // Create fragment shader module
    VkShaderModuleCreateInfo fragCreateInfo{};
    fragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragCreateInfo.codeSize = fragShaderCode.size();
    fragCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());

    if (vkCreateShaderModule(m_device, &fragCreateInfo, nullptr, &newShader->m_vkFragmentShaderModule) != VK_SUCCESS)
    {
        vkDestroyShaderModule(m_device, newShader->m_vkVertexShaderModule, nullptr);
        delete newShader;
        return nullptr;
    }

    // Create graphics pipeline for this shader
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = newShader->m_vkVertexShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = newShader->m_vkFragmentShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex input based on vertex type
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

    if (vertexType == VertexType::VERTEX_PCUTBN)
    {
        bindingDescription.stride = sizeof(Vertex_PCUTBN);

        VkVertexInputAttributeDescription posAttr{};
        posAttr.binding = 0;
        posAttr.location = 0;
        posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        posAttr.offset = offsetof(Vertex_PCUTBN, m_position);
        attributeDescriptions.push_back(posAttr);

        VkVertexInputAttributeDescription colorAttr{};
        colorAttr.binding = 0;
        colorAttr.location = 1;
        colorAttr.format = VK_FORMAT_R8G8B8A8_UNORM;
        colorAttr.offset = offsetof(Vertex_PCUTBN, m_color);
        attributeDescriptions.push_back(colorAttr);

        VkVertexInputAttributeDescription texAttr{};
        texAttr.binding = 0;
        texAttr.location = 2;
        texAttr.format = VK_FORMAT_R32G32_SFLOAT;
        texAttr.offset = offsetof(Vertex_PCUTBN, m_uvTexCoords);
        attributeDescriptions.push_back(texAttr);
    }
    else // VERTEX_PCU
    {
        bindingDescription.stride = sizeof(Vertex_PCU);

        VkVertexInputAttributeDescription posAttr{};
        posAttr.binding = 0;
        posAttr.location = 0;
        posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
        posAttr.offset = offsetof(Vertex_PCU, m_position);
        attributeDescriptions.push_back(posAttr);

        VkVertexInputAttributeDescription colorAttr{};
        colorAttr.binding = 0;
        colorAttr.location = 1;
        colorAttr.format = VK_FORMAT_R8G8B8A8_UNORM;
        colorAttr.offset = offsetof(Vertex_PCU, m_color);
        attributeDescriptions.push_back(colorAttr);

        VkVertexInputAttributeDescription texAttr{};
        texAttr.binding = 0;
        texAttr.location = 2;
        texAttr.format = VK_FORMAT_R32G32_SFLOAT;
        texAttr.offset = offsetof(Vertex_PCU, m_uvTextColors);
        attributeDescriptions.push_back(texAttr);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Use the same pipeline configuration as the default pipeline
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Use existing pipeline layout
    newShader->m_vkPipelineLayout = m_pipelineLayout;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newShader->m_vkPipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(m_device, newShader->m_vkVertexShaderModule, nullptr);
        vkDestroyShaderModule(m_device, newShader->m_vkFragmentShaderModule, nullptr);
        delete newShader;
        return nullptr;
    }

    m_loadedShaders.push_back(newShader);
    return newShader;
}

Shader* VulkanRenderer::CreateOrGetShader(char const* shaderName, VertexType vertexType)
{
    if (!shaderName)
    {
        return nullptr;
    }

    // Check if shader already exists
    for (Shader* shader : m_loadedShaders)
    {
        if (shader && shader->GetName() == shaderName)
        {
            return shader;
        }
    }

    // Create new shader
    return CreateShader(shaderName, vertexType);
}

void VulkanRenderer::BindShader(Shader* shader)
{
    m_currentShader = shader;
    // Note: The actual pipeline binding happens during draw calls
}

//==============================================================================
// Buffer Management
//==============================================================================
VertexBuffer* VulkanRenderer::CreateVertexBuffer(const unsigned int size, unsigned int stride)
{
    return new VertexBuffer(m_device, m_physicalDevice, size, stride);
}

void VulkanRenderer::BindVertexBuffer(VertexBuffer* vbo)
{
    if (!vbo || vbo->m_vkBuffer == VK_NULL_HANDLE)
    {
        return;
    }

    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;
    VkBuffer vertexBuffers[] = { vbo->m_vkBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
}

void VulkanRenderer::DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount, unsigned int byteOffset)
{
    if (!vbo || vertexCount == 0 || !m_isRenderPassActive)
    {
        if (!m_isRenderPassActive)
        {
            DebuggerPrintf("DrawVertexBuffer: Render pass not active!\n");
        }
        return;
    }

    // Bind the default pipeline if available
    if (m_graphicsPipeline != VK_NULL_HANDLE)
    {
        VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

        // Bind pipeline (or deferred PCU override)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineOverridePCU != VK_NULL_HANDLE ? m_pipelineOverridePCU : m_graphicsPipeline);

        // Update texture descriptor sets if dirty
        if (m_textureBindingDirty && !m_descriptorSets.empty())
        {
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            std::vector<VkDescriptorImageInfo> imageInfos;
            imageInfos.reserve(3);

            // Diffuse texture (binding 1, slot 0)
            const Texture* diffuseTex = m_boundTextures[0] ? m_boundTextures[0] : m_defaultTexture;
            if (diffuseTex && diffuseTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = diffuseTex->m_vkImageView;
                imageInfo.sampler = diffuseTex->m_vkSampler ? diffuseTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 1;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            // Normal texture (binding 2, slot 1)
            const Texture* normalTex = m_boundTextures[1] ? m_boundTextures[1] : m_defaultNormalTexture;
            if (normalTex && normalTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = normalTex->m_vkImageView;
                imageInfo.sampler = normalTex->m_vkSampler ? normalTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 2;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            // Specular texture (binding 3, slot 2)
            const Texture* specTex = m_boundTextures[2] ? m_boundTextures[2] : m_defaultSpecTexture;
            if (specTex && specTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = specTex->m_vkImageView;
                imageInfo.sampler = specTex->m_vkSampler ? specTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 3;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            if (!descriptorWrites.empty())
            {
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0, nullptr);
            }
            m_textureBindingDirty = false;
        }

        // Bind descriptor set with current camera+model dynamic offsets (binding 0, then binding 4).
        if (!m_descriptorSets.empty())
        {
            const uint32_t dynOffsets[2] = { m_currentCameraDynamicOffset, m_currentModelDynamicOffset };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout,
                0, 1, &m_descriptorSets[m_currentFrame], 2, dynOffsets);
        }

        // Push diffuse atlas index for bindless texture sampling.
        {
            vkCmdPushConstants(cmd, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(VkMaterialPC), &m_currentMaterial);
        }

        // Set dynamic viewport and scissor
        // Vulkan has Y pointing down in clip space, DirectX has Y pointing up
        // Flip the viewport to match DirectX behavior
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(m_swapChainExtent.height);  // Start at bottom
        viewport.width = static_cast<float>(m_swapChainExtent.width);
        viewport.height = -static_cast<float>(m_swapChainExtent.height);  // Negative height flips Y
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_swapChainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind vertex buffer at the per-draw ring offset (byteOffset = where AppendDataVulkan put this draw's data).
        VkBuffer vertexBuffers[] = { vbo->m_vkBuffer };
        VkDeviceSize offsets[]   = { (VkDeviceSize)byteOffset };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        // Draw
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
    }
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo)
{
    if (!vbo || !data || size == 0)
    {
        return;
    }

    // Use the VertexBuffer's Vulkan append functionality
    if (vbo->m_vkMappedPtr)
    {
        // Reset and append data for this frame
        vbo->m_vkOffset = 0;
        memcpy(vbo->m_vkMappedPtr, data, size);
    }
}

IndexBuffer* VulkanRenderer::CreateIndexBuffer(const unsigned int size, unsigned int stride)
{
    return new IndexBuffer(m_device, m_physicalDevice, size, stride);
}

void VulkanRenderer::BindIndexBuffer(IndexBuffer* ibo)
{
    if (!ibo || !m_isRenderPassActive)
    {
        return;
    }

    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;
    vkCmdBindIndexBuffer(cmd, ibo->m_vkBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void VulkanRenderer::DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, PrimitiveTopology topology)
{
    UNUSED(topology); // Pipeline topology is set at creation time in Vulkan

    if (!vbo || !ibo || indexCount == 0 || !m_isRenderPassActive)
    {
        return;
    }

    // Select pipeline based on vertex buffer stride (or use the matching deferred override)
    const bool isPcutbn = (vbo->GetStride() == sizeof(Vertex_PCUTBN));
    VkPipeline pipelineToUse = isPcutbn ? m_graphicsPipelinePCUTBN : m_graphicsPipeline;
    if (isPcutbn && m_pipelineOverridePCUTBN != VK_NULL_HANDLE) pipelineToUse = m_pipelineOverridePCUTBN;
    if (!isPcutbn && m_pipelineOverridePCU    != VK_NULL_HANDLE) pipelineToUse = m_pipelineOverridePCU;

    if (pipelineToUse != VK_NULL_HANDLE)
    {
        VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;

        // Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToUse);

        // Update texture descriptor sets if dirty
        if (m_textureBindingDirty && !m_descriptorSets.empty())
        {
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            std::vector<VkDescriptorImageInfo> imageInfos;
            imageInfos.reserve(3);

            // Diffuse texture (binding 1, slot 0)
            const Texture* diffuseTex = m_boundTextures[0] ? m_boundTextures[0] : m_defaultTexture;
            if (diffuseTex && diffuseTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = diffuseTex->m_vkImageView;
                imageInfo.sampler = diffuseTex->m_vkSampler ? diffuseTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 1;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            // Normal texture (binding 2, slot 1)
            const Texture* normalTex = m_boundTextures[1] ? m_boundTextures[1] : m_defaultNormalTexture;
            if (normalTex && normalTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = normalTex->m_vkImageView;
                imageInfo.sampler = normalTex->m_vkSampler ? normalTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 2;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            // Specular texture (binding 3, slot 2)
            const Texture* specTex = m_boundTextures[2] ? m_boundTextures[2] : m_defaultSpecTexture;
            if (specTex && specTex->m_vkImageView != VK_NULL_HANDLE)
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = specTex->m_vkImageView;
                imageInfo.sampler = specTex->m_vkSampler ? specTex->m_vkSampler : m_defaultSampler;
                imageInfos.push_back(imageInfo);

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_descriptorSets[m_currentFrame];
                write.dstBinding = 3;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfos.back();
                descriptorWrites.push_back(write);
            }

            if (!descriptorWrites.empty())
            {
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
                    descriptorWrites.data(), 0, nullptr);
            }
            m_textureBindingDirty = false;
        }

        // Bind descriptor set with current camera+model dynamic offsets (binding 0, then binding 4).
        if (!m_descriptorSets.empty())
        {
            const uint32_t dynOffsets[2] = { m_currentCameraDynamicOffset, m_currentModelDynamicOffset };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout,
                0, 1, &m_descriptorSets[m_currentFrame], 2, dynOffsets);
        }

        // Push diffuse atlas index for bindless texture sampling.
        {
            vkCmdPushConstants(cmd, m_pipelineLayoutOverride ? m_pipelineLayoutOverride : m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(VkMaterialPC), &m_currentMaterial);
        }

        // Set dynamic viewport and scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(m_swapChainExtent.height);
        viewport.width = static_cast<float>(m_swapChainExtent.width);
        viewport.height = -static_cast<float>(m_swapChainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_swapChainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Bind vertex buffer
        VkBuffer vertexBuffers[] = { vbo->m_vkBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        // Bind index buffer
        vkCmdBindIndexBuffer(cmd, ibo->m_vkBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Draw indexed
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    }
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo)
{
    if (!ibo || !data || size == 0)
    {
        return;
    }

    // Use the IndexBuffer's Vulkan mapped memory
    if (ibo->m_vkMappedPtr)
    {
        // Reset and copy data
        ibo->m_vkOffset = 0;
        memcpy(ibo->m_vkMappedPtr, data, size);
    }
}

ConstantBuffer* VulkanRenderer::CreateConstantBuffer(const unsigned int size)
{
    return new ConstantBuffer(m_device, m_physicalDevice, size);
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo)
{
    if (!cbo || !data || size == 0)
    {
        return;
    }

    // Use the ConstantBuffer's Vulkan mapped memory
    if (cbo->m_vkMappedPtr)
    {
        memcpy(cbo->m_vkMappedPtr, data, size);
    }
}

void VulkanRenderer::BindConstantBuffer(int slot, ConstantBuffer* cbo)
{
    if (!cbo || slot < 0)
    {
        return;
    }

    // In Vulkan, constant buffers are bound via descriptor sets
    // The slot corresponds to the binding index in the descriptor set layout
    // For now, we update the descriptor set with the new buffer

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = cbo->m_vkBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSets[m_currentFrame];
    descriptorWrite.dstBinding = slot;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
}

//==============================================================================
// State Management
//==============================================================================
void VulkanRenderer::SetBlendMode(BlendMode blendMode)
{
    m_desiredBlendMode = blendMode;
}

void VulkanRenderer::SetBlendModeIfChanged()
{
}

void VulkanRenderer::SetRasterizerMode(RasterizerMode rasterizerMode)
{
    m_desiredRasterizerMode = rasterizerMode;
}

void VulkanRenderer::SetRasterizerModeIfChanged()
{
}

void VulkanRenderer::SetSamplerMode(SamplerMode samplerMode, int slot)
{
    UNUSED(slot);
    m_desiredSamplerMode = samplerMode;
}

void VulkanRenderer::SetSamplerModeIfChanged()
{
    SetSamplerModeIfChanged(0);
}

void VulkanRenderer::SetSamplerModeIfChanged(int slot)
{
    if (m_desiredSamplerMode == m_currentSamplerMode)
    {
        return;
    }

    m_currentSamplerMode = m_desiredSamplerMode;

    // Select the appropriate sampler based on mode
    VkSampler newSampler = m_defaultSampler;
    switch (m_currentSamplerMode)
    {
    case SamplerMode::POINT_CLAMP:
        newSampler = m_samplerPointClamp;
        break;
    case SamplerMode::BILINEAR_WRAP:
        newSampler = m_samplerBilinearWrap;
        break;
    default:
        newSampler = m_samplerBilinearWrap;
        break;
    }

    // Update the sampler in the bound texture's descriptor
    // This will take effect on the next draw call
    m_textureBindingDirty = true;
    UNUSED(slot);
}

void VulkanRenderer::SetDepthMode(DepthMode depthMode)
{
    m_desiredDepthMode = depthMode;
}

void VulkanRenderer::SetDepthModeIfChanged()
{
}

//==============================================================================
// Lighting and Constants
//==============================================================================
void VulkanRenderer::SetGeneralLightConstants(Rgba8 sunColor, const Vec3& sunNormal, int numLights,
    std::vector<Rgba8> colors, std::vector<Vec3> worldPositions,
    std::vector<Vec3> spotForwards, std::vector<float> ambiences,
    std::vector<float> innerRadii, std::vector<float> outerRadii,
    std::vector<float> innerDotThresholds, std::vector<float> outerDotThresholds)
{
    GeneralLightConstants lightConstants{};

    // Sun color
    lightConstants.SunColor[0] = sunColor.r / 255.0f;
    lightConstants.SunColor[1] = sunColor.g / 255.0f;
    lightConstants.SunColor[2] = sunColor.b / 255.0f;
    lightConstants.SunColor[3] = sunColor.a / 255.0f;

    // Sun normal
    lightConstants.SunNormal[0] = sunNormal.x;
    lightConstants.SunNormal[1] = sunNormal.y;
    lightConstants.SunNormal[2] = sunNormal.z;

    // Number of lights
    lightConstants.NumLights = numLights;

    // Fill light array
    int lightsToProcess = (numLights < s_maxLights) ? numLights : s_maxLights;
    for (int i = 0; i < lightsToProcess; ++i)
    {
        if (i < (int)colors.size())
        {
            lightConstants.LightsArray[i].Color[0] = colors[i].r / 255.0f;
            lightConstants.LightsArray[i].Color[1] = colors[i].g / 255.0f;
            lightConstants.LightsArray[i].Color[2] = colors[i].b / 255.0f;
            lightConstants.LightsArray[i].Color[3] = colors[i].a / 255.0f;
        }
        if (i < (int)worldPositions.size())
        {
            lightConstants.LightsArray[i].WorldPosition[0] = worldPositions[i].x;
            lightConstants.LightsArray[i].WorldPosition[1] = worldPositions[i].y;
            lightConstants.LightsArray[i].WorldPosition[2] = worldPositions[i].z;
        }
        if (i < (int)spotForwards.size())
        {
            lightConstants.LightsArray[i].SpotForward[0] = spotForwards[i].x;
            lightConstants.LightsArray[i].SpotForward[1] = spotForwards[i].y;
            lightConstants.LightsArray[i].SpotForward[2] = spotForwards[i].z;
        }
        if (i < (int)ambiences.size())
        {
            lightConstants.LightsArray[i].Ambience = ambiences[i];
        }
        if (i < (int)innerRadii.size())
        {
            lightConstants.LightsArray[i].InnerRadius = innerRadii[i];
        }
        if (i < (int)outerRadii.size())
        {
            lightConstants.LightsArray[i].OuterRadius = outerRadii[i];
        }
        if (i < (int)innerDotThresholds.size())
        {
            lightConstants.LightsArray[i].InnerDotThreshold = innerDotThresholds[i];
        }
        if (i < (int)outerDotThresholds.size())
        {
            lightConstants.LightsArray[i].OuterDotThreshold = outerDotThresholds[i];
        }
    }

    memcpy(m_lightUniformBuffersMapped[m_currentFrame], &lightConstants, sizeof(GeneralLightConstants));
}

void VulkanRenderer::SetModelConstants(const Mat44& modelToWorldTransform, const Rgba8& modelColor)
{
    ModelConstants modelConstants;
    modelConstants.ModelToWorldTransform = modelToWorldTransform;
    modelConstants.ModelColor[0] = modelColor.r / 255.0f;
    modelConstants.ModelColor[1] = modelColor.g / 255.0f;
    modelConstants.ModelColor[2] = modelColor.b / 255.0f;
    modelConstants.ModelColor[3] = modelColor.a / 255.0f;

    // Write into the model ring buffer at an aligned offset, then advance.
    const uint32_t alignedSlotSize =
        (uint32_t)(((sizeof(ModelConstants) + m_minUboAlignment - 1) / m_minUboAlignment) * m_minUboAlignment);

    if (m_modelRingOffset + alignedSlotSize > kModelRingBufferSize)
    {
        DebuggerPrintf("SetModelConstants: model ring buffer overflow, wrapping (offset=%u size=%u)\n",
            m_modelRingOffset, alignedSlotSize);
        m_modelRingOffset = 0;
    }

    uint8_t* dst = (uint8_t*)m_modelUniformBuffersMapped[m_currentFrame] + m_modelRingOffset;
    memcpy(dst, &modelConstants, sizeof(ModelConstants));
    m_currentModelDynamicOffset = m_modelRingOffset;
    m_modelRingOffset += alignedSlotSize;
}

void VulkanRenderer::SetShadowConstants(const Mat44& lightViewProjectionMatrix)
{
    ShadowConstants shadowConstants{};
    shadowConstants.LightWorldToCamera = lightViewProjectionMatrix;
    shadowConstants.LightCameraToRender = Mat44();
    shadowConstants.LightRenderToClip = Mat44();
    shadowConstants.ShadowMapSize = 2048.0f;
    shadowConstants.ShadowBias = 0.001f;
    shadowConstants.SoftnessFactor = 1.0f;
    shadowConstants.LightSize = 1.0f;

    memcpy(m_shadowUniformBuffersMapped[m_currentFrame], &shadowConstants, sizeof(ShadowConstants));
}

void VulkanRenderer::SetPerFrameConstants(const float time, const int debugInt, const float debugFloat)
{
    PerFrameConstants perFrameConstants{};
    perFrameConstants.Time = time;
    perFrameConstants.DebugInt = debugInt;
    perFrameConstants.DebugFloat = debugFloat;
    perFrameConstants.EMPTY_PADDING = 0.0f;

    memcpy(m_perFrameUniformBuffersMapped[m_currentFrame], &perFrameConstants, sizeof(PerFrameConstants));
}

//==============================================================================
// Shadow Mapping
//==============================================================================
void VulkanRenderer::CreateShadowMapResources()
{
}

void VulkanRenderer::BeginShadowPass()
{
}

void VulkanRenderer::EndShadowPass()
{
}

void VulkanRenderer::CreateShadowMapShader()
{
}

void VulkanRenderer::BindShadowMapTextureAndSampler()
{
}

void VulkanRenderer::SetDefaultTexture()
{
    // Create a 1x1 white texture as default
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };

    Image whiteImage(whitePixel, 1, 1, 4);
    whiteImage.SetName("DefaultWhiteTexture");

    m_defaultTexture = CreateTextureFromImage(whiteImage);

    // Create a 1x1 default normal texture (flat normal pointing up in tangent space: 0.5, 0.5, 1.0)
    uint8_t normalPixel[4] = { 128, 128, 255, 255 };
    Image normalImage(normalPixel, 1, 1, 4);
    normalImage.SetName("DefaultNormalTexture");
    m_defaultNormalTexture = CreateTextureFromImage(normalImage);

    // Create a 1x1 default specular texture (no specular)
    uint8_t specPixel[4] = { 0, 0, 0, 255 };
    Image specImage(specPixel, 1, 1, 4);
    specImage.SetName("DefaultSpecTexture");
    m_defaultSpecTexture = CreateTextureFromImage(specImage);
}

void VulkanRenderer::CreateAndBindDefaultShader()
{
    // Helper lambda to read SPIR-V file
    auto readSpirVFile = [](const std::string& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            return {};
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    };

    // Try to load compiled SPIR-V shaders from file
    std::vector<char> vertShaderCode = readSpirVFile("Data/Shaders/Vulkan/default.vert.spv");
    std::vector<char> fragShaderCode = readSpirVFile("Data/Shaders/Vulkan/default.frag.spv");

    // If files not found, skip pipeline creation (will render nothing but won't crash)
    if (vertShaderCode.empty() || fragShaderCode.empty())
    {
        DebuggerPrintf("Warning: Vulkan shaders not found. Run glslangValidator to compile:\n");
        DebuggerPrintf("  glslangValidator -V Data/Shaders/Vulkan/default.vert -o Data/Shaders/Vulkan/default.vert.spv\n");
        DebuggerPrintf("  glslangValidator -V Data/Shaders/Vulkan/default.frag -o Data/Shaders/Vulkan/default.frag.spv\n");
        return;
    }

    // Create shader modules
    VkShaderModuleCreateInfo vertCreateInfo{};
    vertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertCreateInfo.codeSize = vertShaderCode.size();
    vertCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());

    VkShaderModule vertShaderModule;
    if (vkCreateShaderModule(m_device, &vertCreateInfo, nullptr, &vertShaderModule) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create vertex shader module!");
    }

    VkShaderModuleCreateInfo fragCreateInfo{};
    fragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragCreateInfo.codeSize = fragShaderCode.size();
    fragCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());

    VkShaderModule fragShaderModule;
    if (vkCreateShaderModule(m_device, &fragCreateInfo, nullptr, &fragShaderModule) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create fragment shader module!");
    }

    // Shader stage info
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // Vertex input - matches Vertex_PCU layout
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex_PCU);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    // Position (vec3) at offset 0
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex_PCU, m_position);

    // Color (RGBA8 normalized) at offset 12
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributeDescriptions[1].offset = offsetof(Vertex_PCU, m_color);

    // TexCoord (vec2) at offset 16
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex_PCU, m_uvTextColors);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // Disable culling to ensure visibility
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic states
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Pipeline layout with descriptor set layout, plus a 4-byte push constant
    // for the bindless diffuse texture index (per draw, fragment stage).
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset     = 0;
    pushConstantRange.size       = sizeof(VkMaterialPC);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges    = &pushConstantRange;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create pipeline layout!");
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create graphics pipeline!");
    }

    // Create second pipeline for Vertex_PCUTBN
    // Update vertex input for PCUTBN layout
    VkVertexInputBindingDescription bindingDescriptionPCUTBN{};
    bindingDescriptionPCUTBN.binding = 0;
    bindingDescriptionPCUTBN.stride = sizeof(Vertex_PCUTBN);
    bindingDescriptionPCUTBN.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // PCUTBN has 6 attributes: position, color, texcoord, tangent, bitangent, normal
    // But the shader only uses position, color, texcoord for now
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptionsPCUTBN{};
    // Position (vec3) at offset 0
    attributeDescriptionsPCUTBN[0].binding = 0;
    attributeDescriptionsPCUTBN[0].location = 0;
    attributeDescriptionsPCUTBN[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptionsPCUTBN[0].offset = offsetof(Vertex_PCUTBN, m_position);

    // Color (RGBA8 normalized)
    attributeDescriptionsPCUTBN[1].binding = 0;
    attributeDescriptionsPCUTBN[1].location = 1;
    attributeDescriptionsPCUTBN[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributeDescriptionsPCUTBN[1].offset = offsetof(Vertex_PCUTBN, m_color);

    // TexCoord (vec2)
    attributeDescriptionsPCUTBN[2].binding = 0;
    attributeDescriptionsPCUTBN[2].location = 2;
    attributeDescriptionsPCUTBN[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptionsPCUTBN[2].offset = offsetof(Vertex_PCUTBN, m_uvTexCoords);

    VkPipelineVertexInputStateCreateInfo vertexInputInfoPCUTBN{};
    vertexInputInfoPCUTBN.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfoPCUTBN.vertexBindingDescriptionCount = 1;
    vertexInputInfoPCUTBN.pVertexBindingDescriptions = &bindingDescriptionPCUTBN;
    vertexInputInfoPCUTBN.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptionsPCUTBN.size());
    vertexInputInfoPCUTBN.pVertexAttributeDescriptions = attributeDescriptionsPCUTBN.data();

    // Update pipeline info for PCUTBN
    pipelineInfo.pVertexInputState = &vertexInputInfoPCUTBN;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipelinePCUTBN) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create PCUTBN graphics pipeline!");
    }

    // Cleanup shader modules (no longer needed after pipeline creation)
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
}

//==============================================================================
// Vulkan Initialization Functions
//==============================================================================
void VulkanRenderer::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Game Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Custom Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create Vulkan instance!");
    }

    DebuggerPrintf("Vulkan instance created successfully!\n");
}

void VulkanRenderer::SetupDebugMessenger()
{
    if (!m_enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(m_instance, &createInfo, nullptr, &m_debugMessenger);
    }
}

void VulkanRenderer::CreateSurface()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)m_config.m_window->GetHwnd();
    createInfo.hinstance = GetModuleHandle(nullptr);

    if (vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create window surface!");
    }
#else
    ERROR_AND_DIE("Platform not supported!");
#endif

    DebuggerPrintf("Vulkan surface created successfully!\n");
}

void VulkanRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        ERROR_AND_DIE("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        ERROR_AND_DIE("Failed to find a suitable GPU!");
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProperties);
    DebuggerPrintf("Selected GPU: %s\n", deviceProperties.deviceName);
    m_minUboAlignment = (uint32_t)deviceProperties.limits.minUniformBufferOffsetAlignment;
    if (m_minUboAlignment < 1) m_minUboAlignment = 1;
    DebuggerPrintf("minUniformBufferOffsetAlignment = %u\n", m_minUboAlignment);
}

void VulkanRenderer::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
        indices.transferFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;

    // Enable descriptor indexing features for UPDATE_AFTER_BIND support
    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features = deviceFeatures;
    deviceFeatures2.pNext = &descriptorIndexingFeatures;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &deviceFeatures2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr;  // Using pNext chain instead
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create logical device!");
    }

    if (m_device == VK_NULL_HANDLE)
    {
        ERROR_AND_DIE("Logical device is VK_NULL_HANDLE after creation!");
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, indices.transferFamily.value(), 0, &m_transferQueue);

    DebuggerPrintf("Vulkan logical device created successfully! (device = 0x%p)\n", m_device);
}

void VulkanRenderer::CreateSwapChain()
{
    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    DebuggerPrintf("Vulkan swap chain created successfully!\n");
}

void VulkanRenderer::CreateImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++)
    {
        m_swapChainImageViews[i] = CreateImageView(m_swapChainImages[i], m_swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanRenderer::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create render pass!");
    }
}

void VulkanRenderer::CreateDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Binding 0: Dynamic uniform buffer for camera matrices (ring-buffered, per BeginCamera)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    bindings.push_back(uboLayoutBinding);

    // Binding 1: Bindless texture atlas — array of 256 combined image samplers.
    // Slot 0 is the default white texture; subsequent textures get unique indices.
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = kMaxBindlessTextures;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    bindings.push_back(samplerLayoutBinding);

    // Binding 2: Combined image sampler for normal map
    VkDescriptorSetLayoutBinding normalSamplerBinding{};
    normalSamplerBinding.binding = 2;
    normalSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalSamplerBinding.descriptorCount = 1;
    normalSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    normalSamplerBinding.pImmutableSamplers = nullptr;
    bindings.push_back(normalSamplerBinding);

    // Binding 3: Combined image sampler for specular map
    VkDescriptorSetLayoutBinding specSamplerBinding{};
    specSamplerBinding.binding = 3;
    specSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    specSamplerBinding.descriptorCount = 1;
    specSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    specSamplerBinding.pImmutableSamplers = nullptr;
    bindings.push_back(specSamplerBinding);

    // Binding 4: Dynamic uniform buffer for model constants (ring-buffered, per SetModelConstants)
    VkDescriptorSetLayoutBinding modelUboBinding{};
    modelUboBinding.binding = 4;
    modelUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelUboBinding.descriptorCount = 1;
    modelUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    modelUboBinding.pImmutableSamplers = nullptr;
    bindings.push_back(modelUboBinding);

    // Binding 5: Uniform buffer for lighting
    VkDescriptorSetLayoutBinding lightUboBinding{};
    lightUboBinding.binding = 5;
    lightUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightUboBinding.descriptorCount = 1;
    lightUboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightUboBinding.pImmutableSamplers = nullptr;
    bindings.push_back(lightUboBinding);

    // Binding 6: Uniform buffer for shadow constants
    VkDescriptorSetLayoutBinding shadowUboBinding{};
    shadowUboBinding.binding = 6;
    shadowUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadowUboBinding.descriptorCount = 1;
    shadowUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowUboBinding.pImmutableSamplers = nullptr;
    bindings.push_back(shadowUboBinding);

    // Binding 7: Uniform buffer for per-frame constants
    VkDescriptorSetLayoutBinding perFrameUboBinding{};
    perFrameUboBinding.binding = 7;
    perFrameUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    perFrameUboBinding.descriptorCount = 1;
    perFrameUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    perFrameUboBinding.pImmutableSamplers = nullptr;
    bindings.push_back(perFrameUboBinding);

    // No UPDATE_AFTER_BIND_BIT here — Vulkan forbids mixing it with UNIFORM_BUFFER_DYNAMIC
    // (used at bindings 0 and 4 for camera/model ring buffers). All bindless texture writes
    // happen during texture creation, BEFORE any draw uses the new slot, so update-after-bind
    // semantics aren't required.
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = 0;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create descriptor set layout!");
    }
}

void VulkanRenderer::CreateFramebuffers()
{
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {
            m_swapChainImageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create framebuffer!");
        }
    }
}

void VulkanRenderer::CreateCommandPools()
{
    m_frameData.resize(MAX_FRAMES_IN_FLIGHT);
    QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_frameData[i].commandPool) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create command pool!");
        }
    }
}

void VulkanRenderer::CreateCommandBuffers()
{
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_frameData[i].commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_frameData[i].commandBuffer) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to allocate command buffers!");
        }
    }
}

void VulkanRenderer::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Create per-frame sync objects (fences only now)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameData[i].inFlightFence) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create synchronization objects!");
        }
    }

    // Create per-swapchain-image semaphores to avoid semaphore reuse issues
    // We need one set per swapchain image to ensure proper synchronization
    size_t imageCount = m_swapChainImages.size();
    m_imageAvailableSemaphores.resize(imageCount);
    m_renderFinishedSemaphores.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create per-image synchronization objects!");
        }
    }
    m_semaphoreIndex = 0;
}

void VulkanRenderer::CreateDepthResources()
{
    VkFormat depthFormat = FindDepthFormat();

    CreateImage(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_depthImage, m_depthImageMemory);
    m_depthImageView = CreateImageView(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanRenderer::CreateDescriptorPool()
{
    // Define pool sizes for different descriptor types
    std::vector<VkDescriptorPoolSize> poolSizes;

    // Uniform buffers - for camera, model, lighting constant buffers
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = 100 * MAX_FRAMES_IN_FLIGHT;  // Allow many uniform buffers
    poolSizes.push_back(uboPoolSize);

    // Dynamic uniform buffers - for per-draw constant data
    VkDescriptorPoolSize dynamicUboPoolSize{};
    dynamicUboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dynamicUboPoolSize.descriptorCount = 50 * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(dynamicUboPoolSize);

    // Combined image samplers - bindless atlas needs kMaxBindlessTextures per descriptor set,
    // plus a small surplus for normal/spec/shadow bindings still living at bindings 2,3,...
    VkDescriptorPoolSize samplerPoolSize{};
    samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerPoolSize.descriptorCount = (kMaxBindlessTextures + 16) * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(samplerPoolSize);

    // Storage buffers - for compute shaders or large data
    VkDescriptorPoolSize storagePoolSize{};
    storagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storagePoolSize.descriptorCount = 20 * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(storagePoolSize);

    // Input attachments - for deferred subpass inputs (VulkanDeferredPath uses 3: albedo/normal/depth)
    VkDescriptorPoolSize inputAttachmentPoolSize{};
    inputAttachmentPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    inputAttachmentPoolSize.descriptorCount = 16 * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(inputAttachmentPoolSize);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 500 * MAX_FRAMES_IN_FLIGHT;  // Maximum number of descriptor sets

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create descriptor pool!");
    }
}

void VulkanRenderer::CreateUniformBuffers()
{
    // Camera ring buffer (per frame). Each BeginCamera writes a fresh slot at an aligned offset.
    VkDeviceSize cameraBufferSize = kCameraRingBufferSize;
    m_cameraUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(cameraBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_cameraUniformBuffers[i], m_cameraUniformBuffersMemory[i]);

        vkMapMemory(m_device, m_cameraUniformBuffersMemory[i], 0, cameraBufferSize, 0, &m_cameraUniformBuffersMapped[i]);
    }

    // Model ring buffer (per frame). Each SetModelConstants writes a fresh slot at an aligned offset.
    VkDeviceSize modelBufferSize = kModelRingBufferSize;
    m_modelUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_modelUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_modelUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_modelUniformBuffers[i], m_modelUniformBuffersMemory[i]);

        vkMapMemory(m_device, m_modelUniformBuffersMemory[i], 0, modelBufferSize, 0, &m_modelUniformBuffersMapped[i]);

        // Seed slot 0 with identity model + white color so any draw before SetModelConstants is sane.
        ModelConstants defaultModel;
        defaultModel.ModelToWorldTransform = Mat44();
        defaultModel.ModelColor[0] = 1.0f;
        defaultModel.ModelColor[1] = 1.0f;
        defaultModel.ModelColor[2] = 1.0f;
        defaultModel.ModelColor[3] = 1.0f;
        memcpy(m_modelUniformBuffersMapped[i], &defaultModel, sizeof(ModelConstants));
    }

    // Light uniform buffers
    VkDeviceSize lightBufferSize = sizeof(GeneralLightConstants);
    m_lightUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(lightBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_lightUniformBuffers[i], m_lightUniformBuffersMemory[i]);

        vkMapMemory(m_device, m_lightUniformBuffersMemory[i], 0, lightBufferSize, 0, &m_lightUniformBuffersMapped[i]);

        // Initialize with default values
        GeneralLightConstants defaultLight{};
        memset(&defaultLight, 0, sizeof(GeneralLightConstants));
        memcpy(m_lightUniformBuffersMapped[i], &defaultLight, sizeof(GeneralLightConstants));
    }

    // Shadow uniform buffers
    VkDeviceSize shadowBufferSize = sizeof(ShadowConstants);
    m_shadowUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_shadowUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_shadowUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(shadowBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_shadowUniformBuffers[i], m_shadowUniformBuffersMemory[i]);

        vkMapMemory(m_device, m_shadowUniformBuffersMemory[i], 0, shadowBufferSize, 0, &m_shadowUniformBuffersMapped[i]);

        ShadowConstants defaultShadow{};
        memset(&defaultShadow, 0, sizeof(ShadowConstants));
        memcpy(m_shadowUniformBuffersMapped[i], &defaultShadow, sizeof(ShadowConstants));
    }

    // PerFrame uniform buffers
    VkDeviceSize perFrameBufferSize = sizeof(PerFrameConstants);
    m_perFrameUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_perFrameUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_perFrameUniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(perFrameBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_perFrameUniformBuffers[i], m_perFrameUniformBuffersMemory[i]);

        vkMapMemory(m_device, m_perFrameUniformBuffersMemory[i], 0, perFrameBufferSize, 0, &m_perFrameUniformBuffersMapped[i]);

        PerFrameConstants defaultPerFrame{};
        memset(&defaultPerFrame, 0, sizeof(PerFrameConstants));
        memcpy(m_perFrameUniformBuffersMapped[i], &defaultPerFrame, sizeof(PerFrameConstants));
    }

    // Create samplers for different modes
    // Point Clamp sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerPointClamp) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create point clamp sampler!");
    }

    // Point Wrap sampler
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerPointWrap) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create point wrap sampler!");
    }

    // Bilinear Clamp sampler
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerBilinearClamp) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create bilinear clamp sampler!");
    }

    // Bilinear Wrap sampler
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerBilinearWrap) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create bilinear wrap sampler!");
    }

    // Default sampler (bilinear wrap)
    m_defaultSampler = m_samplerBilinearWrap;

    // Allocate descriptor sets for each frame
    m_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to allocate descriptor sets!");
    }

    // Update descriptor sets
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        // Camera UBO (binding 0) - dynamic. Range = single struct; per-bind dynamic offset selects slot.
        VkDescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = m_cameraUniformBuffers[i];
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraConstants);

        VkWriteDescriptorSet cameraWrite{};
        cameraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        cameraWrite.dstSet = m_descriptorSets[i];
        cameraWrite.dstBinding = 0;
        cameraWrite.dstArrayElement = 0;
        cameraWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        cameraWrite.descriptorCount = 1;
        cameraWrite.pBufferInfo = &cameraBufferInfo;
        descriptorWrites.push_back(cameraWrite);

        // Model UBO (binding 4) - dynamic. Range = single struct; per-bind dynamic offset selects slot.
        VkDescriptorBufferInfo modelBufferInfo{};
        modelBufferInfo.buffer = m_modelUniformBuffers[i];
        modelBufferInfo.offset = 0;
        modelBufferInfo.range = sizeof(ModelConstants);

        VkWriteDescriptorSet modelWrite{};
        modelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        modelWrite.dstSet = m_descriptorSets[i];
        modelWrite.dstBinding = 4;
        modelWrite.dstArrayElement = 0;
        modelWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        modelWrite.descriptorCount = 1;
        modelWrite.pBufferInfo = &modelBufferInfo;
        descriptorWrites.push_back(modelWrite);

        // Light UBO (binding 5)
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = m_lightUniformBuffers[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(GeneralLightConstants);

        VkWriteDescriptorSet lightWrite{};
        lightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lightWrite.dstSet = m_descriptorSets[i];
        lightWrite.dstBinding = 5;
        lightWrite.dstArrayElement = 0;
        lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightWrite.descriptorCount = 1;
        lightWrite.pBufferInfo = &lightBufferInfo;
        descriptorWrites.push_back(lightWrite);

        // Shadow UBO (binding 6)
        VkDescriptorBufferInfo shadowBufferInfo{};
        shadowBufferInfo.buffer = m_shadowUniformBuffers[i];
        shadowBufferInfo.offset = 0;
        shadowBufferInfo.range = sizeof(ShadowConstants);

        VkWriteDescriptorSet shadowWrite{};
        shadowWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrite.dstSet = m_descriptorSets[i];
        shadowWrite.dstBinding = 6;
        shadowWrite.dstArrayElement = 0;
        shadowWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowWrite.descriptorCount = 1;
        shadowWrite.pBufferInfo = &shadowBufferInfo;
        descriptorWrites.push_back(shadowWrite);

        // PerFrame UBO (binding 7)
        VkDescriptorBufferInfo perFrameBufferInfo{};
        perFrameBufferInfo.buffer = m_perFrameUniformBuffers[i];
        perFrameBufferInfo.offset = 0;
        perFrameBufferInfo.range = sizeof(PerFrameConstants);

        VkWriteDescriptorSet perFrameWrite{};
        perFrameWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        perFrameWrite.dstSet = m_descriptorSets[i];
        perFrameWrite.dstBinding = 7;
        perFrameWrite.dstArrayElement = 0;
        perFrameWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        perFrameWrite.descriptorCount = 1;
        perFrameWrite.pBufferInfo = &perFrameBufferInfo;
        descriptorWrites.push_back(perFrameWrite);

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(), 0, nullptr);
    }
}

//==============================================================================
// Helper Functions
//==============================================================================
QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            indices.transferFamily = i;
        }

        if (indices.IsComplete())
        {
            break;
        }

        i++;
    }

    return indices;
}

SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = { 1600, 800 };
    actualExtent.width = (uint32_t)MaxI(capabilities.minImageExtent.width,
        MinI(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = (uint32_t)MaxI(capabilities.minImageExtent.height,
        MinI(capabilities.maxImageExtent.height, actualExtent.height));
    return actualExtent;
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = FindQueueFamilies(device);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.IsComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions()
{
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef VK_USE_PLATFORM_WIN32_KHR
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    if (m_enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

bool VulkanRenderer::CheckValidationLayerSupport()
{
    return true;
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create shader module!");
    }

    return shaderModule;
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat VulkanRenderer::FindDepthFormat()
{
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_device, buffer, bufferMemory, 0);
}

void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    UNUSED(srcBuffer);
    UNUSED(dstBuffer);
    UNUSED(size);
}

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
    VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to allocate image memory!");
    }

    vkBindImageMemory(m_device, image, imageMemory, 0);
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    UNUSED(format);

    // Use the dedicated transfer command pool
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_transferCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Image memory barrier
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // End and submit command buffer with fence
    vkEndCommandBuffer(commandBuffer);

    // Reset fence before use
    vkResetFences(m_device, 1, &m_transferFence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_transferFence);

    // Wait for fence instead of vkQueueWaitIdle
    vkWaitForFences(m_device, 1, &m_transferFence, VK_TRUE, UINT64_MAX);

    vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    // Use the dedicated transfer command pool
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_transferCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // End and submit command buffer with fence
    vkEndCommandBuffer(commandBuffer);

    // Reset fence before use
    vkResetFences(m_device, 1, &m_transferFence);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_transferFence);

    // Wait for fence instead of vkQueueWaitIdle
    vkWaitForFences(m_device, 1, &m_transferFence, VK_TRUE, UINT64_MAX);

    vkFreeCommandBuffers(m_device, m_transferCommandPool, 1, &commandBuffer);
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create texture image view!");
    }

    return imageView;
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    ERROR_AND_DIE("Failed to find suitable memory type!");
    return 0;
}

void VulkanRenderer::RecreateSwapChain()
{
    // Handle window minimization - wait until window is visible again
    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(m_physicalDevice);
    VkExtent2D extent = swapChainSupport.capabilities.currentExtent;

    // If extent is 0, window is minimized - wait until it's restored
    while (extent.width == 0 || extent.height == 0)
    {
        swapChainSupport = QuerySwapChainSupport(m_physicalDevice);
        extent = swapChainSupport.capabilities.currentExtent;

        // Sleep briefly to avoid busy-waiting
        Sleep(10);

        // Process window messages to detect when window is restored
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    vkDeviceWaitIdle(m_device);

    // Cleanup old per-image semaphores
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++)
    {
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();

    CleanupSwapChain();
    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();

    // Recreate per-image semaphores for new swapchain
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    size_t imageCount = m_swapChainImages.size();
    m_imageAvailableSemaphores.resize(imageCount);
    m_renderFinishedSemaphores.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to recreate per-image synchronization objects!");
        }
    }
    m_semaphoreIndex = 0;
}

void VulkanRenderer::CleanupSwapChain()
{
    // Destroy framebuffers first (they reference image views)
    for (auto framebuffer : m_swapChainFramebuffers)
    {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapChainFramebuffers.clear();

    // Destroy depth resources (must be done before creating new ones in RecreateSwapChain)
    if (m_depthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }

    // Destroy swapchain image views
    for (auto imageView : m_swapChainImageViews)
    {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();

    // Destroy swapchain
    if (m_swapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

VkCommandBuffer VulkanRenderer::GetCurrentCommandBuffer() const
{
    return m_frameData[m_currentFrame].commandBuffer;
}

#endif // ENGINE_VULKAN_RENDERER