#include "Engine/Renderer/VulkanRTPath.h"

#ifdef ENGINE_VULKAN_RENDERER

#include "Engine/Renderer/VulkanRenderer.h"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/StringUtils.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

// =====================================================================
// VulkanRTPath — Vulkan KHR ray tracing path. Boilerplate skeleton;
// individual builds (BLAS / TLAS / RT pipeline / SBT / TraceRays) are
// stubbed with TODO markers. The skeleton documents the dataflow + the
// engine-side function-pointer wiring (m_renderer->pfn*).
//
// Design references:
//   - VK_KHR_acceleration_structure / VK_KHR_ray_tracing_pipeline / VK_KHR_ray_query
//   - Sascha Willems' raytracingbasic sample (single triangle)
//   - NVIDIA's nvpro-samples/vk_raytracing_tutorial_KHR
//
// Implementation order (next session):
//   1. CreateOutputImage  — allocate VK_FORMAT_R8G8B8A8_UNORM storage image,
//      transition to GENERAL layout, create view.
//   2. BuildBLAS          — geometry desc with triangle data, query build sizes,
//      allocate AS buffer + scratch, vkCmdBuildAccelerationStructuresKHR.
//   3. BuildTLAS          — instance buffer with VkAccelerationStructureInstanceKHR
//      array, geometry desc with INSTANCES type, build via same path as BLAS.
//   4. CreateRTPipeline   — shader stages (rgen/rchit/rmiss),
//      shader groups (general/triangles_hit/miss), VkRayTracingPipelineCreateInfoKHR.
//   5. CreateSBT          — query handles via vkGetRayTracingShaderGroupHandlesKHR,
//      pack into HOST_VISIBLE buffer with shaderGroupBaseAlignment between regions.
//   6. TraceRays          — bind pipeline + descriptor, vkCmdTraceRaysKHR with
//      raygen/miss/hit/callable region addresses + width/height/1.
//
// =====================================================================

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

    if (m_oneShotPool) vkDestroyCommandPool(m_device, m_oneShotPool, nullptr);
    m_oneShotPool = VK_NULL_HANDLE;

    m_renderer = nullptr;
    m_device   = VK_NULL_HANDLE;
}

void VulkanRTPath::OnSwapchainResized()
{
    // TODO: resize output image to new extent.
}

VulkanBLAS VulkanRTPath::BuildBLAS(const float*    vertexPositions,
                                   uint32_t        vertexCount,
                                   const uint32_t* indices,
                                   uint32_t        indexCount)
{
    VulkanBLAS blas{};
    const VkDeviceSize vertexBufSize = vertexCount * sizeof(float) * 3;  // vec3 positions
    const VkDeviceSize indexBufSize  = indexCount * sizeof(uint32_t);

    // Both buffers need: AS build input | shader device address (so AS build
    // can read them via VkDeviceAddress) | host-coherent (we memcpy in).
    const VkBufferUsageFlags asInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkMemoryPropertyFlags hostCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Vertex buffer.
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

    // Index buffer.
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

    // Geometry desc (single triangles geometry per BLAS for the demo).
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat                = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress    = vbAddr;
    triangles.vertexStride                = sizeof(float) * 3;
    triangles.maxVertex                   = vertexCount - 1;
    triangles.indexType                   = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress     = ibAddr;
    triangles.transformData.deviceAddress = 0;        // identity, no per-geometry transform

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

    // AS storage buffer (sized exactly accelerationStructureSize).
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

    // Scratch buffer for the build itself.
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

    // Instance buffer must be SHADER_DEVICE_ADDRESS + AS_BUILD_INPUT.
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

// ---------- shader module from file -------------------------------------

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
                                    const char* rmissSpvPath)
{
    // ----- Descriptor set layout -----
    // Bindings match raygen.rgen layout(set=0, binding=N):
    //   0: storage image       (raygen writes)
    //   1: acceleration struct (TLAS)
    //   2: uniform buffer      (camera matrices)
    VkDescriptorSetLayoutBinding bindings[3]{};
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
    bindings[2].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 3;
    dsl.pBindings    = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &dsl, nullptr, &m_setLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkCreateDescriptorSetLayout failed");

    VkPipelineLayoutCreateInfo plci{};
    plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_setLayout;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: vkCreatePipelineLayout failed");

    // ----- Shader modules -----
    auto rgenSpv  = LoadFile(rgenSpvPath);
    auto rchitSpv = LoadFile(rchitSpvPath);
    auto rmissSpv = LoadFile(rmissSpvPath);
    VkShaderModule rgen  = CreateShaderModuleFromSPV(m_device, rgenSpv);
    VkShaderModule rchit = CreateShaderModuleFromSPV(m_device, rchitSpv);
    VkShaderModule rmiss = CreateShaderModuleFromSPV(m_device, rmissSpv);

    VkPipelineShaderStageCreateInfo stages[3]{};
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

    // ----- Shader groups -----
    // Group 0: general (raygen)   stage index 0
    // Group 1: triangles hit      closesthit = stage index 1
    // Group 2: general (miss)     stage index 2
    VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
    for (int i = 0; i < 3; ++i) {
        groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groups[i].generalShader      = VK_SHADER_UNUSED_KHR;
        groups[i].closestHitShader   = VK_SHADER_UNUSED_KHR;
        groups[i].anyHitShader       = VK_SHADER_UNUSED_KHR;
        groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    groups[0].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader  = 0;
    groups[1].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[1].closestHitShader = 1;
    groups[2].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[2].generalShader  = 2;

    VkRayTracingPipelineCreateInfoKHR rtci{};
    rtci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtci.stageCount                   = 3;
    rtci.pStages                      = stages;
    rtci.groupCount                   = 3;
    rtci.pGroups                      = groups;
    rtci.maxPipelineRayRecursionDepth = 1;       // primary rays only
    rtci.layout                       = m_pipelineLayout;
    if (m_renderer->pfnCreateRayTracingPipelinesKHR(
            m_device, VK_NULL_HANDLE, VK_NULL_HANDLE,
            1, &rtci, nullptr, &m_rtPipeline) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateRTPipeline: pfnCreateRayTracingPipelinesKHR failed");

    vkDestroyShaderModule(m_device, rgen,  nullptr);
    vkDestroyShaderModule(m_device, rchit, nullptr);
    vkDestroyShaderModule(m_device, rmiss, nullptr);

    // ----- Camera UBO (host-mapped, persistently) -----
    const VkDeviceSize cameraUBOSize = sizeof(float) * 16 * 2;   // viewInv + projInv
    CreateAndAllocateBuffer(cameraUBOSize,
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_cameraUBO, m_cameraUBOMem);
    vkMapMemory(m_device, m_cameraUBOMem, 0, cameraUBOSize, 0, &m_cameraUBOMapped);

    // ----- Descriptor pool + set -----
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;            poolSizes[0].descriptorCount = 1;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; poolSizes[1].descriptorCount = 1;
    poolSizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;           poolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCi{};
    poolCi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets       = 1;
    poolCi.poolSizeCount = 3;
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
    // Per Vulkan spec: each "region" (raygen / miss / hit / callable) has a
    // base address aligned to shaderGroupBaseAlignment, with each handle
    // padded up to shaderGroupHandleAlignment within the region.
    // For our 3-group layout (raygen=1, miss=1, hit=1) each region is 1 handle.
    const uint32_t handleSize    = m_renderer->m_rtProperties.shaderGroupHandleSize;
    const uint32_t handleAlign   = m_renderer->m_rtProperties.shaderGroupHandleAlignment;
    const uint32_t baseAlign     = m_renderer->m_rtProperties.shaderGroupBaseAlignment;
    auto alignUp = [](uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); };
    const uint32_t handleStride  = alignUp(handleSize, handleAlign);

    // 3 regions × 1 handle each, but each region's BASE must be baseAlign-aligned.
    // Layout in buffer: [raygen | pad | miss | pad | hit].
    const uint32_t raygenOffset = 0;
    const uint32_t missOffset   = alignUp(raygenOffset + handleStride, baseAlign);
    const uint32_t hitOffset    = alignUp(missOffset   + handleStride, baseAlign);
    const VkDeviceSize sbtSize  = hitOffset + handleStride;

    // Get all 3 handles in one call (buffer of N * handleSize bytes).
    std::vector<uint8_t> handleStorage(handleSize * 3);
    if (m_renderer->pfnGetRayTracingShaderGroupHandlesKHR(
            m_device, m_rtPipeline, 0, 3,
            handleStorage.size(), handleStorage.data()) != VK_SUCCESS)
        ERROR_AND_DIE("VulkanRTPath::CreateSBT: pfnGetRayTracingShaderGroupHandlesKHR failed");

    // SBT buffer: HOST_VISIBLE for first cut (driver may not be optimal but
    // works on every implementation; use device-local + staging copy for prod).
    CreateAndAllocateBuffer(sbtSize,
                            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_sbt.buffer, m_sbt.memory);

    void* mapped = nullptr;
    vkMapMemory(m_device, m_sbt.memory, 0, sbtSize, 0, &mapped);
    uint8_t* dst = (uint8_t*)mapped;
    memcpy(dst + raygenOffset, handleStorage.data() + 0 * handleSize, handleSize);
    memcpy(dst + missOffset,   handleStorage.data() + 2 * handleSize, handleSize);  // group idx 2 = miss
    memcpy(dst + hitOffset,    handleStorage.data() + 1 * handleSize, handleSize);  // group idx 1 = hit
    vkUnmapMemory(m_device, m_sbt.memory);

    const VkDeviceAddress sbtAddr = GetBufferDeviceAddress(m_sbt.buffer);

    m_sbt.raygenRegion.deviceAddress = sbtAddr + raygenOffset;
    m_sbt.raygenRegion.stride        = handleStride;
    m_sbt.raygenRegion.size          = handleStride;     // raygen size MUST equal stride per spec

    m_sbt.missRegion.deviceAddress   = sbtAddr + missOffset;
    m_sbt.missRegion.stride          = handleStride;
    m_sbt.missRegion.size            = handleStride;

    m_sbt.hitRegion.deviceAddress    = sbtAddr + hitOffset;
    m_sbt.hitRegion.stride           = handleStride;
    m_sbt.hitRegion.size             = handleStride;

    m_sbt.callableRegion = {};   // unused
}

void VulkanRTPath::UpdateDescriptors(const VulkanTLAS& tlas)
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

    VkWriteDescriptorSet writes[3]{};
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

    vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
}

void VulkanRTPath::UpdateCamera(const float* viewInverse_4x4, const float* projInverse_4x4)
{
    if (!m_cameraUBOMapped) return;
    memcpy((uint8_t*)m_cameraUBOMapped + 0,            viewInverse_4x4, sizeof(float) * 16);
    memcpy((uint8_t*)m_cameraUBOMapped + 16 * sizeof(float), projInverse_4x4, sizeof(float) * 16);
}

void VulkanRTPath::RecreateOutput(uint32_t width, uint32_t height)
{
    vkDeviceWaitIdle(m_device);
    CreateOutputImage(width, height);

    // Re-emit binding 0 (storage image) with the new view; binding 1/2 unchanged
    // but re-write all three for safety. Caller must have called
    // UpdateDescriptors at least once already so m_lastBoundTLAS is valid.
    if (m_lastBoundTLAS)
    {
        VulkanTLAS dummy;
        dummy.handle = m_lastBoundTLAS;
        UpdateDescriptors(dummy);
    }
}

void VulkanRTPath::TraceRays(VkCommandBuffer cmd, uint32_t width, uint32_t height)
{
    // Storage images need to be in GENERAL during the pipeline's writes.
    // Caller (App / Game::Render) is responsible for transitioning OUT to
    // TRANSFER_SRC for the post-trace blit. We assume the image is in GENERAL
    // when this is called — a one-time UNDEFINED -> GENERAL barrier must be
    // issued by the caller right after CreateOutputImage / RecreateOutput.

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

// ----- Helpers -----

void VulkanRTPath::CreateOutputImage(uint32_t width, uint32_t height)
{
    DestroyOutputImage();
    m_outputWidth  = width;
    m_outputHeight = height;

    // R8G8B8A8 is fine for first-cut tone-mapped output; switch to RGBA16F later
    // if HDR accumulation needed. STORAGE for raygen imageStore, TRANSFER_SRC
    // so we can blit to swapchain after TraceRays.
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

    // Transition UNDEFINED -> GENERAL so first frame's imageStore is valid.
    // Storage images live in GENERAL during the entire RT pipeline lifetime;
    // we only transition to TRANSFER_SRC during the post-trace blit, then back.
    // This needs a one-shot cmd buffer; defer to per-frame transition for now —
    // first TraceRays will issue the UNDEFINED->GENERAL barrier.
}

void VulkanRTPath::DestroyOutputImage()
{
    if (m_outputImageView) vkDestroyImageView(m_device, m_outputImageView, nullptr);
    if (m_outputImage)     vkDestroyImage(m_device, m_outputImage, nullptr);
    if (m_outputImageMem)  vkFreeMemory(m_device, m_outputImageMem, nullptr);
    m_outputImageView = VK_NULL_HANDLE;
    m_outputImage     = VK_NULL_HANDLE;
    m_outputImageMem  = VK_NULL_HANDLE;
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

    // Synchronous: wait on a fence so AS build is done before we destroy
    // the scratch buffer right after. Init-time only; not perf-critical.
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
