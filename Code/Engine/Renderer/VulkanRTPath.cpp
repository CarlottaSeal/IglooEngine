#include "Engine/Renderer/VulkanRTPath.h"

#ifdef ENGINE_VULKAN_RENDERER

#include "Engine/Renderer/VulkanRenderer.h"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/StringUtils.hpp"
#include "ThirdParty/stb/stb_image.h"

#include <cstdio>
#include <cstring>
#include <vector>

void VulkanRTPath::Init(VulkanRenderer* renderer)
{
    m_renderer = renderer;
    m_device   = renderer->m_device;
    GUARANTEE_OR_DIE(m_renderer->m_supportsRayTracing,
                     "VulkanRTPath::Init: device does not expose KHR ray tracing");

    QueueFamilyIndices qf = m_renderer->FindQueueFamilies(m_renderer->m_physicalDevice);
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = qf.graphicsFamily.value();
    pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                           VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &pci, nullptr, &m_oneShotPool) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::Init: failed to create one-shot cmd pool");
}

void VulkanRTPath::Shutdown()
{
    if (!m_device) return;

    DestroyOutputImage();

    if (m_rtPipeline)     vkDestroyPipeline(m_device, m_rtPipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_setLayout)      vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    if (m_sbt.buffer) vkDestroyBuffer(m_device, m_sbt.buffer, nullptr);
    if (m_sbt.memory) vkFreeMemory(m_device, m_sbt.memory, nullptr);

    if (m_cameraUBOMem)    vkUnmapMemory(m_device, m_cameraUBOMem);
    if (m_cameraUBO)       vkDestroyBuffer(m_device, m_cameraUBO, nullptr);
    if (m_cameraUBOMem)    vkFreeMemory(m_device, m_cameraUBOMem, nullptr);
    m_cameraUBO       = VK_NULL_HANDLE;
    m_cameraUBOMem    = VK_NULL_HANDLE;
    m_cameraUBOMapped = nullptr;

    if (m_matColorsBuf) vkDestroyBuffer(m_device, m_matColorsBuf, nullptr);
    if (m_matColorsMem) vkFreeMemory(m_device, m_matColorsMem, nullptr);
    if (m_triMatIdsBuf) vkDestroyBuffer(m_device, m_triMatIdsBuf, nullptr);
    if (m_triMatIdsMem) vkFreeMemory(m_device, m_triMatIdsMem, nullptr);
    m_matColorsBuf = VK_NULL_HANDLE;
    m_matColorsMem = VK_NULL_HANDLE;
    m_triMatIdsBuf = VK_NULL_HANDLE;
    m_triMatIdsMem = VK_NULL_HANDLE;

    if (m_uvCoordsBuf)   vkDestroyBuffer(m_device, m_uvCoordsBuf, nullptr);
    if (m_uvCoordsMem)   vkFreeMemory(m_device, m_uvCoordsMem, nullptr);
    if (m_uvIdxBuf)      vkDestroyBuffer(m_device, m_uvIdxBuf, nullptr);
    if (m_uvIdxMem)      vkFreeMemory(m_device, m_uvIdxMem, nullptr);
    if (m_matTexSlotBuf)    vkDestroyBuffer(m_device, m_matTexSlotBuf, nullptr);
    if (m_matTexSlotMem)    vkFreeMemory(m_device, m_matTexSlotMem, nullptr);
    if (m_matNormalSlotBuf) vkDestroyBuffer(m_device, m_matNormalSlotBuf, nullptr);
    if (m_matNormalSlotMem) vkFreeMemory(m_device, m_matNormalSlotMem, nullptr);
    m_uvCoordsBuf      = VK_NULL_HANDLE;
    m_uvCoordsMem      = VK_NULL_HANDLE;
    m_uvIdxBuf         = VK_NULL_HANDLE;
    m_uvIdxMem         = VK_NULL_HANDLE;
    m_matTexSlotBuf    = VK_NULL_HANDLE;
    m_matTexSlotMem    = VK_NULL_HANDLE;
    m_matNormalSlotBuf = VK_NULL_HANDLE;
    m_matNormalSlotMem = VK_NULL_HANDLE;

    for (auto& t : m_textures) {
        if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
        if (t.image) vkDestroyImage(m_device, t.image, nullptr);
        if (t.mem)   vkFreeMemory(m_device, t.mem, nullptr);
    }
    m_textures.clear();
    if (m_textureSampler) vkDestroySampler(m_device, m_textureSampler, nullptr);
    m_textureSampler = VK_NULL_HANDLE;

    if (m_lightsBuf)     vkDestroyBuffer(m_device, m_lightsBuf, nullptr);
    if (m_lightsMem)     vkFreeMemory(m_device, m_lightsMem, nullptr);
    if (m_reservoirBuf)  vkDestroyBuffer(m_device, m_reservoirBuf, nullptr);
    if (m_reservoirMem)  vkFreeMemory(m_device, m_reservoirMem, nullptr);
    if (m_reservoirBuf2) vkDestroyBuffer(m_device, m_reservoirBuf2, nullptr);
    if (m_reservoirMem2) vkFreeMemory(m_device, m_reservoirMem2, nullptr);
    m_lightsBuf     = VK_NULL_HANDLE;
    m_lightsMem     = VK_NULL_HANDLE;
    m_reservoirBuf  = VK_NULL_HANDLE;
    m_reservoirMem  = VK_NULL_HANDLE;
    m_reservoirBuf2 = VK_NULL_HANDLE;
    m_reservoirMem2 = VK_NULL_HANDLE;

    if (m_atrousPipeline)         vkDestroyPipeline(m_device, m_atrousPipeline, nullptr);
    if (m_atrousPipelineLayout)   vkDestroyPipelineLayout(m_device, m_atrousPipelineLayout, nullptr);
    if (m_atrousDescSetLayout)    vkDestroyDescriptorSetLayout(m_device, m_atrousDescSetLayout, nullptr);
    if (m_atrousDescPool)         vkDestroyDescriptorPool(m_device, m_atrousDescPool, nullptr);
    if (m_compositePipeline)      vkDestroyPipeline(m_device, m_compositePipeline, nullptr);
    if (m_compositePipelineLayout)vkDestroyPipelineLayout(m_device, m_compositePipelineLayout, nullptr);
    if (m_compositeDescSetLayout) vkDestroyDescriptorSetLayout(m_device, m_compositeDescSetLayout, nullptr);
    if (m_compositeDescPool)      vkDestroyDescriptorPool(m_device, m_compositeDescPool, nullptr);
    m_atrousPipeline           = VK_NULL_HANDLE;
    m_atrousPipelineLayout     = VK_NULL_HANDLE;
    m_atrousDescSetLayout      = VK_NULL_HANDLE;
    m_atrousDescPool           = VK_NULL_HANDLE;
    m_compositePipeline        = VK_NULL_HANDLE;
    m_compositePipelineLayout  = VK_NULL_HANDLE;
    m_compositeDescSetLayout   = VK_NULL_HANDLE;
    m_compositeDescPool        = VK_NULL_HANDLE;

    if (m_oneShotPool) vkDestroyCommandPool(m_device, m_oneShotPool, nullptr);
    m_oneShotPool = VK_NULL_HANDLE;

    m_renderer = nullptr;
    m_device   = VK_NULL_HANDLE;
}

void VulkanRTPath::OnSwapchainResized()
{
}

VulkanBLAS VulkanRTPath::BuildBLAS(const float*    vertexPositions,
                                   uint32_t        vertexCount,
                                   const uint32_t* indices,
                                   uint32_t        indexCount)
{
    VulkanBLAS blas{};
    const VkDeviceSize vertexBufSize = vertexCount * sizeof(float) * 3;
    const VkDeviceSize indexBufSize  = indexCount * sizeof(uint32_t);

    const VkBufferUsageFlags asInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkMemoryPropertyFlags hostCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    CreateAndAllocateBuffer(vertexBufSize,
                            asInputUsage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            hostCoherent,
                            blas.vertexBuf, blas.vertexMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, blas.vertexMem, 0, vertexBufSize, 0, &mapped);
        memcpy(mapped, vertexPositions, (size_t)vertexBufSize);
        vkUnmapMemory(m_device, blas.vertexMem);
    }

    CreateAndAllocateBuffer(indexBufSize,
                            asInputUsage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            hostCoherent,
                            blas.indexBuf, blas.indexMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, blas.indexMem, 0, indexBufSize, 0, &mapped);
        memcpy(mapped, indices, (size_t)indexBufSize);
        vkUnmapMemory(m_device, blas.indexMem);
    }

    const VkDeviceAddress vbAddr = GetBufferDeviceAddress(blas.vertexBuf);
    const VkDeviceAddress ibAddr = GetBufferDeviceAddress(blas.indexBuf);

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat                = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress    = vbAddr;
    triangles.vertexStride                = sizeof(float) * 3;
    triangles.maxVertex                   = vertexCount - 1;
    triangles.indexType                   = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress     = ibAddr;
    triangles.transformData.deviceAddress = 0;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType           = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags                  = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles     = triangles;

    const uint32_t primitiveCount = indexCount / 3;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_renderer->pfnGetAccelerationStructureBuildSizesKHR(
        m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizes);

    CreateAndAllocateBuffer(sizes.accelerationStructureSize,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            blas.asBuffer, blas.asMemory);

    VkAccelerationStructureCreateInfoKHR asci{};
    asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asci.buffer = blas.asBuffer;
    asci.size   = sizes.accelerationStructureSize;
    if (m_renderer->pfnCreateAccelerationStructureKHR(m_device, &asci, nullptr, &blas.handle) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::BuildBLAS: pfnCreateAccelerationStructureKHR failed");

    VkBuffer       scratchBuf = VK_NULL_HANDLE;
    VkDeviceMemory scratchMem = VK_NULL_HANDLE;
    CreateAndAllocateBuffer(sizes.buildScratchSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            scratchBuf, scratchMem);
    const VkDeviceAddress scratchAddr = GetBufferDeviceAddress(scratchBuf);

    buildInfo.dstAccelerationStructure   = blas.handle;
    buildInfo.scratchData.deviceAddress  = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount  = primitiveCount;
    range.primitiveOffset = 0;
    range.firstVertex     = 0;
    range.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;

    VkCommandBuffer cmd = BeginOneShotCmd();
    m_renderer->pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &rangePtr);
    EndAndSubmitOneShotCmd(cmd);

    blas.address = GetASDeviceAddress(blas.handle);

    vkDestroyBuffer(m_device, scratchBuf, nullptr);
    vkFreeMemory(m_device, scratchMem, nullptr);

    return blas;
}

VulkanTLAS VulkanRTPath::BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& instances)
{
    VulkanTLAS tlas{};
    tlas.instanceCount = (uint32_t)instances.size();

    const VkDeviceSize instBufSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();

    CreateAndAllocateBuffer(instBufSize,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            tlas.instBuffer, tlas.instMemory);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, tlas.instMemory, 0, instBufSize, 0, &mapped);
        memcpy(mapped, instances.data(), (size_t)instBufSize);
        vkUnmapMemory(m_device, tlas.instMemory);
    }
    const VkDeviceAddress instAddr = GetBufferDeviceAddress(tlas.instBuffer);

    VkAccelerationStructureGeometryInstancesDataKHR instData{};
    instData.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData.arrayOfPointers    = VK_FALSE;
    instData.data.deviceAddress = instAddr;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType                  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType           = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags                  = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances     = instData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries   = &geometry;

    const uint32_t primitiveCount = tlas.instanceCount;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_renderer->pfnGetAccelerationStructureBuildSizesKHR(
        m_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizes);

    CreateAndAllocateBuffer(sizes.accelerationStructureSize,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            tlas.buffer, tlas.memory);

    VkAccelerationStructureCreateInfoKHR asci{};
    asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    asci.buffer = tlas.buffer;
    asci.size   = sizes.accelerationStructureSize;
    if (m_renderer->pfnCreateAccelerationStructureKHR(m_device, &asci, nullptr, &tlas.handle) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::BuildTLAS: pfnCreateAccelerationStructureKHR failed");

    VkBuffer       scratchBuf = VK_NULL_HANDLE;
    VkDeviceMemory scratchMem = VK_NULL_HANDLE;
    CreateAndAllocateBuffer(sizes.buildScratchSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            scratchBuf, scratchMem);
    const VkDeviceAddress scratchAddr = GetBufferDeviceAddress(scratchBuf);

    buildInfo.dstAccelerationStructure  = tlas.handle;
    buildInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = tlas.instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;

    VkCommandBuffer cmd = BeginOneShotCmd();
    m_renderer->pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &rangePtr);
    EndAndSubmitOneShotCmd(cmd);

    tlas.address = GetASDeviceAddress(tlas.handle);

    vkDestroyBuffer(m_device, scratchBuf, nullptr);
    vkFreeMemory(m_device, scratchMem, nullptr);

    return tlas;
}

void VulkanRTPath::DestroyBLAS(VulkanBLAS& blas)
{
    if (blas.handle && m_renderer->pfnDestroyAccelerationStructureKHR)
        m_renderer->pfnDestroyAccelerationStructureKHR(m_device, blas.handle, nullptr);
    if (blas.asBuffer)  vkDestroyBuffer(m_device, blas.asBuffer, nullptr);
    if (blas.asMemory)  vkFreeMemory(m_device, blas.asMemory, nullptr);
    if (blas.vertexBuf) vkDestroyBuffer(m_device, blas.vertexBuf, nullptr);
    if (blas.vertexMem) vkFreeMemory(m_device, blas.vertexMem, nullptr);
    if (blas.indexBuf)  vkDestroyBuffer(m_device, blas.indexBuf, nullptr);
    if (blas.indexMem)  vkFreeMemory(m_device, blas.indexMem, nullptr);
    blas = {};
}

void VulkanRTPath::DestroyTLAS(VulkanTLAS& tlas)
{
    if (tlas.handle && m_renderer->pfnDestroyAccelerationStructureKHR)
        m_renderer->pfnDestroyAccelerationStructureKHR(m_device, tlas.handle, nullptr);
    if (tlas.buffer)     vkDestroyBuffer(m_device, tlas.buffer, nullptr);
    if (tlas.memory)     vkFreeMemory(m_device, tlas.memory, nullptr);
    if (tlas.instBuffer) vkDestroyBuffer(m_device, tlas.instBuffer, nullptr);
    if (tlas.instMemory) vkFreeMemory(m_device, tlas.instMemory, nullptr);
    tlas = {};
}

static std::vector<char> LoadFile(const char* path)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) ERROR_AND_DIE(Stringf("VulkanRTPath: failed to open '%s'", path));
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

static VkShaderModule CreateShaderModuleFromSPV(VkDevice device, const std::vector<char>& code)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath: vkCreateShaderModule failed");
    return mod;
}

void VulkanRTPath::CreateRTPipeline(const char* rgenSpvPath,
                                    const char* rchitSpvPath,
                                    const char* rmissSpvPath,
                                    const char* rshadowMissSpvPath)
{
    // 0: storage image, 1: TLAS, 2: camera UBO,
    // 3: VB, 4: IB, 5: matColors, 6: triMatIds,
    // 7: bindless textures, 8: matTexSlot, 9: uvCoords, 10: uvIndices,
    // 11: matNormalSlot, 12: lights, 13/14: reservoirs ping-pong,
    // 15: TAA history, 16: albedo G-buffer, 17: SVGF moments,
    // 18: per-pixel material id.
    constexpr uint32_t kBindingCount = 19;
    VkDescriptorSetLayoutBinding bindings[kBindingCount]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    for (int b = 3; b <= 6; ++b) {
        bindings[b].binding         = (uint32_t)b;
        bindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    }

    bindings[7].binding         = 7;
    bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = kMaxRTTextures;
    bindings[7].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    for (int b = 8; b <= 14; ++b) {
        bindings[b].binding         = (uint32_t)b;
        bindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    }
    // Reservoirs (13, 14) also visible to raygen so the post-TAA spatial
    // filter can sample neighbor normals/depths without a separate G-buffer.
    bindings[13].stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[14].stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[15].binding         = 15;
    bindings[15].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[15].descriptorCount = 1;
    bindings[15].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    // Albedo G-buffer: closesthit writes baseColor here; raygen multiplies
    // it back after spatially filtering the un-modulated lighting.
    // Miss writes 1.0 so sky pixels survive the multiply.
    bindings[16].binding         = 16;
    bindings[16].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[16].descriptorCount = 1;
    bindings[16].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                                 | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                                 | VK_SHADER_STAGE_MISS_BIT_KHR;

    // SVGF moments — raygen-only (read prev at reproj, write new).
    bindings[17].binding         = 17;
    bindings[17].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[17].descriptorCount = 1;
    bindings[17].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    // matId G-buffer (closesthit writes id+1, miss writes 0, raygen reads
    // for reprojection-rejection on material boundaries).
    bindings[18].binding         = 18;
    bindings[18].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[18].descriptorCount = 1;
    bindings[18].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                                 | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                                 | VK_SHADER_STAGE_MISS_BIT_KHR;

    // Texture array slots may be unbound (Sponza has fewer than kMaxRTTextures);
    // PARTIALLY_BOUND_BIT lets validation accept that.
    VkDescriptorBindingFlags bindingFlags[kBindingCount] = {};
    bindingFlags[7] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsCi{};
    bindingFlagsCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
    bindingFlagsCi.bindingCount  = kBindingCount;
    bindingFlagsCi.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.pNext        = &bindingFlagsCi;
    dsl.bindingCount = kBindingCount;
    dsl.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &dsl, nullptr, &m_setLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkCreateDescriptorSetLayout failed");

    VkPipelineLayoutCreateInfo plci{};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_setLayout;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkCreatePipelineLayout failed");

    auto rgenSpv      = LoadFile(rgenSpvPath);
    auto rchitSpv     = LoadFile(rchitSpvPath);
    auto rmissSpv     = LoadFile(rmissSpvPath);
    auto rshadowSpv   = LoadFile(rshadowMissSpvPath);
    VkShaderModule rgen        = CreateShaderModuleFromSPV(m_device, rgenSpv);
    VkShaderModule rchit       = CreateShaderModuleFromSPV(m_device, rchitSpv);
    VkShaderModule rmiss       = CreateShaderModuleFromSPV(m_device, rmissSpv);
    VkShaderModule rshadowMiss = CreateShaderModuleFromSPV(m_device, rshadowSpv);

    VkPipelineShaderStageCreateInfo stages[4]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[0].module = rgen;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[1].module = rchit;
    stages[1].pName  = "main";
    stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[2].module = rmiss;
    stages[2].pName  = "main";
    stages[3].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[3].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[3].module = rshadowMiss;
    stages[3].pName  = "main";

    // Groups: rgen / triangles-hit / primary-miss / shadow-miss.
    VkRayTracingShaderGroupCreateInfoKHR groups[4]{};
    for (int i = 0; i < 4; ++i) {
        groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[i].generalShader      = VK_SHADER_UNUSED_KHR;
        groups[i].closestHitShader   = VK_SHADER_UNUSED_KHR;
        groups[i].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader    = 0;
    groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[1].closestHitShader = 1;
    groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[2].generalShader    = 2;
    groups[3].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[3].generalShader    = 3;

    VkRayTracingPipelineCreateInfoKHR rtci{};
    rtci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtci.stageCount                   = 4;
    rtci.pStages                      = stages;
    rtci.groupCount                   = 4;
    rtci.pGroups                      = groups;
    rtci.maxPipelineRayRecursionDepth = 2;
    rtci.layout                       = m_pipelineLayout;
    if (m_renderer->pfnCreateRayTracingPipelinesKHR(
            m_device, VK_NULL_HANDLE, VK_NULL_HANDLE,
            1, &rtci, nullptr, &m_rtPipeline) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: pfnCreateRayTracingPipelinesKHR failed");

    vkDestroyShaderModule(m_device, rgen,        nullptr);
    vkDestroyShaderModule(m_device, rchit,       nullptr);
    vkDestroyShaderModule(m_device, rmiss,       nullptr);
    vkDestroyShaderModule(m_device, rshadowMiss, nullptr);

    // 9 vec4: 5 current (4 cam basis + 1 misc) + 4 prev frame cam (no misc).
    const VkDeviceSize cameraUBOSize = sizeof(float) * 4 * 9;
    CreateAndAllocateBuffer(cameraUBOSize,
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_cameraUBO, m_cameraUBOMem);
    vkMapMemory(m_device, m_cameraUBOMem, 0, cameraUBOSize, 0, &m_cameraUBOMapped);

    VkDescriptorPoolSize poolSizes[5]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;              poolSizes[0].descriptorCount = 5;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; poolSizes[1].descriptorCount = 1;
    poolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;             poolSizes[2].descriptorCount = 1;
    poolSizes[3].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;             poolSizes[3].descriptorCount = 11;
    poolSizes[4].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;     poolSizes[4].descriptorCount = kMaxRTTextures;

    VkDescriptorPoolCreateInfo poolCi{};
    poolCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets       = 1;
    poolCi.poolSizeCount = 5;
    poolCi.pPoolSizes    = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_descriptorPool) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkCreateDescriptorPool failed");

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool     = m_descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts        = &m_setLayout;
    if (vkAllocateDescriptorSets(m_device, &dsAlloc, &m_descriptorSet) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkAllocateDescriptorSets failed");
}

void VulkanRTPath::CreateSBT()
{
    // Each region's base aligned to shaderGroupBaseAlignment; entries within
    // a region stride by alignUp(handleSize, handleAlignment).
    const uint32_t handleSize   = m_renderer->m_rtProperties.shaderGroupHandleSize;
    const uint32_t handleAlign  = m_renderer->m_rtProperties.shaderGroupHandleAlignment;
    const uint32_t baseAlign    = m_renderer->m_rtProperties.shaderGroupBaseAlignment;
    auto alignUp = [](uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); };
    const uint32_t handleStride = alignUp(handleSize, handleAlign);

    const uint32_t raygenSize  = handleStride;
    const uint32_t missSize    = handleStride * 2;
    const uint32_t hitSize     = handleStride;

    const uint32_t raygenOffset = 0;
    const uint32_t missOffset   = alignUp(raygenOffset + raygenSize, baseAlign);
    const uint32_t hitOffset    = alignUp(missOffset   + missSize,   baseAlign);
    const VkDeviceSize sbtSize  = hitOffset + hitSize;

    std::vector<uint8_t> handleStorage(handleSize * 4);
    if (m_renderer->pfnGetRayTracingShaderGroupHandlesKHR(
            m_device, m_rtPipeline, 0, 4,
            handleStorage.size(), handleStorage.data()) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateSBT: pfnGetRayTracingShaderGroupHandlesKHR failed");

    CreateAndAllocateBuffer(sbtSize,
                            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_sbt.buffer, m_sbt.memory);

    void* mapped = nullptr;
    vkMapMemory(m_device, m_sbt.memory, 0, sbtSize, 0, &mapped);
    uint8_t* dst = (uint8_t*)mapped;
    memcpy(dst + raygenOffset,                  handleStorage.data() + 0 * handleSize, handleSize);
    memcpy(dst + missOffset + 0 * handleStride, handleStorage.data() + 2 * handleSize, handleSize);
    memcpy(dst + missOffset + 1 * handleStride, handleStorage.data() + 3 * handleSize, handleSize);
    memcpy(dst + hitOffset,                     handleStorage.data() + 1 * handleSize, handleSize);
    vkUnmapMemory(m_device, m_sbt.memory);

    const VkDeviceAddress sbtAddr = GetBufferDeviceAddress(m_sbt.buffer);

    // raygen.size MUST equal stride per spec.
    m_sbt.raygenRegion = { sbtAddr + raygenOffset, handleStride, raygenSize };
    m_sbt.missRegion   = { sbtAddr + missOffset,   handleStride, missSize };
    m_sbt.hitRegion    = { sbtAddr + hitOffset,    handleStride, hitSize };
    m_sbt.callableRegion = {};
}

void VulkanRTPath::SetMaterialBuffers(const float*    matColorsRGB, uint32_t numMaterials,
                                      const uint32_t* triMatIds,    uint32_t numTriangles)
{
    if (m_matColorsBuf) { vkDestroyBuffer(m_device, m_matColorsBuf, nullptr); m_matColorsBuf = VK_NULL_HANDLE; }
    if (m_matColorsMem) { vkFreeMemory(m_device, m_matColorsMem, nullptr);    m_matColorsMem = VK_NULL_HANDLE; }
    if (m_triMatIdsBuf) { vkDestroyBuffer(m_device, m_triMatIdsBuf, nullptr); m_triMatIdsBuf = VK_NULL_HANDLE; }
    if (m_triMatIdsMem) { vkFreeMemory(m_device, m_triMatIdsMem, nullptr);    m_triMatIdsMem = VK_NULL_HANDLE; }

    const VkMemoryPropertyFlags hostCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const VkDeviceSize matSize = numMaterials * sizeof(float) * 3;
    CreateAndAllocateBuffer(matSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            hostCoherent,
                            m_matColorsBuf, m_matColorsMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_matColorsMem, 0, matSize, 0, &mapped);
        memcpy(mapped, matColorsRGB, (size_t)matSize);
        vkUnmapMemory(m_device, m_matColorsMem);
    }

    const VkDeviceSize idSize = numTriangles * sizeof(uint32_t);
    CreateAndAllocateBuffer(idSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            hostCoherent,
                            m_triMatIdsBuf, m_triMatIdsMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_triMatIdsMem, 0, idSize, 0, &mapped);
        memcpy(mapped, triMatIds, (size_t)idSize);
        vkUnmapMemory(m_device, m_triMatIdsMem);
    }
}

void VulkanRTPath::SetUVs(const float* uvCoords, uint32_t numUVs,
                          const uint32_t* uvIndices, uint32_t numTriangles)
{
    if (m_uvCoordsBuf) { vkDestroyBuffer(m_device, m_uvCoordsBuf, nullptr); m_uvCoordsBuf = VK_NULL_HANDLE; }
    if (m_uvCoordsMem) { vkFreeMemory(m_device, m_uvCoordsMem, nullptr);    m_uvCoordsMem = VK_NULL_HANDLE; }
    if (m_uvIdxBuf)    { vkDestroyBuffer(m_device, m_uvIdxBuf,    nullptr); m_uvIdxBuf    = VK_NULL_HANDLE; }
    if (m_uvIdxMem)    { vkFreeMemory(m_device, m_uvIdxMem,    nullptr);    m_uvIdxMem    = VK_NULL_HANDLE; }

    const VkMemoryPropertyFlags hostCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    const VkDeviceSize uvSize = numUVs * sizeof(float) * 2;
    CreateAndAllocateBuffer(uvSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            hostCoherent,
                            m_uvCoordsBuf, m_uvCoordsMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_uvCoordsMem, 0, uvSize, 0, &mapped);
        memcpy(mapped, uvCoords, (size_t)uvSize);
        vkUnmapMemory(m_device, m_uvCoordsMem);
    }

    const VkDeviceSize idxSize = numTriangles * 3 * sizeof(uint32_t);
    CreateAndAllocateBuffer(idxSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            hostCoherent,
                            m_uvIdxBuf, m_uvIdxMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_uvIdxMem, 0, idxSize, 0, &mapped);
        memcpy(mapped, uvIndices, (size_t)idxSize);
        vkUnmapMemory(m_device, m_uvIdxMem);
    }
}

void VulkanRTPath::SetTextures(const std::vector<std::string>& texturePaths,
                               const int32_t* matDiffuseSlot,
                               const int32_t* matNormalSlot,
                               uint32_t numMaterials)
{
    GUARANTEE_OR_DIE(texturePaths.size() <= kMaxRTTextures,
                     "VulkanRTPath::SetTextures: texture count exceeds kMaxRTTextures");

    for (auto& t : m_textures) {
        if (t.view)  vkDestroyImageView(m_device, t.view, nullptr);
        if (t.image) vkDestroyImage(m_device, t.image, nullptr);
        if (t.mem)   vkFreeMemory(m_device, t.mem, nullptr);
    }
    m_textures.clear();
    if (m_matTexSlotBuf)    { vkDestroyBuffer(m_device, m_matTexSlotBuf, nullptr);    m_matTexSlotBuf    = VK_NULL_HANDLE; }
    if (m_matTexSlotMem)    { vkFreeMemory(m_device, m_matTexSlotMem, nullptr);       m_matTexSlotMem    = VK_NULL_HANDLE; }
    if (m_matNormalSlotBuf) { vkDestroyBuffer(m_device, m_matNormalSlotBuf, nullptr); m_matNormalSlotBuf = VK_NULL_HANDLE; }
    if (m_matNormalSlotMem) { vkFreeMemory(m_device, m_matNormalSlotMem, nullptr);    m_matNormalSlotMem = VK_NULL_HANDLE; }

    if (!m_textureSampler)
    {
        VkSamplerCreateInfo sci{};
        sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter    = VK_FILTER_LINEAR;
        sci.minFilter    = VK_FILTER_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.maxLod       = VK_LOD_CLAMP_NONE;
        if (vkCreateSampler(m_device, &sci, nullptr, &m_textureSampler) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanRTPath::SetTextures: vkCreateSampler failed");
    }

    for (const std::string& path : texturePaths)
    {
        int w = 0, h = 0, ch = 0;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!data || w <= 0 || h <= 0)
            ERROR_AND_DIE(Stringf("VulkanRTPath::SetTextures: stbi_load failed for '%s' (%s)",
                                  path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "unknown"));
        const VkDeviceSize imgSize = (VkDeviceSize)w * (VkDeviceSize)h * 4;

        VkBuffer       staging    = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        CreateAndAllocateBuffer(imgSize,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                staging, stagingMem);
        {
            void* mapped = nullptr;
            vkMapMemory(m_device, stagingMem, 0, imgSize, 0, &mapped);
            memcpy(mapped, data, (size_t)imgSize);
            vkUnmapMemory(m_device, stagingMem);
        }
        stbi_image_free(data);

        LoadedTexture lt;
        VkImageCreateInfo ic{};
        ic.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType     = VK_IMAGE_TYPE_2D;
        ic.format        = VK_FORMAT_R8G8B8A8_SRGB;
        ic.extent        = { (uint32_t)w, (uint32_t)h, 1 };
        ic.mipLevels     = 1;
        ic.arrayLayers   = 1;
        ic.samples       = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ic.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ic.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ic, nullptr, &lt.image) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanRTPath::SetTextures: vkCreateImage failed");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_device, lt.image, &memReq);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &alloc, nullptr, &lt.mem) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanRTPath::SetTextures: vkAllocateMemory failed");
        vkBindImageMemory(m_device, lt.image, lt.mem, 0);

        VkImageViewCreateInfo iv{};
        iv.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image      = lt.image;
        iv.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        iv.format     = VK_FORMAT_R8G8B8A8_SRGB;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &iv, nullptr, &lt.view) != VK_SUCCESS)
            ERROR_AND_DIE("VulkanRTPath::SetTextures: vkCreateImageView failed");

        VkCommandBuffer cmd = BeginOneShotCmd();
        {
            VkImageMemoryBarrier b{};
            b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image            = lt.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask    = 0;
            b.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging, lt.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        {
            VkImageMemoryBarrier b{};
            b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image            = lt.image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        EndAndSubmitOneShotCmd(cmd);

        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, stagingMem, nullptr);

        m_textures.push_back(lt);
    }

    const VkDeviceSize matSize = numMaterials * sizeof(int32_t);
    CreateAndAllocateBuffer(matSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_matTexSlotBuf, m_matTexSlotMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_matTexSlotMem, 0, matSize, 0, &mapped);
        memcpy(mapped, matDiffuseSlot, (size_t)matSize);
        vkUnmapMemory(m_device, m_matTexSlotMem);
    }

    CreateAndAllocateBuffer(matSize,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_matNormalSlotBuf, m_matNormalSlotMem);
    {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_matNormalSlotMem, 0, matSize, 0, &mapped);
        memcpy(mapped, matNormalSlot, (size_t)matSize);
        vkUnmapMemory(m_device, m_matNormalSlotMem);
    }
}

void VulkanRTPath::SetLights(const float* lightData, uint32_t numLights)
{
    if (m_lightsBuf) { vkDestroyBuffer(m_device, m_lightsBuf, nullptr); m_lightsBuf = VK_NULL_HANDLE; }
    if (m_lightsMem) { vkFreeMemory(m_device, m_lightsMem, nullptr);    m_lightsMem = VK_NULL_HANDLE; }

    const VkDeviceSize size = (VkDeviceSize)numLights * sizeof(float) * 8;   // 2 vec4 per light
    CreateAndAllocateBuffer(size,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_lightsBuf, m_lightsMem);
    void* mapped = nullptr;
    vkMapMemory(m_device, m_lightsMem, 0, size, 0, &mapped);
    memcpy(mapped, lightData, (size_t)size);
    vkUnmapMemory(m_device, m_lightsMem);
}

void VulkanRTPath::UpdateDescriptors(const VulkanTLAS& tlas, const VulkanBLAS& geomBLAS)
{
    m_lastBoundTLAS = tlas.handle;

    VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
    asWrite.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures    = &tlas.handle;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = m_outputImageView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = m_cameraUBO;
    uboInfo.offset = 0;
    uboInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo vbInfo{};
    vbInfo.buffer = geomBLAS.vertexBuf;
    vbInfo.offset = 0;
    vbInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo ibInfo{};
    ibInfo.buffer = geomBLAS.indexBuf;
    ibInfo.offset = 0;
    ibInfo.range  = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[7]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_descriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo      = &imgInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].pNext           = &asWrite;
    writes[1].dstSet          = m_descriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet          = m_descriptorSet;
    writes[2].dstBinding      = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo     = &uboInfo;

    writes[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet          = m_descriptorSet;
    writes[3].dstBinding      = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo     = &vbInfo;

    writes[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet          = m_descriptorSet;
    writes[4].dstBinding      = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo     = &ibInfo;

    VkDescriptorBufferInfo matInfo{};
    matInfo.buffer = m_matColorsBuf;
    matInfo.offset = 0;
    matInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo triMatInfo{};
    triMatInfo.buffer = m_triMatIdsBuf;
    triMatInfo.offset = 0;
    triMatInfo.range  = VK_WHOLE_SIZE;

    writes[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet          = m_descriptorSet;
    writes[5].dstBinding      = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo     = &matInfo;

    writes[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet          = m_descriptorSet;
    writes[6].dstBinding      = 6;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].pBufferInfo     = &triMatInfo;

    vkUpdateDescriptorSets(m_device, 7, writes, 0, nullptr);

    // Bindings 7..10 are owned by SetTextures / SetUVs and may be unbound
    // until those are called. Skip writes for any that aren't ready yet.
    if (!m_textures.empty() && m_matTexSlotBuf && m_uvCoordsBuf && m_uvIdxBuf && m_matNormalSlotBuf && m_lightsBuf && m_reservoirBuf && m_reservoirBuf2)
    {
        std::vector<VkDescriptorImageInfo> texInfos(m_textures.size());
        for (size_t i = 0; i < m_textures.size(); ++i) {
            texInfos[i].sampler     = m_textureSampler;
            texInfos[i].imageView   = m_textures[i].view;
            texInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkDescriptorBufferInfo matTexInfo{ m_matTexSlotBuf,    0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo uvCoordInfo{ m_uvCoordsBuf,     0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo uvIdxInfo  { m_uvIdxBuf,        0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo matNormalInfo{ m_matNormalSlotBuf, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo lightsInfo{ m_lightsBuf, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo reservoirInfo { m_reservoirBuf,  0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo reservoirInfo2{ m_reservoirBuf2, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo historyInfo{};
        historyInfo.imageView   = m_historyImageView;
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo albedoInfo{};
        albedoInfo.imageView    = m_albedoImageView;
        albedoInfo.imageLayout  = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo momentsInfo{};
        momentsInfo.imageView   = m_momentsImageView;
        momentsInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo matIdInfo{};
        matIdInfo.imageView     = m_matIdImageView;
        matIdInfo.imageLayout   = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet extraWrites[12]{};
        extraWrites[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[0].dstSet          = m_descriptorSet;
        extraWrites[0].dstBinding      = 7;
        extraWrites[0].descriptorCount = (uint32_t)texInfos.size();
        extraWrites[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        extraWrites[0].pImageInfo      = texInfos.data();

        extraWrites[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[1].dstSet          = m_descriptorSet;
        extraWrites[1].dstBinding      = 8;
        extraWrites[1].descriptorCount = 1;
        extraWrites[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[1].pBufferInfo     = &matTexInfo;

        extraWrites[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[2].dstSet          = m_descriptorSet;
        extraWrites[2].dstBinding      = 9;
        extraWrites[2].descriptorCount = 1;
        extraWrites[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[2].pBufferInfo     = &uvCoordInfo;

        extraWrites[3].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[3].dstSet          = m_descriptorSet;
        extraWrites[3].dstBinding      = 10;
        extraWrites[3].descriptorCount = 1;
        extraWrites[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[3].pBufferInfo     = &uvIdxInfo;

        extraWrites[4].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[4].dstSet          = m_descriptorSet;
        extraWrites[4].dstBinding      = 11;
        extraWrites[4].descriptorCount = 1;
        extraWrites[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[4].pBufferInfo     = &matNormalInfo;

        extraWrites[5].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[5].dstSet          = m_descriptorSet;
        extraWrites[5].dstBinding      = 12;
        extraWrites[5].descriptorCount = 1;
        extraWrites[5].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[5].pBufferInfo     = &lightsInfo;

        extraWrites[6].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[6].dstSet          = m_descriptorSet;
        extraWrites[6].dstBinding      = 13;
        extraWrites[6].descriptorCount = 1;
        extraWrites[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[6].pBufferInfo     = &reservoirInfo;

        extraWrites[7].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[7].dstSet          = m_descriptorSet;
        extraWrites[7].dstBinding      = 14;
        extraWrites[7].descriptorCount = 1;
        extraWrites[7].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        extraWrites[7].pBufferInfo     = &reservoirInfo2;

        extraWrites[8].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[8].dstSet          = m_descriptorSet;
        extraWrites[8].dstBinding      = 15;
        extraWrites[8].descriptorCount = 1;
        extraWrites[8].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        extraWrites[8].pImageInfo      = &historyInfo;

        extraWrites[9].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[9].dstSet          = m_descriptorSet;
        extraWrites[9].dstBinding      = 16;
        extraWrites[9].descriptorCount = 1;
        extraWrites[9].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        extraWrites[9].pImageInfo      = &albedoInfo;

        extraWrites[10].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[10].dstSet          = m_descriptorSet;
        extraWrites[10].dstBinding      = 17;
        extraWrites[10].descriptorCount = 1;
        extraWrites[10].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        extraWrites[10].pImageInfo      = &momentsInfo;

        extraWrites[11].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        extraWrites[11].dstSet          = m_descriptorSet;
        extraWrites[11].dstBinding      = 18;
        extraWrites[11].descriptorCount = 1;
        extraWrites[11].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        extraWrites[11].pImageInfo      = &matIdInfo;

        vkUpdateDescriptorSets(m_device, 12, extraWrites, 0, nullptr);
    }
}

void VulkanRTPath::UpdateCameraVectors(const float eye[3],
                                       const float forward[3],
                                       const float right[3],
                                       const float up[3],
                                       float fovTan,
                                       float aspect,
                                       uint32_t frameId,
                                       uint32_t numLights)
{
    if (!m_cameraUBOMapped) return;
    union { uint32_t u; float f; } u2f0, u2f1;
    u2f0.u = frameId   | 0x40000000u;
    u2f1.u = numLights | 0x40000000u;

    // First frame: prev = current (no real history); afterward use cache.
    const float* peye   = m_havePrevCam ? m_prevEye     : eye;
    const float* pfwd   = m_havePrevCam ? m_prevForward : forward;
    const float* prgt   = m_havePrevCam ? m_prevRight   : right;
    const float* pup    = m_havePrevCam ? m_prevUp      : up;
    const float pFovTan = m_havePrevCam ? m_prevFovTan  : fovTan;
    const float pAspect = m_havePrevCam ? m_prevAspect  : aspect;

    float ubo[36] = {
        eye[0],     eye[1],     eye[2],     aspect,
        forward[0], forward[1], forward[2], fovTan,
        right[0],   right[1],   right[2],   0.f,
        up[0],      up[1],      up[2],      0.f,
        u2f0.f,     u2f1.f,     0.f,        0.f,
        peye[0],    peye[1],    peye[2],    pAspect,
        pfwd[0],    pfwd[1],    pfwd[2],    pFovTan,
        prgt[0],    prgt[1],    prgt[2],    0.f,
        pup[0],     pup[1],     pup[2],     0.f,
    };
    memcpy(m_cameraUBOMapped, ubo, sizeof(ubo));

    // Cache current as next frame's prev.
    for (int i = 0; i < 3; ++i) {
        m_prevEye[i]     = eye[i];
        m_prevForward[i] = forward[i];
        m_prevRight[i]   = right[i];
        m_prevUp[i]      = up[i];
    }
    m_prevFovTan = fovTan;
    m_prevAspect = aspect;
    m_havePrevCam = true;
}

void VulkanRTPath::RecreateOutput(uint32_t width, uint32_t height)
{
    vkDeviceWaitIdle(m_device);
    CreateOutputImage(width, height);

    if (m_descriptorSet)
    {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView   = m_outputImageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = m_descriptorSet;
        w.dstBinding      = 0;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }
}

void VulkanRTPath::TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    m_renderer->pfnCmdTraceRaysKHR(cmd,
                                   &m_sbt.raygenRegion,
                                   &m_sbt.missRegion,
                                   &m_sbt.hitRegion,
                                   &m_sbt.callableRegion,
                                   width, height, 1);
}

// Push constants for atrous (must match GLSL):
struct AtrousPushConstants {
    int32_t  stride;
    uint32_t frameId;
};

void VulkanRTPath::CreateDenoisePipelines(const char* atrousSpvPath, const char* compositeSpvPath)
{
    // ----- Atrous: 5 bindings -----
    VkDescriptorSetLayoutBinding atrousBindings[5]{};
    // 0: in image (rgba16f), 1: out image (rgba16f), 4: albedo (rgba8)
    atrousBindings[0].binding         = 0;
    atrousBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    atrousBindings[0].descriptorCount = 1;
    atrousBindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    atrousBindings[1].binding         = 1;
    atrousBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    atrousBindings[1].descriptorCount = 1;
    atrousBindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    atrousBindings[2].binding         = 2;
    atrousBindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    atrousBindings[2].descriptorCount = 1;
    atrousBindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    atrousBindings[3].binding         = 3;
    atrousBindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    atrousBindings[3].descriptorCount = 1;
    atrousBindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    atrousBindings[4].binding         = 4;
    atrousBindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    atrousBindings[4].descriptorCount = 1;
    atrousBindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslAtrous{};
    dslAtrous.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslAtrous.bindingCount = 5;
    dslAtrous.pBindings    = atrousBindings;
    if (vkCreateDescriptorSetLayout(m_device, &dslAtrous, nullptr, &m_atrousDescSetLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: atrous DSL failed");

    VkPushConstantRange pcAtrous{};
    pcAtrous.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcAtrous.offset     = 0;
    pcAtrous.size       = sizeof(AtrousPushConstants);

    VkPipelineLayoutCreateInfo plAtrous{};
    plAtrous.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plAtrous.setLayoutCount         = 1;
    plAtrous.pSetLayouts            = &m_atrousDescSetLayout;
    plAtrous.pushConstantRangeCount = 1;
    plAtrous.pPushConstantRanges    = &pcAtrous;
    if (vkCreatePipelineLayout(m_device, &plAtrous, nullptr, &m_atrousPipelineLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: atrous pipelineLayout failed");

    auto atrousSpv = LoadFile(atrousSpvPath);
    VkShaderModule atrousMod = CreateShaderModuleFromSPV(m_device, atrousSpv);
    VkComputePipelineCreateInfo cpAtrous{};
    cpAtrous.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpAtrous.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpAtrous.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpAtrous.stage.module = atrousMod;
    cpAtrous.stage.pName  = "main";
    cpAtrous.layout       = m_atrousPipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpAtrous, nullptr, &m_atrousPipeline) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: atrous compute pipeline failed");
    vkDestroyShaderModule(m_device, atrousMod, nullptr);

    // ----- Composite: 3 storage-image bindings -----
    VkDescriptorSetLayoutBinding compBindings[3]{};
    for (int i = 0; i < 3; ++i) {
        compBindings[i].binding         = (uint32_t)i;
        compBindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        compBindings[i].descriptorCount = 1;
        compBindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslComp{};
    dslComp.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslComp.bindingCount = 3;
    dslComp.pBindings    = compBindings;
    if (vkCreateDescriptorSetLayout(m_device, &dslComp, nullptr, &m_compositeDescSetLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: composite DSL failed");

    VkPipelineLayoutCreateInfo plComp{};
    plComp.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plComp.setLayoutCount         = 1;
    plComp.pSetLayouts            = &m_compositeDescSetLayout;
    if (vkCreatePipelineLayout(m_device, &plComp, nullptr, &m_compositePipelineLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: composite pipelineLayout failed");

    auto compSpv = LoadFile(compositeSpvPath);
    VkShaderModule compMod = CreateShaderModuleFromSPV(m_device, compSpv);
    VkComputePipelineCreateInfo cpComp{};
    cpComp.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpComp.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpComp.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpComp.stage.module = compMod;
    cpComp.stage.pName  = "main";
    cpComp.layout       = m_compositePipelineLayout;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpComp, nullptr, &m_compositePipeline) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: composite compute pipeline failed");
    vkDestroyShaderModule(m_device, compMod, nullptr);

    // ----- Descriptor pools + sets -----
    VkDescriptorPoolSize atrousPoolSizes[2]{};
    atrousPoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    atrousPoolSizes[0].descriptorCount = 9;        // 3 sets × 3 image bindings
    atrousPoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    atrousPoolSizes[1].descriptorCount = 6;        // 3 sets × 2 buffer bindings
    VkDescriptorPoolCreateInfo atrousPoolCi{};
    atrousPoolCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    atrousPoolCi.maxSets       = 3;
    atrousPoolCi.poolSizeCount = 2;
    atrousPoolCi.pPoolSizes    = atrousPoolSizes;
    if (vkCreateDescriptorPool(m_device, &atrousPoolCi, nullptr, &m_atrousDescPool) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: atrous pool failed");

    VkDescriptorSetLayout atrousLayouts[3] = { m_atrousDescSetLayout, m_atrousDescSetLayout, m_atrousDescSetLayout };
    VkDescriptorSetAllocateInfo atrousAlloc{};
    atrousAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    atrousAlloc.descriptorPool     = m_atrousDescPool;
    atrousAlloc.descriptorSetCount = 3;
    atrousAlloc.pSetLayouts        = atrousLayouts;
    VkDescriptorSet atrousSets[3];
    if (vkAllocateDescriptorSets(m_device, &atrousAlloc, atrousSets) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: atrous set alloc failed");
    m_atrousSet_HtoPing    = atrousSets[0];
    m_atrousSet_PingToPong = atrousSets[1];
    m_atrousSet_PongToPing = atrousSets[2];

    auto writeAtrousSet = [&](VkDescriptorSet set, VkImageView inV, VkImageView outV) {
        VkDescriptorImageInfo inInfo  { VK_NULL_HANDLE, inV,  VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo outInfo { VK_NULL_HANDLE, outV, VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo albInfo { VK_NULL_HANDLE, m_albedoImageView, VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorBufferInfo resAInfo{ m_reservoirBuf,  0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo resBInfo{ m_reservoirBuf2, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet w[5]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[0].dstSet = set; w[0].dstBinding = 0;
        w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[0].pImageInfo = &inInfo;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[1].dstSet = set; w[1].dstBinding = 1;
        w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[1].pImageInfo = &outInfo;
        w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[2].dstSet = set; w[2].dstBinding = 2;
        w[2].descriptorCount = 1; w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[2].pBufferInfo = &resAInfo;
        w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[3].dstSet = set; w[3].dstBinding = 3;
        w[3].descriptorCount = 1; w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[3].pBufferInfo = &resBInfo;
        w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[4].dstSet = set; w[4].dstBinding = 4;
        w[4].descriptorCount = 1; w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[4].pImageInfo = &albInfo;
        vkUpdateDescriptorSets(m_device, 5, w, 0, nullptr);
    };
    writeAtrousSet(m_atrousSet_HtoPing,    m_historyImageView, m_atrousPingView);
    writeAtrousSet(m_atrousSet_PingToPong, m_atrousPingView,   m_atrousPongView);
    writeAtrousSet(m_atrousSet_PongToPing, m_atrousPongView,   m_atrousPingView);

    // Composite pool + set
    VkDescriptorPoolSize compPoolSize{};
    compPoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    compPoolSize.descriptorCount = 3;
    VkDescriptorPoolCreateInfo compPoolCi{};
    compPoolCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    compPoolCi.maxSets       = 1;
    compPoolCi.poolSizeCount = 1;
    compPoolCi.pPoolSizes    = &compPoolSize;
    if (vkCreateDescriptorPool(m_device, &compPoolCi, nullptr, &m_compositeDescPool) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: composite pool failed");

    VkDescriptorSetAllocateInfo compAlloc{};
    compAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    compAlloc.descriptorPool     = m_compositeDescPool;
    compAlloc.descriptorSetCount = 1;
    compAlloc.pSetLayouts        = &m_compositeDescSetLayout;
    if (vkAllocateDescriptorSets(m_device, &compAlloc, &m_compositeSet) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateDenoisePipelines: composite set alloc failed");

    // Composite reads from PING (final atrous output after 3 odd passes).
    VkDescriptorImageInfo cFiltInfo  { VK_NULL_HANDLE, m_atrousPingView,  VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo cAlbInfo   { VK_NULL_HANDLE, m_albedoImageView, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo cOutInfo   { VK_NULL_HANDLE, m_outputImageView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet cw[3]{};
    cw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; cw[0].dstSet = m_compositeSet; cw[0].dstBinding = 0;
    cw[0].descriptorCount = 1; cw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; cw[0].pImageInfo = &cFiltInfo;
    cw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; cw[1].dstSet = m_compositeSet; cw[1].dstBinding = 1;
    cw[1].descriptorCount = 1; cw[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; cw[1].pImageInfo = &cAlbInfo;
    cw[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; cw[2].dstSet = m_compositeSet; cw[2].dstBinding = 2;
    cw[2].descriptorCount = 1; cw[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; cw[2].pImageInfo = &cOutInfo;
    vkUpdateDescriptorSets(m_device, 3, cw, 0, nullptr);
}

void VulkanRTPath::RunDenoise(VkCommandBuffer cmd, uint32_t width, uint32_t height, uint32_t frameId)
{
    const uint32_t gx = (width  + 7) / 8;
    const uint32_t gy = (height + 7) / 8;

    auto computeBarrier = [&]() {
        VkMemoryBarrier b{};
        b.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &b, 0, nullptr, 0, nullptr);
    };

    // RT writes to history → barrier before compute reads.
    computeBarrier();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_atrousPipeline);

    auto dispatchAtrous = [&](VkDescriptorSet set, int stride) {
        AtrousPushConstants pc{ stride, frameId };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_atrousPipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_atrousPipelineLayout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, gx, gy, 1);
        computeBarrier();
    };
    // Five-pass A-Trous (strides 1/2/4/8/16, ~31-pixel reach). Descriptor
    // sets cycle PING↔PONG; final result lands in PING after odd-number
    // passes (5 here), which is what the composite shader binding reads.
    dispatchAtrous(m_atrousSet_HtoPing,    1);   // history → ping
    dispatchAtrous(m_atrousSet_PingToPong, 2);   // ping → pong
    dispatchAtrous(m_atrousSet_PongToPing, 4);   // pong → ping
    dispatchAtrous(m_atrousSet_PingToPong, 8);   // ping → pong
    dispatchAtrous(m_atrousSet_PongToPing, 16);  // pong → ping

    // Composite: ping × albedo + Reinhard → outImage
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_compositePipelineLayout, 0, 1, &m_compositeSet, 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1);

    // Compute write → blit read on outImage.
    VkMemoryBarrier toBlit{};
    toBlit.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    toBlit.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toBlit.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &toBlit, 0, nullptr, 0, nullptr);
}

void VulkanRTPath::BlitToSwapImage(VkCommandBuffer cmd,
                                   uint32_t swapW, uint32_t swapH,
                                   VkImageLayout finalLayout)
{
    VkImage swapImage = m_renderer->m_swapChainImages[m_renderer->m_imageIndex];

    VkImageMemoryBarrier outBarrier{};
    outBarrier.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    outBarrier.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
    outBarrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
    outBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    outBarrier.image            = m_outputImage;
    outBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    outBarrier.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
    outBarrier.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &outBarrier);

    // UNDEFINED → TRANSFER_DST: discards the previous swap content, which is
    // fine since we're overwriting every pixel via blit.
    VkImageMemoryBarrier swapToDst{};
    swapToDst.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapToDst.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    swapToDst.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapToDst.image            = swapImage;
    swapToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    swapToDst.srcAccessMask    = 0;
    swapToDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &swapToDst);

    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[0]  = { 0, 0, 0 };
    region.srcOffsets[1]  = { (int32_t)m_outputWidth, (int32_t)m_outputHeight, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffsets[0]  = { 0, 0, 0 };
    region.dstOffsets[1]  = { (int32_t)swapW, (int32_t)swapH, 1 };
    vkCmdBlitImage(cmd,
                   m_outputImage, VK_IMAGE_LAYOUT_GENERAL,
                   swapImage,    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, VK_FILTER_NEAREST);

    const bool toColorAttach = (finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkImageMemoryBarrier swapEnd{};
    swapEnd.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapEnd.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapEnd.newLayout        = finalLayout;
    swapEnd.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapEnd.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapEnd.image            = swapImage;
    swapEnd.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    swapEnd.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapEnd.dstAccessMask    = toColorAttach
                               ? (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
                               : 0;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         toColorAttach
                             ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &swapEnd);
}

void VulkanRTPath::CreateOutputImage(uint32_t width, uint32_t height)
{
    DestroyOutputImage();
    m_outputWidth  = width;
    m_outputHeight = height;

    VkImageCreateInfo ic{};
    ic.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ic.imageType     = VK_IMAGE_TYPE_2D;
    ic.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent        = { width, height, 1 };
    ic.mipLevels     = 1;
    ic.arrayLayers   = 1;
    ic.samples       = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ic.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ic.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ic, nullptr, &m_outputImage) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateOutputImage: vkCreateImage failed");

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, m_outputImage, &memReq);
    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &m_outputImageMem) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateOutputImage: vkAllocateMemory failed");
    vkBindImageMemory(m_device, m_outputImage, m_outputImageMem, 0);

    VkImageViewCreateInfo iv{};
    iv.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image      = m_outputImage;
    iv.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    iv.format     = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &iv, nullptr, &m_outputImageView) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateOutputImage: vkCreateImageView failed");

    auto allocStorageImage = [&](VkFormat fmt, VkImage& img, VkDeviceMemory& mem, VkImageView& view, const char* what)
    {
        VkImageCreateInfo ic{};
        ic.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType     = VK_IMAGE_TYPE_2D;
        ic.format        = fmt;
        ic.extent        = { width, height, 1 };
        ic.mipLevels     = 1;
        ic.arrayLayers   = 1;
        ic.samples       = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ic.usage         = VK_IMAGE_USAGE_STORAGE_BIT;
        ic.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ic, nullptr, &img) != VK_SUCCESS)
            ERROR_AND_DIE(Stringf("VulkanRTPath::CreateOutputImage: %s vkCreateImage failed", what));

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(m_device, img, &memReq);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = memReq.size;
        alloc.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &alloc, nullptr, &mem) != VK_SUCCESS)
            ERROR_AND_DIE(Stringf("VulkanRTPath::CreateOutputImage: %s vkAllocateMemory failed", what));
        vkBindImageMemory(m_device, img, mem, 0);

        VkImageViewCreateInfo iv{};
        iv.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image      = img;
        iv.viewType   = VK_IMAGE_VIEW_TYPE_2D;
        iv.format     = fmt;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &iv, nullptr, &view) != VK_SUCCESS)
            ERROR_AND_DIE(Stringf("VulkanRTPath::CreateOutputImage: %s vkCreateImageView failed", what));
    };

    if (m_historyImageView) { vkDestroyImageView(m_device, m_historyImageView, nullptr); m_historyImageView = VK_NULL_HANDLE; }
    if (m_historyImage)     { vkDestroyImage(m_device, m_historyImage, nullptr);         m_historyImage     = VK_NULL_HANDLE; }
    if (m_historyImageMem)  { vkFreeMemory(m_device, m_historyImageMem, nullptr);        m_historyImageMem  = VK_NULL_HANDLE; }
    if (m_albedoImageView)  { vkDestroyImageView(m_device, m_albedoImageView, nullptr);  m_albedoImageView  = VK_NULL_HANDLE; }
    if (m_albedoImage)      { vkDestroyImage(m_device, m_albedoImage, nullptr);          m_albedoImage      = VK_NULL_HANDLE; }
    if (m_albedoImageMem)   { vkFreeMemory(m_device, m_albedoImageMem, nullptr);         m_albedoImageMem   = VK_NULL_HANDLE; }
    if (m_atrousPingView)   { vkDestroyImageView(m_device, m_atrousPingView, nullptr);   m_atrousPingView   = VK_NULL_HANDLE; }
    if (m_atrousPing)       { vkDestroyImage(m_device, m_atrousPing, nullptr);           m_atrousPing       = VK_NULL_HANDLE; }
    if (m_atrousPingMem)    { vkFreeMemory(m_device, m_atrousPingMem, nullptr);          m_atrousPingMem    = VK_NULL_HANDLE; }
    if (m_atrousPongView)   { vkDestroyImageView(m_device, m_atrousPongView, nullptr);   m_atrousPongView   = VK_NULL_HANDLE; }
    if (m_atrousPong)       { vkDestroyImage(m_device, m_atrousPong, nullptr);           m_atrousPong       = VK_NULL_HANDLE; }
    if (m_atrousPongMem)    { vkFreeMemory(m_device, m_atrousPongMem, nullptr);          m_atrousPongMem    = VK_NULL_HANDLE; }
    if (m_momentsImageView) { vkDestroyImageView(m_device, m_momentsImageView, nullptr); m_momentsImageView = VK_NULL_HANDLE; }
    if (m_momentsImage)     { vkDestroyImage(m_device, m_momentsImage, nullptr);         m_momentsImage     = VK_NULL_HANDLE; }
    if (m_momentsImageMem)  { vkFreeMemory(m_device, m_momentsImageMem, nullptr);        m_momentsImageMem  = VK_NULL_HANDLE; }
    if (m_matIdImageView)   { vkDestroyImageView(m_device, m_matIdImageView, nullptr);   m_matIdImageView   = VK_NULL_HANDLE; }
    if (m_matIdImage)       { vkDestroyImage(m_device, m_matIdImage, nullptr);           m_matIdImage       = VK_NULL_HANDLE; }
    if (m_matIdImageMem)    { vkFreeMemory(m_device, m_matIdImageMem, nullptr);          m_matIdImageMem    = VK_NULL_HANDLE; }
    allocStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_historyImage, m_historyImageMem, m_historyImageView, "history");
    allocStorageImage(VK_FORMAT_R8G8B8A8_UNORM,      m_albedoImage,  m_albedoImageMem,  m_albedoImageView,  "albedo");
    allocStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_atrousPing,   m_atrousPingMem,   m_atrousPingView,   "atrous ping");
    allocStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_atrousPong,   m_atrousPongMem,   m_atrousPongView,   "atrous pong");
    allocStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, m_momentsImage, m_momentsImageMem, m_momentsImageView, "moments");
    allocStorageImage(VK_FORMAT_R32_UINT,            m_matIdImage,   m_matIdImageMem,   m_matIdImageView,   "matId");

    // UNDEFINED → GENERAL transitions for all storage images.
    {
        VkCommandBuffer initCmd = BeginOneShotCmd();
        VkImageMemoryBarrier bs[7]{};
        for (int i = 0; i < 7; ++i) {
            bs[i].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            bs[i].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            bs[i].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            bs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bs[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            bs[i].srcAccessMask    = 0;
            bs[i].dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        }
        bs[0].image = m_outputImage;
        bs[1].image = m_historyImage;
        bs[2].image = m_albedoImage;
        bs[3].image = m_atrousPing;
        bs[4].image = m_atrousPong;
        bs[5].image = m_momentsImage;
        bs[6].image = m_matIdImage;
        vkCmdPipelineBarrier(initCmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 7, bs);
        EndAndSubmitOneShotCmd(initCmd);
    }

    // Reservoir SSBOs: ping-pong pair for spatial reuse.
    if (m_reservoirBuf)  { vkDestroyBuffer(m_device, m_reservoirBuf,  nullptr); m_reservoirBuf  = VK_NULL_HANDLE; }
    if (m_reservoirMem)  { vkFreeMemory(m_device, m_reservoirMem,  nullptr);    m_reservoirMem  = VK_NULL_HANDLE; }
    if (m_reservoirBuf2) { vkDestroyBuffer(m_device, m_reservoirBuf2, nullptr); m_reservoirBuf2 = VK_NULL_HANDLE; }
    if (m_reservoirMem2) { vkFreeMemory(m_device, m_reservoirMem2, nullptr);    m_reservoirMem2 = VK_NULL_HANDLE; }
    // 48 bytes per pixel: 12 ints { lightIdx, wsum, M, _pad,
    //                                normal.xyz, depth,
    //                                hitWorld.xyz, _pad2 }.
    const VkDeviceSize reservoirSize = (VkDeviceSize)width * (VkDeviceSize)height * 48;
    const VkBufferUsageFlags resUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    CreateAndAllocateBuffer(reservoirSize, resUsage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            m_reservoirBuf, m_reservoirMem);
    CreateAndAllocateBuffer(reservoirSize, resUsage,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            m_reservoirBuf2, m_reservoirMem2);
    {
        VkCommandBuffer initCmd = BeginOneShotCmd();
        vkCmdFillBuffer(initCmd, m_reservoirBuf,  0, reservoirSize, 0u);
        vkCmdFillBuffer(initCmd, m_reservoirBuf2, 0, reservoirSize, 0u);
        EndAndSubmitOneShotCmd(initCmd);
    }
}

void VulkanRTPath::DestroyOutputImage()
{
    if (m_outputImageView) vkDestroyImageView(m_device, m_outputImageView, nullptr);
    if (m_outputImage)     vkDestroyImage(m_device, m_outputImage, nullptr);
    if (m_outputImageMem)  vkFreeMemory(m_device, m_outputImageMem, nullptr);
    if (m_historyImageView) vkDestroyImageView(m_device, m_historyImageView, nullptr);
    if (m_historyImage)     vkDestroyImage(m_device, m_historyImage, nullptr);
    if (m_historyImageMem)  vkFreeMemory(m_device, m_historyImageMem, nullptr);
    if (m_albedoImageView)  vkDestroyImageView(m_device, m_albedoImageView, nullptr);
    if (m_albedoImage)      vkDestroyImage(m_device, m_albedoImage, nullptr);
    if (m_albedoImageMem)   vkFreeMemory(m_device, m_albedoImageMem, nullptr);
    if (m_atrousPingView)   vkDestroyImageView(m_device, m_atrousPingView, nullptr);
    if (m_atrousPing)       vkDestroyImage(m_device, m_atrousPing, nullptr);
    if (m_atrousPingMem)    vkFreeMemory(m_device, m_atrousPingMem, nullptr);
    if (m_atrousPongView)   vkDestroyImageView(m_device, m_atrousPongView, nullptr);
    if (m_atrousPong)       vkDestroyImage(m_device, m_atrousPong, nullptr);
    if (m_atrousPongMem)    vkFreeMemory(m_device, m_atrousPongMem, nullptr);
    if (m_momentsImageView) vkDestroyImageView(m_device, m_momentsImageView, nullptr);
    if (m_momentsImage)     vkDestroyImage(m_device, m_momentsImage, nullptr);
    if (m_momentsImageMem)  vkFreeMemory(m_device, m_momentsImageMem, nullptr);
    if (m_matIdImageView)   vkDestroyImageView(m_device, m_matIdImageView, nullptr);
    if (m_matIdImage)       vkDestroyImage(m_device, m_matIdImage, nullptr);
    if (m_matIdImageMem)    vkFreeMemory(m_device, m_matIdImageMem, nullptr);
    m_momentsImageView = VK_NULL_HANDLE;
    m_momentsImage     = VK_NULL_HANDLE;
    m_momentsImageMem  = VK_NULL_HANDLE;
    m_matIdImageView   = VK_NULL_HANDLE;
    m_matIdImage       = VK_NULL_HANDLE;
    m_matIdImageMem    = VK_NULL_HANDLE;
    m_outputImageView  = VK_NULL_HANDLE;
    m_outputImage      = VK_NULL_HANDLE;
    m_outputImageMem   = VK_NULL_HANDLE;
    m_historyImageView = VK_NULL_HANDLE;
    m_historyImage     = VK_NULL_HANDLE;
    m_historyImageMem  = VK_NULL_HANDLE;
    m_albedoImageView  = VK_NULL_HANDLE;
    m_albedoImage      = VK_NULL_HANDLE;
    m_albedoImageMem   = VK_NULL_HANDLE;
    m_atrousPingView   = VK_NULL_HANDLE;
    m_atrousPing       = VK_NULL_HANDLE;
    m_atrousPingMem    = VK_NULL_HANDLE;
    m_atrousPongView   = VK_NULL_HANDLE;
    m_atrousPong       = VK_NULL_HANDLE;
    m_atrousPongMem    = VK_NULL_HANDLE;
}

uint32_t VulkanRTPath::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_renderer->m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

VkDeviceAddress VulkanRTPath::GetBufferDeviceAddress(VkBuffer buffer)
{
    if (!m_renderer->pfnGetBufferDeviceAddressKHR) return 0;
    VkBufferDeviceAddressInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return m_renderer->pfnGetBufferDeviceAddressKHR(m_device, &info);
}

VkDeviceAddress VulkanRTPath::GetASDeviceAddress(VkAccelerationStructureKHR as)
{
    if (!m_renderer->pfnGetAccelerationStructureDeviceAddressKHR) return 0;
    VkAccelerationStructureDeviceAddressInfoKHR info{};
    info.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    info.accelerationStructure = as;
    return m_renderer->pfnGetAccelerationStructureDeviceAddressKHR(m_device, &info);
}

void VulkanRTPath::CreateAndAllocateBuffer(VkDeviceSize size,
                                           VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags memProps,
                                           VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size  = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &ci, nullptr, &outBuffer) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateAndAllocateBuffer: vkCreateBuffer failed");

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_device, outBuffer, &memReq);

    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.pNext           = (allocFlags.flags != 0) ? &allocFlags : nullptr;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, memProps);

    if (vkAllocateMemory(m_device, &alloc, nullptr, &outMemory) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateAndAllocateBuffer: vkAllocateMemory failed");
    vkBindBufferMemory(m_device, outBuffer, outMemory, 0);
}

VkCommandBuffer VulkanRTPath::BeginOneShotCmd()
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_oneShotPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &ai, &cmd) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::BeginOneShotCmd: vkAllocateCommandBuffers failed");

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanRTPath::EndAndSubmitOneShotCmd(VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(m_device, &fci, nullptr, &fence);
    vkQueueSubmit(m_renderer->m_graphicsQueue, 1, &si, fence);
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_oneShotPool, 1, &cmd);
}

#endif // ENGINE_VULKAN_RENDERER
