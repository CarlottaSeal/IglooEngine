#pragma once

#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_VULKAN_RENDERER
    #include <vulkan/vulkan.h>
    #include <vector>
    #include <optional>

#include <optional>
#include <vulkan/vulkan_core.h>

#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/IntVec2.hpp"

class Window;
class BitmapFont;
class Image;
class Texture;
class Shader;
class VertexBuffer;
class ConstantBuffer;
class IndexBuffer;

// Vulkan Queue Family Indices
struct QueueFamilyIndices 
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> transferFamily;

    bool IsComplete() const 
    {
        return graphicsFamily.has_value() && 
               presentFamily.has_value() &&
               transferFamily.has_value();
    }
};

// Vulkan Swapchain Support Details
struct SwapChainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Vulkan Frame Data (for double/triple buffering)
struct FrameData 
{
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
};

class VulkanRenderer
{
public:
    VulkanRenderer(RendererConfig config);
    ~VulkanRenderer();

    void Startup();
    void ShutDown();

    // Frame functions
    void BeginFrame();
    void EndFrame();

    // Screen and Camera
    void ClearScreen(const Rgba8& clearColor);
    void ClearScreen();
    void BeginCamera(const Camera& camera);
    void EndCamera(const Camera& camera);
    void SetViewport(const AABB2& normalizedViewport);

    // Drawing functions
    void DrawVertexArray(const std::vector<Vertex_PCU>& verts);
    void DrawVertexArray(int numVerts, const Vertex_PCU* verts);
    void DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts);
    void DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts);
    void DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices);
    void DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices, 
                              VertexBuffer* vbo, IndexBuffer* ibo);
    void DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices);
    void DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices, 
                              VertexBuffer* vbo, IndexBuffer* ibo);

    // Texture functions
    Image* CreateImageFromFile(char const* imageFilePath);
    Texture* CreateTextureFromImage(const Image& image, bool usingMipmaps = false);
    Texture* CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps = false);
    Texture* CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel, 
                                   uint8_t* texelData, bool usingMipmaps = false);
    Texture* CreateTextureFromFile(char const* imageFilePath, bool usingMipmaps = false);
    Texture* GetTextureForFileName(const char* imageFilePath);
    void BindTexture(const Texture* texture, int slot = 0);

    // Font functions
    BitmapFont* CreateOrGetBitmapFont(const char* bitmapFontFilePathWithNoExtension);

    // Shader functions
    Shader* CreateShader(char const* shaderName, char const* shaderSource, VertexType vertexType = VertexType::VERTEX_PCU);
    Shader* CreateShader(char const* shaderName, VertexType vertexType = VertexType::VERTEX_PCU);
    Shader* CreateOrGetShader(char const* shaderName, VertexType vertexType = VertexType::VERTEX_PCU);
    void BindShader(Shader* shader);

    // Buffer functions
    VertexBuffer* CreateVertexBuffer(const unsigned int size, unsigned int stride);
    void BindVertexBuffer(VertexBuffer* vbo);
    void DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount);
    void CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo);

    IndexBuffer* CreateIndexBuffer(const unsigned int size, unsigned int stride);
    void BindIndexBuffer(IndexBuffer* ibo);
    void DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, 
                         PrimitiveTopology topology = PRIMITIVE_TRIANGLES);
    void CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo);

    ConstantBuffer* CreateConstantBuffer(const unsigned int size);
    void CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo);
    void BindConstantBuffer(int slot, ConstantBuffer* cbo);

    // State functions
    void SetBlendMode(BlendMode blendMode);
    void SetBlendModeIfChanged();
    void SetRasterizerMode(RasterizerMode rasterizerMode);
    void SetRasterizerModeIfChanged();
    void SetSamplerMode(SamplerMode samplerMode, int slot = 0);
    void SetSamplerModeIfChanged();
    void SetSamplerModeIfChanged(int slot);
    void SetDepthMode(DepthMode depthMode);
    void SetDepthModeIfChanged();

    // Lighting constants
    void SetGeneralLightConstants(Rgba8 sunColor, const Vec3& sunNormal, int numLights, 
                                  std::vector<Rgba8> colors, std::vector<Vec3> worldPositions, 
                                  std::vector<Vec3> spotForwards, std::vector<float> ambiences,
                                  std::vector<float> innerRadii, std::vector<float> outerRadii,
                                  std::vector<float> innerDotThresholds, std::vector<float> outerDotThresholds);
    void SetModelConstants(const Mat44& modelToWorldTransform = Mat44(), const Rgba8& modelColor = Rgba8::WHITE);
    void SetShadowConstants(const Mat44& lightViewProjectionMatrix = Mat44());
    void SetPerFrameConstants(const float time, const int debugInt, const float debugFloat);

    // Shadow mapping
    void CreateShadowMapResources();
    void BeginShadowPass();
    void EndShadowPass();
    void CreateShadowMapShader();
    void BindShadowMapTextureAndSampler();
    void SetDefaultTexture();
    void CreateAndBindDefaultShader();

    // Vulkan-specific getters
    VkInstance GetInstance() const { return m_instance; }
    VkDevice GetDevice() const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkCommandBuffer GetCurrentCommandBuffer() const;
    VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }

private:
    // Initialization functions
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPools();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateDepthResources();
    void CreateDescriptorPool();
    void CreateUniformBuffers();

    // Helper functions
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    bool IsDeviceSuitable(VkPhysicalDevice device);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    std::vector<const char*> GetRequiredExtensions();
    bool CheckValidationLayerSupport();
    VkShaderModule CreateShaderModule(const std::vector<char>& code);
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat FindDepthFormat();
    
    // Resource creation helpers
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Frame management
    void RecreateSwapChain();
    void CleanupSwapChain();

protected:
    RendererConfig m_config;

private:
    // Vulkan Core Objects
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    // Queues
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;

    // Swapchain
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    VkFormat m_swapChainImageFormat;
    VkExtent2D m_swapChainExtent;

    // Render Pass
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    // Pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;        // For Vertex_PCU
    VkPipeline m_graphicsPipelinePCUTBN = VK_NULL_HANDLE;  // For Vertex_PCUTBN

    // Descriptor Pool
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    // Frame Data (for multiple frames in flight)
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<FrameData> m_frameData;
    size_t m_currentFrame = 0;
    uint32_t m_imageIndex = 0;

    // Per-swapchain-image semaphores (to avoid semaphore reuse issues)
    std::vector<VkSemaphore> m_imageAvailableSemaphores;  // One per swapchain image
    std::vector<VkSemaphore> m_renderFinishedSemaphores;  // One per swapchain image
    uint32_t m_semaphoreIndex = 0;  // Tracks which semaphore to use next

    // Depth Buffer
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    // Uniform Buffers (legacy - to be removed)
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<void*> m_uniformBuffersMapped;

    // Camera Uniform Buffers (per frame)
    std::vector<VkBuffer> m_cameraUniformBuffers;
    std::vector<VkDeviceMemory> m_cameraUniformBuffersMemory;
    std::vector<void*> m_cameraUniformBuffersMapped;

    // Model Uniform Buffers (per frame)
    std::vector<VkBuffer> m_modelUniformBuffers;
    std::vector<VkDeviceMemory> m_modelUniformBuffersMemory;
    std::vector<void*> m_modelUniformBuffersMapped;

    // Light Uniform Buffers (per frame)
    std::vector<VkBuffer> m_lightUniformBuffers;
    std::vector<VkDeviceMemory> m_lightUniformBuffersMemory;
    std::vector<void*> m_lightUniformBuffersMapped;

    // Shadow Uniform Buffers (per frame)
    std::vector<VkBuffer> m_shadowUniformBuffers;
    std::vector<VkDeviceMemory> m_shadowUniformBuffersMemory;
    std::vector<void*> m_shadowUniformBuffersMapped;

    // PerFrame Uniform Buffers (per frame)
    std::vector<VkBuffer> m_perFrameUniformBuffers;
    std::vector<VkDeviceMemory> m_perFrameUniformBuffersMemory;
    std::vector<void*> m_perFrameUniformBuffersMapped;

    // Samplers for different modes
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
    VkSampler m_samplerPointClamp = VK_NULL_HANDLE;
    VkSampler m_samplerPointWrap = VK_NULL_HANDLE;
    VkSampler m_samplerBilinearClamp = VK_NULL_HANDLE;
    VkSampler m_samplerBilinearWrap = VK_NULL_HANDLE;
    SamplerMode m_currentSamplerMode = SamplerMode::POINT_CLAMP;

    // Staging Buffer for transfers
    VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingBufferMemory = VK_NULL_HANDLE;

    // Transfer command pool (for texture uploads, separate from frame command pools)
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    VkFence m_transferFence = VK_NULL_HANDLE;

    // Resources
    std::vector<Texture*> m_loadedTextures;
    std::vector<BitmapFont*> m_loadedFonts;
    std::vector<Shader*> m_loadedShaders;
    Shader* m_currentShader = nullptr;
    Shader* m_defaultShader = nullptr;

    // Immediate mode buffers
    VertexBuffer* m_immediateVBO = nullptr;
    VertexBuffer* m_immediateVBOForVertex_PCUTBN = nullptr;
    IndexBuffer* m_immediateIBO = nullptr;

    // Constant Buffers
    ConstantBuffer* m_cameraCBO = nullptr;
    ConstantBuffer* m_modelCBO = nullptr;
    ConstantBuffer* m_generalLightCBO = nullptr;
    ConstantBuffer* m_shadowCBO = nullptr;
    ConstantBuffer* m_perFrameCBO = nullptr;

    // State tracking
    BlendMode m_desiredBlendMode = BlendMode::ALPHA;
    SamplerMode m_desiredSamplerMode = SamplerMode::POINT_CLAMP;
    RasterizerMode m_desiredRasterizerMode = RasterizerMode::SOLID_CULL_BACK;
    DepthMode m_desiredDepthMode = DepthMode::READ_WRITE_LESS_EQUAL;
    bool m_isRenderPassActive = false;

    // Bound textures (for each slot)
    static const int MAX_TEXTURE_SLOTS = 4;
    const Texture* m_boundTextures[MAX_TEXTURE_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
    bool m_textureBindingDirty = false;

    // Default textures
    const Texture* m_defaultTexture = nullptr;
    const Texture* m_defaultNormalTexture = nullptr;
    const Texture* m_defaultSpecTexture = nullptr;

    // Shadow mapping
    VkImage m_shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowImageMemory = VK_NULL_HANDLE;
    VkImageView m_shadowImageView = VK_NULL_HANDLE;
    VkFramebuffer m_shadowFramebuffer = VK_NULL_HANDLE;
    VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
    Shader* m_shadowPassShader = nullptr;

    // Validation layers
    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // Device extensions
    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef ENGINE_DEBUG_RENDER
    const bool m_enableValidationLayers = true;
#else
    const bool m_enableValidationLayers = false;
#endif

    bool m_framebufferResized = false;
};

#endif // ENGINE_VULKAN_RENDERER
