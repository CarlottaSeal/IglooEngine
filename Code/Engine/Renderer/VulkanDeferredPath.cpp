#include "Engine/Renderer/VulkanDeferredPath.h"

#ifdef ENGINE_VULKAN_RENDERER

#include "Engine/Renderer/VulkanRenderer.h"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/Vertex_PCU.hpp"

#include <array>
#include <cstring>
#include <fstream>

namespace
{
    uint32_t g_currentSwapImageIdx = 0; // captured by BeginGBuffer, consumed by End*
}

void VulkanDeferredPath::Init(VulkanRenderer* renderer)
{
    m_renderer = renderer;
    m_device   = renderer->m_device;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(renderer->m_physicalDevice, &props);
    m_timestampPeriodNs = (double)props.limits.timestampPeriod;

    m_width  = renderer->m_swapChainExtent.width;
    m_height = renderer->m_swapChainExtent.height;

    CreateRenderPass();
    CreateSet1LayoutAndLightUBO();
    CreatePipelines();
    CreateQueryPool();
    CreatePerSwapResources();

    DebuggerPrintf("VulkanDeferredPath: initialized (%ux%u, %zu swap images, timestampPeriod=%.2f ns)\n",
        m_width, m_height, m_perSwap.size(), m_timestampPeriodNs);
}

void VulkanDeferredPath::Shutdown()
{
    if (!m_device) return;
    vkDeviceWaitIdle(m_device);

    DestroyPerSwapResources();

    if (m_overlayPipeline)         vkDestroyPipeline(m_device, m_overlayPipeline, nullptr);
    if (m_forwardPipelinePCUTBN)   vkDestroyPipeline(m_device, m_forwardPipelinePCUTBN, nullptr);
    if (m_forwardPipeline)         vkDestroyPipeline(m_device, m_forwardPipeline, nullptr);
    if (m_forwardPipelineLayout)   vkDestroyPipelineLayout(m_device, m_forwardPipelineLayout, nullptr);
    if (m_lightingPipeline)        vkDestroyPipeline(m_device, m_lightingPipeline, nullptr);
    if (m_lightingPipelineLayout)  vkDestroyPipelineLayout(m_device, m_lightingPipelineLayout, nullptr);
    if (m_gbufferPipelinePCUTBN)   vkDestroyPipeline(m_device, m_gbufferPipelinePCUTBN, nullptr);
    if (m_gbufferPipeline)         vkDestroyPipeline(m_device, m_gbufferPipeline, nullptr);
    if (m_gbufferPipelineLayout)   vkDestroyPipelineLayout(m_device, m_gbufferPipelineLayout, nullptr);

    if (m_lightUBOMapped)          vkUnmapMemory(m_device, m_lightUBOMem);
    if (m_lightUBO)                vkDestroyBuffer(m_device, m_lightUBO, nullptr);
    if (m_lightUBOMem)             vkFreeMemory(m_device, m_lightUBOMem, nullptr);

    if (m_forwardSet1Layout)       vkDestroyDescriptorSetLayout(m_device, m_forwardSet1Layout, nullptr);
    if (m_set1Layout)              vkDestroyDescriptorSetLayout(m_device, m_set1Layout, nullptr);
    if (m_queryPool)               vkDestroyQueryPool(m_device, m_queryPool, nullptr);
    if (m_overlayRenderPass)       vkDestroyRenderPass(m_device, m_overlayRenderPass, nullptr);
    if (m_forwardRenderPass)       vkDestroyRenderPass(m_device, m_forwardRenderPass, nullptr);
    if (m_deferredRenderPass)      vkDestroyRenderPass(m_device, m_deferredRenderPass, nullptr);

    *this = VulkanDeferredPath{};
}

void VulkanDeferredPath::OnSwapchainResized()
{
    if (!m_renderer) return;
    vkDeviceWaitIdle(m_device);
    m_width  = m_renderer->m_swapChainExtent.width;
    m_height = m_renderer->m_swapChainExtent.height;
    DestroyPerSwapResources();
    CreatePerSwapResources();
}

void VulkanDeferredPath::CreateRenderPass()
{
    std::array<VkAttachmentDescription, 4> atts{};

    atts[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    atts[1] = atts[0];
    atts[1].format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;

    atts[2].format         = VK_FORMAT_D32_SFLOAT;
    atts[2].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[2].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[2].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[2].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    atts[3].format         = m_renderer->m_swapChainImageFormat;
    atts[3].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[3].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[3].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[3].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[3].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference gbufColor[2] = {
        { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
    };
    VkAttachmentReference gbufDepth = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub0{};
    sub0.pipelineBindPoint        = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub0.colorAttachmentCount     = 2;
    sub0.pColorAttachments        = gbufColor;
    sub0.pDepthStencilAttachment  = &gbufDepth;

    VkAttachmentReference inputs[3] = {
        { 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
    };
    VkAttachmentReference lightColor = { 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub1{};
    sub1.pipelineBindPoint        = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub1.inputAttachmentCount     = 3;
    sub1.pInputAttachments        = inputs;
    sub1.colorAttachmentCount     = 1;
    sub1.pColorAttachments        = &lightColor;

    VkSubpassDescription subpasses[2] = { sub0, sub1 };

    std::array<VkSubpassDependency, 3> deps{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // 0 -> 1: tile-local hand-off (BY_REGION_BIT — the keyword)
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = 1;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[2].srcSubpass    = 1;
    deps[2].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[2].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[2].dstAccessMask = 0;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = (uint32_t)atts.size();
    rpci.pAttachments    = atts.data();
    rpci.subpassCount    = 2;
    rpci.pSubpasses      = subpasses;
    rpci.dependencyCount = (uint32_t)deps.size();
    rpci.pDependencies   = deps.data();

    if (vkCreateRenderPass(m_device, &rpci, nullptr, &m_deferredRenderPass) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: failed to create deferred render pass");

    {
        VkAttachmentDescription overlayColor{};
        overlayColor.format         = m_renderer->m_swapChainImageFormat;
        overlayColor.samples        = VK_SAMPLE_COUNT_1_BIT;
        overlayColor.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlayColor.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        overlayColor.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        overlayColor.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        overlayColor.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayColor.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference overlayRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription overlaySub{};
        overlaySub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        overlaySub.colorAttachmentCount = 1;
        overlaySub.pColorAttachments    = &overlayRef;

        VkSubpassDependency overlayDeps[2]{};
        overlayDeps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        overlayDeps[0].dstSubpass    = 0;
        overlayDeps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        overlayDeps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        overlayDeps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        overlayDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                     | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

        overlayDeps[1].srcSubpass    = 0;
        overlayDeps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        overlayDeps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        overlayDeps[1].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        overlayDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        overlayDeps[1].dstAccessMask = 0;

        VkRenderPassCreateInfo orpci{};
        orpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        orpci.attachmentCount = 1; orpci.pAttachments = &overlayColor;
        orpci.subpassCount    = 1; orpci.pSubpasses   = &overlaySub;
        orpci.dependencyCount = 2; orpci.pDependencies = overlayDeps;
        if (vkCreateRenderPass(m_device, &orpci, nullptr, &m_overlayRenderPass) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: failed to create overlay render pass");
    }

    {
        VkAttachmentDescription atts2[2]{};
        atts2[0].format         = m_renderer->m_swapChainImageFormat;
        atts2[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts2[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts2[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts2[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts2[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts2[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts2[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        atts2[1].format         = VK_FORMAT_D32_SFLOAT;
        atts2[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts2[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts2[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts2[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts2[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts2[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts2[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference fwdColor = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference fwdDepth = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription fwdSub{};
        fwdSub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        fwdSub.colorAttachmentCount    = 1;
        fwdSub.pColorAttachments       = &fwdColor;
        fwdSub.pDepthStencilAttachment = &fwdDepth;

        VkSubpassDependency fwdDeps[2]{};
        fwdDeps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        fwdDeps[0].dstSubpass    = 0;
        fwdDeps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        fwdDeps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                 | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        fwdDeps[0].srcAccessMask = 0;
        fwdDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                 | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        fwdDeps[1].srcSubpass    = 0;
        fwdDeps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        fwdDeps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        fwdDeps[1].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        fwdDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        fwdDeps[1].dstAccessMask = 0;

        VkRenderPassCreateInfo frpci{};
        frpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        frpci.attachmentCount = 2; frpci.pAttachments = atts2;
        frpci.subpassCount    = 1; frpci.pSubpasses   = &fwdSub;
        frpci.dependencyCount = 2; frpci.pDependencies = fwdDeps;
        if (vkCreateRenderPass(m_device, &frpci, nullptr, &m_forwardRenderPass) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: failed to create forward render pass");
    }
}

void VulkanDeferredPath::CreateSet1LayoutAndLightUBO()
{
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (int i = 0; i < 3; ++i)
    {
        bindings[i].binding         = (uint32_t)i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsci{};
    dsci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsci.bindingCount = 4;
    dsci.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &dsci, nullptr, &m_set1Layout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: failed to create set1 layout");

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = sizeof(DeferredLightConstants);
    bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &m_lightUBO) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: failed to create light UBO");

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, m_lightUBO, &memReq);
    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = FindMemoryTypeWithFallback(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX)
        ERROR_AND_DIE("VulkanDeferredPath: no host-visible memory for light UBO");
    if (vkAllocateMemory(m_device, &alloc, nullptr, &m_lightUBOMem) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: light UBO vkAllocateMemory failed");
    vkBindBufferMemory(m_device, m_lightUBO, m_lightUBOMem, 0);
    vkMapMemory(m_device, m_lightUBOMem, 0, sizeof(DeferredLightConstants), 0, &m_lightUBOMapped);

    DeferredLightConstants empty{};
    memcpy(m_lightUBOMapped, &empty, sizeof(empty));

    {
        VkDescriptorSetLayoutBinding fwdBinding{};
        fwdBinding.binding         = 0;
        fwdBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        fwdBinding.descriptorCount = 1;
        fwdBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo fwdDsci{};
        fwdDsci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        fwdDsci.bindingCount = 1;
        fwdDsci.pBindings    = &fwdBinding;
        if (vkCreateDescriptorSetLayout(m_device, &fwdDsci, nullptr, &m_forwardSet1Layout) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: failed to create forward set1 layout");

        VkDescriptorSetAllocateInfo a{};
        a.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        a.descriptorPool     = m_renderer->m_descriptorPool;
        a.descriptorSetCount = 1;
        a.pSetLayouts        = &m_forwardSet1Layout;
        if (vkAllocateDescriptorSets(m_device, &a, &m_forwardSet1) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: failed to allocate forward set1");

        VkDescriptorBufferInfo ubInfo{};
        ubInfo.buffer = m_lightUBO;
        ubInfo.offset = 0;
        ubInfo.range  = sizeof(DeferredLightConstants);
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = m_forwardSet1;
        w.dstBinding      = 0;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.descriptorCount = 1;
        w.pBufferInfo     = &ubInfo;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
}

uint32_t VulkanDeferredPath::FindMemoryTypeWithFallback(uint32_t typeBits, VkMemoryPropertyFlags preferred, VkMemoryPropertyFlags fallback)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_renderer->m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & preferred) == preferred)
            return i;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & fallback) == fallback)
            return i;
    return UINT32_MAX;
}

void VulkanDeferredPath::CreatePerSwapResources()
{
    const size_t numSwap = m_renderer->m_swapChainImageViews.size();
    m_perSwap.resize(numSwap);

    auto makeImg = [&](VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                       VkImage& img, VkImageView& view, VkDeviceMemory& mem)
    {
        VkImageCreateInfo ic{};
        ic.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType     = VK_IMAGE_TYPE_2D;
        ic.extent        = { m_width, m_height, 1 };
        ic.mipLevels     = 1;
        ic.arrayLayers   = 1;
        ic.format        = fmt;
        ic.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ic.usage         = usage;
        ic.samples       = VK_SAMPLE_COUNT_1_BIT;
        ic.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ic, nullptr, &img) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: vkCreateImage failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_device, img, &memReq);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = FindMemoryTypeWithFallback(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (alloc.memoryTypeIndex == UINT32_MAX)
            ERROR_AND_DIE("VulkanDeferredPath: no suitable memory type for G-buffer image");
        if (vkAllocateMemory(m_device, &alloc, nullptr, &mem) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: vkAllocateMemory failed");
        vkBindImageMemory(m_device, img, mem, 0);

        VkImageViewCreateInfo iv{};
        iv.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image        = img;
        iv.viewType     = VK_IMAGE_VIEW_TYPE_2D;
        iv.format       = fmt;
        iv.subresourceRange.aspectMask = aspect;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &iv, nullptr, &view) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: vkCreateImageView failed");
    };

    constexpr VkImageUsageFlags kColorTransient =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    constexpr VkImageUsageFlags kDepthTransient =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

    for (size_t i = 0; i < numSwap; ++i)
    {
        PerSwap& s = m_perSwap[i];

        makeImg(VK_FORMAT_R8G8B8A8_UNORM,           kColorTransient, VK_IMAGE_ASPECT_COLOR_BIT,
                s.gAlbedoImage, s.gAlbedoView, s.gAlbedoMem);
        makeImg(VK_FORMAT_A2B10G10R10_UNORM_PACK32, kColorTransient, VK_IMAGE_ASPECT_COLOR_BIT,
                s.gNormalImage, s.gNormalView, s.gNormalMem);
        makeImg(VK_FORMAT_D32_SFLOAT,               kDepthTransient, VK_IMAGE_ASPECT_DEPTH_BIT,
                s.gDepthImage,  s.gDepthView,  s.gDepthMem);

        VkImageView views[4] = {
            s.gAlbedoView, s.gNormalView, s.gDepthView,
            m_renderer->m_swapChainImageViews[i]
        };
        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = m_deferredRenderPass;
        fb.attachmentCount = 4;
        fb.pAttachments    = views;
        fb.width           = m_width;
        fb.height          = m_height;
        fb.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fb, nullptr, &s.framebuffer) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: vkCreateFramebuffer failed");

        VkImageView overlayView = m_renderer->m_swapChainImageViews[i];
        VkFramebufferCreateInfo ofb{};
        ofb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ofb.renderPass      = m_overlayRenderPass;
        ofb.attachmentCount = 1;
        ofb.pAttachments    = &overlayView;
        ofb.width           = m_width;
        ofb.height          = m_height;
        ofb.layers          = 1;
        if (vkCreateFramebuffer(m_device, &ofb, nullptr, &s.overlayFramebuffer) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: overlay vkCreateFramebuffer failed");

        // Non-transient: the forward pass stands alone, no subpass depth-readback to elide.
        {
            VkImageCreateInfo ic{};
            ic.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ic.imageType     = VK_IMAGE_TYPE_2D;
            ic.extent        = { m_width, m_height, 1 };
            ic.mipLevels     = 1; ic.arrayLayers = 1;
            ic.format        = VK_FORMAT_D32_SFLOAT;
            ic.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ic.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            ic.samples       = VK_SAMPLE_COUNT_1_BIT;
            ic.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateImage(m_device, &ic, nullptr, &s.fwdDepthImage) != VK_SUCCESS)
                ERROR_AND_DIE("VulkanDeferredPath: forward depth vkCreateImage failed");

            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(m_device, s.fwdDepthImage, &memReq);
            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize  = memReq.size;
            alloc.memoryTypeIndex = FindMemoryTypeWithFallback(memReq.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (alloc.memoryTypeIndex == UINT32_MAX)
                ERROR_AND_DIE("VulkanDeferredPath: no suitable memory for forward depth");
            if (vkAllocateMemory(m_device, &alloc, nullptr, &s.fwdDepthMem) != VK_SUCCESS)
                ERROR_AND_DIE("VulkanDeferredPath: forward depth vkAllocateMemory failed");
            vkBindImageMemory(m_device, s.fwdDepthImage, s.fwdDepthMem, 0);

            VkImageViewCreateInfo iv{};
            iv.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            iv.image        = s.fwdDepthImage;
            iv.viewType     = VK_IMAGE_VIEW_TYPE_2D;
            iv.format       = VK_FORMAT_D32_SFLOAT;
            iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            iv.subresourceRange.levelCount = 1;
            iv.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_device, &iv, nullptr, &s.fwdDepthView) != VK_SUCCESS)
                ERROR_AND_DIE("VulkanDeferredPath: forward depth vkCreateImageView failed");
        }
        VkImageView fwdViews[2] = { m_renderer->m_swapChainImageViews[i], s.fwdDepthView };
        VkFramebufferCreateInfo ffb{};
        ffb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ffb.renderPass      = m_forwardRenderPass;
        ffb.attachmentCount = 2;
        ffb.pAttachments    = fwdViews;
        ffb.width           = m_width;
        ffb.height          = m_height;
        ffb.layers          = 1;
        if (vkCreateFramebuffer(m_device, &ffb, nullptr, &s.forwardFramebuffer) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: forward vkCreateFramebuffer failed");

        VkDescriptorSetAllocateInfo a{};
        a.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        a.descriptorPool     = m_renderer->m_descriptorPool;
        a.descriptorSetCount = 1;
        a.pSetLayouts        = &m_set1Layout;
        if (vkAllocateDescriptorSets(m_device, &a, &s.set1) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: failed to allocate set1");

        VkDescriptorImageInfo imgInfos[3]{};
        imgInfos[0].imageView   = s.gAlbedoView;
        imgInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[1].imageView   = s.gNormalView;
        imgInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfos[2].imageView   = s.gDepthView;
        imgInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo ubInfo{};
        ubInfo.buffer = m_lightUBO;
        ubInfo.offset = 0;
        ubInfo.range  = sizeof(DeferredLightConstants);

        VkWriteDescriptorSet w[4]{};
        for (int j = 0; j < 3; ++j)
        {
            w[j].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[j].dstSet          = s.set1;
            w[j].dstBinding      = (uint32_t)j;
            w[j].descriptorType  = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            w[j].descriptorCount = 1;
            w[j].pImageInfo      = &imgInfos[j];
        }
        w[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[3].dstSet          = s.set1;
        w[3].dstBinding      = 3;
        w[3].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[3].descriptorCount = 1;
        w[3].pBufferInfo     = &ubInfo;

        vkUpdateDescriptorSets(m_device, 4, w, 0, nullptr);
    }
}

void VulkanDeferredPath::DestroyPerSwapResources()
{
    for (PerSwap& s : m_perSwap)
    {
        if (s.overlayFramebuffer) vkDestroyFramebuffer(m_device, s.overlayFramebuffer, nullptr);
        if (s.forwardFramebuffer) vkDestroyFramebuffer(m_device, s.forwardFramebuffer, nullptr);
        if (s.fwdDepthView)  vkDestroyImageView(m_device, s.fwdDepthView, nullptr);
        if (s.fwdDepthImage) vkDestroyImage(m_device, s.fwdDepthImage, nullptr);
        if (s.fwdDepthMem)   vkFreeMemory(m_device, s.fwdDepthMem, nullptr);
        if (s.framebuffer)  vkDestroyFramebuffer(m_device, s.framebuffer, nullptr);
        if (s.gAlbedoView)  vkDestroyImageView(m_device, s.gAlbedoView, nullptr);
        if (s.gAlbedoImage) vkDestroyImage(m_device, s.gAlbedoImage, nullptr);
        if (s.gAlbedoMem)   vkFreeMemory(m_device, s.gAlbedoMem, nullptr);
        if (s.gNormalView)  vkDestroyImageView(m_device, s.gNormalView, nullptr);
        if (s.gNormalImage) vkDestroyImage(m_device, s.gNormalImage, nullptr);
        if (s.gNormalMem)   vkFreeMemory(m_device, s.gNormalMem, nullptr);
        if (s.gDepthView)   vkDestroyImageView(m_device, s.gDepthView, nullptr);
        if (s.gDepthImage)  vkDestroyImage(m_device, s.gDepthImage, nullptr);
        if (s.gDepthMem)    vkFreeMemory(m_device, s.gDepthMem, nullptr);
        // (descriptor sets are freed implicitly with the pool)
    }
    m_perSwap.clear();
}

VkShaderModule VulkanDeferredPath::LoadShaderModule(const char* spvPath)
{
    std::ifstream f(spvPath, std::ios::ate | std::ios::binary);
    if (!f.is_open())
    {
        DebuggerPrintf("VulkanDeferredPath: shader file missing: %s\n", spvPath);
        return VK_NULL_HANDLE;
    }
    size_t fileSize = (size_t)f.tellg();
    std::vector<char> code(fileSize);
    f.seekg(0);
    f.read(code.data(), fileSize);

    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &info, nullptr, &mod) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: vkCreateShaderModule failed");
    return mod;
}

void VulkanDeferredPath::CreatePipelines()
{
    {
        VkShaderModule vs = LoadShaderModule("Data/Shaders/Vulkan/deferred/gbuffer.vert.spv");
        VkShaderModule fs = LoadShaderModule("Data/Shaders/Vulkan/deferred/gbuffer.frag.spv");
        if (!vs || !fs) ERROR_AND_DIE("VulkanDeferredPath: gbuffer shaders missing");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs; stages[1].pName = "main";

        VkVertexInputBindingDescription vbBinding{};
        vbBinding.binding = 0; vbBinding.stride = sizeof(Vertex_PCU); vbBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription vbAttrs[3]{};
        vbAttrs[0].binding = 0; vbAttrs[0].location = 0; vbAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[0].offset = offsetof(Vertex_PCU, m_position);
        vbAttrs[1].binding = 0; vbAttrs[1].location = 1; vbAttrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        vbAttrs[1].offset = offsetof(Vertex_PCU, m_color);
        vbAttrs[2].binding = 0; vbAttrs[2].location = 2; vbAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        vbAttrs[2].offset = offsetof(Vertex_PCU, m_uvTextColors);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &vbBinding;
        vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = vbAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cb[2]{};
        for (int i = 0; i < 2; ++i)
        {
            cb[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                 | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cb[i].blendEnable    = VK_FALSE;
        }
        VkPipelineColorBlendStateCreateInfo cbi{};
        cbi.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbi.attachmentCount = 2; cbi.pAttachments = cb;

        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pc.offset     = 0; pc.size = sizeof(VulkanRenderer::VkMaterialPC);

        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &m_renderer->m_descriptorSetLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pc;
        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_gbufferPipelineLayout) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: gbuffer pipeline layout failed");

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount          = 2; gpci.pStages = stages;
        gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms; gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbi; gpci.pDynamicState     = &dyn;
        gpci.layout              = m_gbufferPipelineLayout;
        gpci.renderPass          = m_deferredRenderPass;
        gpci.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_gbufferPipeline) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: gbuffer vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    }

    {
        VkShaderModule vs = LoadShaderModule("Data/Shaders/Vulkan/deferred/gbuffer_pcutbn.vert.spv");
        VkShaderModule fs = LoadShaderModule("Data/Shaders/Vulkan/deferred/gbuffer_pcutbn.frag.spv");
        if (!vs || !fs) ERROR_AND_DIE("VulkanDeferredPath: gbuffer_pcutbn shaders missing");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkVertexInputBindingDescription vbBinding{};
        vbBinding.binding = 0; vbBinding.stride = sizeof(Vertex_PCUTBN); vbBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vbAttrs[6]{};
        vbAttrs[0].binding = 0; vbAttrs[0].location = 0; vbAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[0].offset = offsetof(Vertex_PCUTBN, m_position);
        vbAttrs[1].binding = 0; vbAttrs[1].location = 1; vbAttrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        vbAttrs[1].offset = offsetof(Vertex_PCUTBN, m_color);
        vbAttrs[2].binding = 0; vbAttrs[2].location = 2; vbAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        vbAttrs[2].offset = offsetof(Vertex_PCUTBN, m_uvTexCoords);
        vbAttrs[3].binding = 0; vbAttrs[3].location = 3; vbAttrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[3].offset = offsetof(Vertex_PCUTBN, m_tangent);
        vbAttrs[4].binding = 0; vbAttrs[4].location = 4; vbAttrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[4].offset = offsetof(Vertex_PCUTBN, m_bitangent);
        vbAttrs[5].binding = 0; vbAttrs[5].location = 5; vbAttrs[5].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[5].offset = offsetof(Vertex_PCUTBN, m_normal);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &vbBinding;
        vi.vertexAttributeDescriptionCount = 6; vi.pVertexAttributeDescriptions = vbAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cb[2]{};
        for (int i = 0; i < 2; ++i)
        {
            cb[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                 | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            cb[i].blendEnable    = VK_FALSE;
        }
        VkPipelineColorBlendStateCreateInfo cbi{};
        cbi.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbi.attachmentCount = 2; cbi.pAttachments = cb;

        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount          = 2; gpci.pStages = stages;
        gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms; gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbi; gpci.pDynamicState     = &dyn;
        gpci.layout              = m_gbufferPipelineLayout;
        gpci.renderPass          = m_deferredRenderPass;
        gpci.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_gbufferPipelinePCUTBN) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: gbuffer PCUTBN vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    }

    {
        VkShaderModule vs = LoadShaderModule("Data/Shaders/Vulkan/deferred/lighting.vert.spv");
        VkShaderModule fs = LoadShaderModule("Data/Shaders/Vulkan/deferred/lighting.frag.spv");
        if (!vs || !fs) ERROR_AND_DIE("VulkanDeferredPath: lighting shaders missing");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cb.blendEnable    = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbi{};
        cbi.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbi.attachmentCount = 1; cbi.pAttachments = &cb;

        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkDescriptorSetLayout sets[2] = { m_renderer->m_descriptorSetLayout, m_set1Layout };
        VkPipelineLayoutCreateInfo plci{};
        plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 2; plci.pSetLayouts = sets;
        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_lightingPipelineLayout) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: lighting pipeline layout failed");

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount          = 2; gpci.pStages = stages;
        gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms; gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbi; gpci.pDynamicState     = &dyn;
        gpci.layout              = m_lightingPipelineLayout;
        gpci.renderPass          = m_deferredRenderPass;
        gpci.subpass             = 1;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_lightingPipeline) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: lighting vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    }

    {
        VkShaderModule vs = LoadShaderModule("Data/Shaders/Vulkan/default.vert.spv");
        VkShaderModule fs = LoadShaderModule("Data/Shaders/Vulkan/default.frag.spv");
        if (!vs || !fs) ERROR_AND_DIE("VulkanDeferredPath: overlay shaders missing");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkVertexInputBindingDescription vbBinding{};
        vbBinding.binding = 0; vbBinding.stride = sizeof(Vertex_PCU); vbBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription vbAttrs[3]{};
        vbAttrs[0].binding = 0; vbAttrs[0].location = 0; vbAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vbAttrs[0].offset = offsetof(Vertex_PCU, m_position);
        vbAttrs[1].binding = 0; vbAttrs[1].location = 1; vbAttrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        vbAttrs[1].offset = offsetof(Vertex_PCU, m_color);
        vbAttrs[2].binding = 0; vbAttrs[2].location = 2; vbAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        vbAttrs[2].offset = offsetof(Vertex_PCU, m_uvTextColors);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &vbBinding;
        vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = vbAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_FALSE; ds.depthWriteEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cb.blendEnable         = VK_TRUE;
        cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb.colorBlendOp        = VK_BLEND_OP_ADD;
        cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cb.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cbi{};
        cbi.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbi.attachmentCount = 1; cbi.pAttachments = &cb;

        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount          = 2; gpci.pStages = stages;
        gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms; gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbi; gpci.pDynamicState     = &dyn;
        gpci.layout              = m_renderer->m_pipelineLayout;
        gpci.renderPass          = m_overlayRenderPass;
        gpci.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_overlayPipeline) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: overlay vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    }

    {
        VkDescriptorSetLayout sets[2] = { m_renderer->m_descriptorSetLayout, m_forwardSet1Layout };
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pc.offset     = 0; pc.size = sizeof(VulkanRenderer::VkMaterialPC);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 2; plci.pSetLayouts = sets;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pc;
        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_forwardPipelineLayout) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: forward pipeline layout failed");
    }

    auto buildForwardPipeline = [&](const char* vertSpv, bool isPcutbn, VkPipeline& out)
    {
        VkShaderModule vs = LoadShaderModule(vertSpv);
        VkShaderModule fs = LoadShaderModule(isPcutbn
            ? "Data/Shaders/Vulkan/deferred/forward_lit_pcutbn.frag.spv"
            : "Data/Shaders/Vulkan/deferred/forward_lit.frag.spv");
        if (!vs || !fs) ERROR_AND_DIE("VulkanDeferredPath: forward shaders missing");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

        VkVertexInputBindingDescription vbBinding{};
        vbBinding.binding = 0;
        vbBinding.stride  = isPcutbn ? sizeof(Vertex_PCUTBN) : sizeof(Vertex_PCU);
        vbBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription pcuAttrs[3]{};
        pcuAttrs[0].binding = 0; pcuAttrs[0].location = 0; pcuAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        pcuAttrs[0].offset = offsetof(Vertex_PCU, m_position);
        pcuAttrs[1].binding = 0; pcuAttrs[1].location = 1; pcuAttrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        pcuAttrs[1].offset = offsetof(Vertex_PCU, m_color);
        pcuAttrs[2].binding = 0; pcuAttrs[2].location = 2; pcuAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        pcuAttrs[2].offset = offsetof(Vertex_PCU, m_uvTextColors);

        VkVertexInputAttributeDescription tbnAttrs[6]{};
        tbnAttrs[0].binding = 0; tbnAttrs[0].location = 0; tbnAttrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        tbnAttrs[0].offset = offsetof(Vertex_PCUTBN, m_position);
        tbnAttrs[1].binding = 0; tbnAttrs[1].location = 1; tbnAttrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
        tbnAttrs[1].offset = offsetof(Vertex_PCUTBN, m_color);
        tbnAttrs[2].binding = 0; tbnAttrs[2].location = 2; tbnAttrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        tbnAttrs[2].offset = offsetof(Vertex_PCUTBN, m_uvTexCoords);
        tbnAttrs[3].binding = 0; tbnAttrs[3].location = 3; tbnAttrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        tbnAttrs[3].offset = offsetof(Vertex_PCUTBN, m_tangent);
        tbnAttrs[4].binding = 0; tbnAttrs[4].location = 4; tbnAttrs[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        tbnAttrs[4].offset = offsetof(Vertex_PCUTBN, m_bitangent);
        tbnAttrs[5].binding = 0; tbnAttrs[5].location = 5; tbnAttrs[5].format = VK_FORMAT_R32G32B32_SFLOAT;
        tbnAttrs[5].offset = offsetof(Vertex_PCUTBN, m_normal);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &vbBinding;
        vi.vertexAttributeDescriptionCount = isPcutbn ? 6u : 3u;
        vi.pVertexAttributeDescriptions    = isPcutbn ? tbnAttrs : pcuAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cb{};
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cb.blendEnable    = VK_FALSE;
        VkPipelineColorBlendStateCreateInfo cbi{};
        cbi.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbi.attachmentCount = 1; cbi.pAttachments = &cb;

        VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount          = 2; gpci.pStages = stages;
        gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState   = &ms; gpci.pDepthStencilState  = &ds;
        gpci.pColorBlendState    = &cbi; gpci.pDynamicState     = &dyn;
        gpci.layout              = m_forwardPipelineLayout;
        gpci.renderPass          = m_forwardRenderPass;
        gpci.subpass             = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &out) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanDeferredPath: forward vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    };

    buildForwardPipeline("Data/Shaders/Vulkan/deferred/gbuffer.vert.spv",        false, m_forwardPipeline);
    buildForwardPipeline("Data/Shaders/Vulkan/deferred/gbuffer_pcutbn.vert.spv", true,  m_forwardPipelinePCUTBN);
}

void VulkanDeferredPath::CreateQueryPool()
{
    VkQueryPoolCreateInfo qpci{};
    qpci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = 10; // 5 per in-flight frame slot, MAX_FRAMES_IN_FLIGHT = 2
    if (vkCreateQueryPool(m_device, &qpci, nullptr, &m_queryPool) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanDeferredPath: vkCreateQueryPool failed");
}

void VulkanDeferredPath::ResetSlotAndReadPrevious(VkCommandBuffer cmd, TimingMode newMode)
{
    const size_t f = m_renderer->m_currentFrame;
    const uint32_t base = (uint32_t)(f * 5);

    // Read this slot's previous values (from 2 frames ago) before reset+rewrite.
    if (m_perSlotMode[f] == TimingMode::Deferred)
    {
        uint64_t ts[3] = {};
        if (vkGetQueryPoolResults(m_device, m_queryPool, base, 3,
                sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            m_lastGbufMs      = double(ts[1] - ts[0]) * m_timestampPeriodNs / 1.0e6;
            m_lastLightMs     = double(ts[2] - ts[1]) * m_timestampPeriodNs / 1.0e6;
            m_timestampsValid = true;
        }
    }
    else if (m_perSlotMode[f] == TimingMode::Forward)
    {
        uint64_t ts[2] = {};
        if (vkGetQueryPoolResults(m_device, m_queryPool, base + 3, 2,
                sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            m_lastForwardMs      = double(ts[1] - ts[0]) * m_timestampPeriodNs / 1.0e6;
            m_forwardTimingValid = true;
        }
    }
    m_perSlotMode[f] = newMode;
    vkCmdResetQueryPool(cmd, m_queryPool, base, 5);
}

void VulkanDeferredPath::BeginGBuffer(VkCommandBuffer cmd, uint32_t swapImageIdx)
{
    g_currentSwapImageIdx = swapImageIdx;
    if (swapImageIdx >= m_perSwap.size())
        ERROR_AND_DIE("VulkanDeferredPath: swap image index out of range");

    PerSwap& s = m_perSwap[swapImageIdx];

    ResetSlotAndReadPrevious(cmd, TimingMode::Deferred);
    const uint32_t base = (uint32_t)(m_renderer->m_currentFrame * 5);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, base + 0);

    VkRenderPassBeginInfo rp{};
    rp.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass       = m_deferredRenderPass;
    rp.framebuffer      = s.framebuffer;
    rp.renderArea       = { {0, 0}, { m_width, m_height } };

    VkClearValue clears[4]{};
    clears[0].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
    clears[1].color = { {0.5f, 0.5f, 1.0f, 0.0f} };
    clears[2].depthStencil = { 1.0f, 0 };
    clears[3].color = { {0.05f, 0.05f, 0.07f, 1.0f} };
    rp.clearValueCount = 4; rp.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    m_renderer->m_isRenderPassActive = true;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbufferPipeline);
    m_renderer->SetPipelineOverride(m_gbufferPipeline, m_gbufferPipelinePCUTBN);
}

void VulkanDeferredPath::EndGBufferAndRunLighting(VkCommandBuffer cmd, const DeferredLightConstants& lights)
{
    const uint32_t base = (uint32_t)(m_renderer->m_currentFrame * 5);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, base + 1);

    m_renderer->SetPipelineOverride(VK_NULL_HANDLE, VK_NULL_HANDLE);

    memcpy(m_lightUBOMapped, &lights, sizeof(DeferredLightConstants));

    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline);

    PerSwap& s = m_perSwap[g_currentSwapImageIdx];
    VkDescriptorSet sets[2] = {
        m_renderer->m_descriptorSets[m_renderer->m_currentFrame],
        s.set1
    };
    uint32_t dynOffsets[2] = {
        m_renderer->m_currentCameraDynamicOffset,
        m_renderer->m_currentModelDynamicOffset
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipelineLayout,
        0, 2, sets, 2, dynOffsets);

    VkViewport vp{};
    vp.x = 0.0f; vp.y = (float)m_height;
    vp.width = (float)m_width; vp.height = -(float)m_height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{ {0,0}, { m_width, m_height } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, base + 2);
    vkCmdEndRenderPass(cmd);

    m_renderer->m_isRenderPassActive = false;
}

void VulkanDeferredPath::BeginForwardLit(VkCommandBuffer cmd, uint32_t swapImageIdx, const DeferredLightConstants& lights)
{
    g_currentSwapImageIdx = swapImageIdx;
    if (swapImageIdx >= m_perSwap.size())
        ERROR_AND_DIE("VulkanDeferredPath::BeginForwardLit: swap index out of range");
    PerSwap& s = m_perSwap[swapImageIdx];

    ResetSlotAndReadPrevious(cmd, TimingMode::Forward);
    const uint32_t base = (uint32_t)(m_renderer->m_currentFrame * 5);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, base + 3);

    memcpy(m_lightUBOMapped, &lights, sizeof(DeferredLightConstants));

    VkRenderPassBeginInfo rp{};
    rp.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass       = m_forwardRenderPass;
    rp.framebuffer      = s.forwardFramebuffer;
    rp.renderArea       = { {0, 0}, { m_width, m_height } };
    VkClearValue clears[2]{};
    clears[0].color = { {0.05f, 0.05f, 0.07f, 1.0f} };
    clears[1].depthStencil = { 1.0f, 0 };
    rp.clearValueCount = 2; rp.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    m_renderer->m_isRenderPassActive = true;
    m_renderer->SetPipelineOverride(m_forwardPipeline, m_forwardPipelinePCUTBN, m_forwardPipelineLayout);

    // Set 1 (lights) bound once. Engine's per-draw set 0 binds via the override layout so
    // the layout-up-through-set-1 compatibility rule keeps this binding live.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_forwardPipelineLayout,
        1, 1, &m_forwardSet1, 0, nullptr);
}

void VulkanDeferredPath::EndForwardLit(VkCommandBuffer cmd)
{
    m_renderer->SetPipelineOverride(VK_NULL_HANDLE, VK_NULL_HANDLE);
    const uint32_t base = (uint32_t)(m_renderer->m_currentFrame * 5);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, base + 4);
    vkCmdEndRenderPass(cmd);
    m_renderer->m_isRenderPassActive = false;
}

bool VulkanDeferredPath::TryGetLastForwardMs(double& outForwardMs)
{
    if (!m_forwardTimingValid) return false;
    outForwardMs = m_lastForwardMs;
    return true;
}

void VulkanDeferredPath::BeginForwardOverlay(VkCommandBuffer cmd, uint32_t swapImageIdx)
{
    if (swapImageIdx >= m_perSwap.size())
        ERROR_AND_DIE("VulkanDeferredPath::BeginForwardOverlay: swap index out of range");

    PerSwap& s = m_perSwap[swapImageIdx];

    VkRenderPassBeginInfo rp{};
    rp.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass       = m_overlayRenderPass;
    rp.framebuffer      = s.overlayFramebuffer;
    rp.renderArea       = { {0, 0}, { m_width, m_height } };
    rp.clearValueCount  = 0;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    m_renderer->m_isRenderPassActive = true;
    m_renderer->SetPipelineOverride(m_overlayPipeline, VK_NULL_HANDLE);
}

void VulkanDeferredPath::EndForwardOverlay(VkCommandBuffer cmd)
{
    m_renderer->SetPipelineOverride(VK_NULL_HANDLE, VK_NULL_HANDLE);
    vkCmdEndRenderPass(cmd);
    m_renderer->m_isRenderPassActive = false;
}

bool VulkanDeferredPath::TryGetLastFrameTimings(double& outGBufferMs, double& outLightingMs)
{
    if (!m_timestampsValid) return false;
    outGBufferMs  = m_lastGbufMs;
    outLightingMs = m_lastLightMs;
    return true;
}

#endif // ENGINE_VULKAN_RENDERER
