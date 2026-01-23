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
#include "Engine/Core/Image.hpp"

#include <set>
#include <algorithm>
#include <fstream>

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
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateCommandBuffers();
    CreateSyncObjects();

    // Create default resources
    SetDefaultTexture();
    CreateAndBindDefaultShader();

    // Create immediate mode buffers
    m_immediateVBO = CreateVertexBuffer(sizeof(Vertex_PCU) * 10000, sizeof(Vertex_PCU));
    m_immediateVBOForVertex_PCUTBN = CreateVertexBuffer(sizeof(Vertex_PCUTBN) * 10000, sizeof(Vertex_PCUTBN));
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

    for (size_t i = 0; i < m_uniformBuffers.size(); i++)
    {
        vkDestroyBuffer(m_device, m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_device, m_uniformBuffersMemory[i], nullptr);
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
    vkWaitForFences(m_device, 1, &m_frameData[m_currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_frameData[m_currentFrame].imageAvailableSemaphore, VK_NULL_HANDLE, &m_imageIndex);

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

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to record command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { m_frameData[m_currentFrame].imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = { m_frameData[m_currentFrame].renderFinishedSemaphore };
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
}

//==============================================================================
// Screen and Camera
//==============================================================================
void VulkanRenderer::ClearScreen(const Rgba8& clearColor)
{
    UNUSED(clearColor);
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
}

void VulkanRenderer::ClearScreen()
{
    ClearScreen(Rgba8::MAGENTA);
}

void VulkanRenderer::BeginCamera(const Camera& camera)
{
    UNUSED(camera);
}

void VulkanRenderer::EndCamera(const Camera& camera)
{
    UNUSED(camera);
    VkCommandBuffer cmd = m_frameData[m_currentFrame].commandBuffer;
    vkCmdEndRenderPass(cmd);
}

void VulkanRenderer::SetViewport(const AABB2& normalizedViewport)
{
    UNUSED(normalizedViewport);
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
    UNUSED(numVerts);
    UNUSED(verts);
}

void VulkanRenderer::DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts)
{
    DrawVertexArray((int)verts.size(), verts.data());
}

void VulkanRenderer::DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts)
{
    UNUSED(numVerts);
    UNUSED(verts);
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
    UNUSED(numVerts);
    UNUSED(verts);
    UNUSED(numIndices);
    UNUSED(indices);
}

void VulkanRenderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices,
    VertexBuffer* vbo, IndexBuffer* ibo)
{
    UNUSED(numVerts);
    UNUSED(verts);
    UNUSED(numIndices);
    UNUSED(indices);
    UNUSED(vbo);
    UNUSED(ibo);
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
    UNUSED(usingMipmaps);

    IntVec2 dimensions = image.GetDimensions();
    VkDeviceSize imageSize = dimensions.x * dimensions.y * 4; // RGBA = 4 bytes per pixel

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

    CreateImage(dimensions.x, dimensions.y, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newTexture->m_vkImage, newTexture->m_vkImageMemory);

    // Transition image layout and copy data
    TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, newTexture->m_vkImage, static_cast<uint32_t>(dimensions.x), static_cast<uint32_t>(dimensions.y));
    TransitionImageLayout(newTexture->m_vkImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &newTexture->m_vkSampler) != VK_SUCCESS)
    {
        ERROR_AND_DIE("Failed to create texture sampler!");
    }

    m_loadedTextures.push_back(newTexture);
    return newTexture;
}

Texture* VulkanRenderer::CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
    UNUSED(usingMipmaps);

    // Check if texture already exists
    Texture* existingTexture = GetTextureForFileName(imageFilePath);
    if (existingTexture)
    {
        return existingTexture;
    }

    // TODO: Implement proper texture loading with separate command pool and fence
    // Current implementation causes Vulkan synchronization issues when loading during frame
    // For now, return nullptr - debug text won't display but program won't crash
    UNUSED(imageFilePath);
    return nullptr;
}

Texture* VulkanRenderer::CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel,
    uint8_t* texelData, bool usingMipmaps)
{
    UNUSED(name);
    UNUSED(dimensions);
    UNUSED(bytesPerTexel);
    UNUSED(texelData);
    UNUSED(usingMipmaps);
    return nullptr;
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
    UNUSED(texture);
    UNUSED(slot);
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
    UNUSED(shaderName);
    UNUSED(shaderSource);
    UNUSED(vertexType);
    return nullptr;
}

Shader* VulkanRenderer::CreateShader(char const* shaderName, VertexType vertexType)
{
    UNUSED(shaderName);
    UNUSED(vertexType);
    return nullptr;
}

Shader* VulkanRenderer::CreateOrGetShader(char const* shaderName, VertexType vertexType)
{
    UNUSED(shaderName);
    UNUSED(vertexType);
    return nullptr;
}

void VulkanRenderer::BindShader(Shader* shader)
{
    UNUSED(shader);
    m_currentShader = shader;
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
    UNUSED(vbo);
}

void VulkanRenderer::DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount)
{
    UNUSED(vbo);
    UNUSED(vertexCount);
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo)
{
    UNUSED(data);
    UNUSED(size);
    UNUSED(vbo);
}

IndexBuffer* VulkanRenderer::CreateIndexBuffer(const unsigned int size, unsigned int stride)
{
    return new IndexBuffer(m_device, m_physicalDevice, size, stride);
}

void VulkanRenderer::BindIndexBuffer(IndexBuffer* ibo)
{
    UNUSED(ibo);
}

void VulkanRenderer::DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, PrimitiveTopology topology)
{
    UNUSED(vbo);
    UNUSED(ibo);
    UNUSED(indexCount);
    UNUSED(topology);
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo)
{
    UNUSED(data);
    UNUSED(size);
    UNUSED(ibo);
}

ConstantBuffer* VulkanRenderer::CreateConstantBuffer(const unsigned int size)
{
    return new ConstantBuffer(m_device, m_physicalDevice, size);
}

void VulkanRenderer::CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo)
{
    UNUSED(data);
    UNUSED(size);
    UNUSED(cbo);
}

void VulkanRenderer::BindConstantBuffer(int slot, ConstantBuffer* cbo)
{
    UNUSED(slot);
    UNUSED(cbo);
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
}

void VulkanRenderer::SetSamplerModeIfChanged(int slot)
{
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
    UNUSED(sunColor);
    UNUSED(sunNormal);
    UNUSED(numLights);
    UNUSED(colors);
    UNUSED(worldPositions);
    UNUSED(spotForwards);
    UNUSED(ambiences);
    UNUSED(innerRadii);
    UNUSED(outerRadii);
    UNUSED(innerDotThresholds);
    UNUSED(outerDotThresholds);
}

void VulkanRenderer::SetModelConstants(const Mat44& modelToWorldTransform, const Rgba8& modelColor)
{
    UNUSED(modelToWorldTransform);
    UNUSED(modelColor);
}

void VulkanRenderer::SetShadowConstants(const Mat44& lightViewProjectionMatrix)
{
    UNUSED(lightViewProjectionMatrix);
}

void VulkanRenderer::SetPerFrameConstants(const float time, const int debugInt, const float debugFloat)
{
    UNUSED(time);
    UNUSED(debugInt);
    UNUSED(debugFloat);
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
}

void VulkanRenderer::CreateAndBindDefaultShader()
{
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

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
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

    // Binding 0: Uniform buffer for vertex shader (camera matrices, etc.)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    bindings.push_back(uboLayoutBinding);

    // Binding 1: Combined image sampler for diffuse texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
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

    // Binding 4: Uniform buffer for model constants
    VkDescriptorSetLayoutBinding modelUboBinding{};
    modelUboBinding.binding = 4;
    modelUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
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

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_frameData[i].renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_frameData[i].inFlightFence) != VK_SUCCESS)
        {
            ERROR_AND_DIE("Failed to create synchronization objects!");
        }
    }
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

    // Combined image samplers - for textures (diffuse, normal, specular, shadow maps)
    VkDescriptorPoolSize samplerPoolSize{};
    samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerPoolSize.descriptorCount = 200 * MAX_FRAMES_IN_FLIGHT;  // Allow many textures
    poolSizes.push_back(samplerPoolSize);

    // Storage buffers - for compute shaders or large data
    VkDescriptorPoolSize storagePoolSize{};
    storagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    storagePoolSize.descriptorCount = 20 * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(storagePoolSize);

    // Input attachments - for subpass inputs
    VkDescriptorPoolSize inputAttachmentPoolSize{};
    inputAttachmentPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    inputAttachmentPoolSize.descriptorCount = 10 * MAX_FRAMES_IN_FLIGHT;
    poolSizes.push_back(inputAttachmentPoolSize);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow freeing individual sets
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
    VkDeviceSize bufferSize = sizeof(Mat44) * 3;

    m_uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i], m_uniformBuffersMemory[i]);

        vkMapMemory(m_device, m_uniformBuffersMemory[i], 0, bufferSize, 0, &m_uniformBuffersMapped[i]);
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
    UNUSED(code);
    return VK_NULL_HANDLE;
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

    // Begin single-time command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_frameData[0].commandPool;
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

    // End and submit command buffer
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_frameData[0].commandPool, 1, &commandBuffer);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    // Begin single-time command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_frameData[0].commandPool;
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

    // End and submit command buffer
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_frameData[0].commandPool, 1, &commandBuffer);
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
    CleanupSwapChain();
    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
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