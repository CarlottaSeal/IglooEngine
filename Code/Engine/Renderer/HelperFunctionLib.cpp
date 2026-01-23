#include "HelperFunctionLib.h"

#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/FileUtils.hpp"
#include <fstream>
#include <sstream>

#ifdef ENGINE_DX12_RENDERER
#include <d3d12.h>
#include <D3Dcompiler.h>

#include "ShaderIncludeHandler.h"

static inline D3D12_HEAP_PROPERTIES CreateHeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 0;
    props.VisibleNodeMask = 0;
    return props;
}
static inline D3D12_RESOURCE_DESC CreateBufferDesc(UINT64 size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}
static inline D3D12_CPU_DESCRIPTOR_HANDLE OffsetHandle(
    D3D12_CPU_DESCRIPTOR_HANDLE base, 
    int index, 
    UINT descriptorSize)
{
    base.ptr += index * descriptorSize;
    return base;
}

namespace DX12Helper
{
    static std::string GetDirectoryFromPath(const std::string& filePath)
    {
        size_t lastSlash = filePath.find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            return filePath.substr(0, lastSlash);
        }
        return "";
    }
    
bool CompileShaderToByteCode(
    ID3DBlob** outShaderByteCode,
    const char* debugName,
    const char* source,
    const char* entryPoint,
    const char* target,
    const char* includeDirectory)  // nullptr = 自动从 debugName 推断
{
#if defined ENGINE_DEBUG_RENDER
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | 
        D3DCOMPILE_PREFER_FLOW_CONTROL | D3DCOMPILE_ENABLE_STRICTNESS |
        D3DCOMPILE_DEBUG_NAME_FOR_SOURCE | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#else
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#endif
    
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    
    // Include 目录策略:
    // 优先级 1: 传入的 includeDirectory 参数
    // 优先级 2: 从 shader 文件路径 (debugName) 自动推断
    // 优先级 3: 默认 "Data/Shaders"
    // 同时始终添加 "Data/Shaders" 作为备选搜索路径
    
    std::string baseDir;
    if (includeDirectory != nullptr && strlen(includeDirectory) > 0)
    {
        // 优先级 1: 使用传入的目录
        baseDir = includeDirectory;
    }
    else
    {
        // 优先级 2: 从 shader 文件路径自动推断
        baseDir = GetDirectoryFromPath(debugName);
        
        // 优先级 3: 如果推断失败，使用默认目录
        if (baseDir.empty())
        {
            baseDir = "Data/Shaders";
        }
    }
    ShaderIncludeHandler includeHandler(baseDir);
    // 始终添加通用 shader 目录作为备选
    if (baseDir != "Data/Shaders")
    {
        includeHandler.AddIncludePath("Data/Shaders");
    }
    
    HRESULT hr = D3DCompile(
        source,
        strlen(source),
        debugName,
        nullptr,
        &includeHandler,  
        entryPoint,
        target,
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob
    );
    
    if (SUCCEEDED(hr))
    {
        *outShaderByteCode = shaderBlob;
        if (errorBlob != nullptr)
        {
            errorBlob->Release();
        }
        return true;
    }
    
    if (errorBlob != nullptr)
    {
        DebuggerPrintf("[DX12Helper] Shader compilation failed (%s::%s):\n%s\n",
            debugName, entryPoint, (char*)errorBlob->GetBufferPointer());
        errorBlob->Release();
    }
    else
    {
        DebuggerPrintf("[DX12Helper] Shader compilation failed (%s::%s): HRESULT = 0x%08X\n",
            debugName, entryPoint, hr);
    }
    return false;
}

ID3D12PipelineState* CreateComputePSO(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const char* shaderFile,
    const char* entryPoint,
    const wchar_t* debugName,
    const char* includeDirectory) 
{
    std::string shaderSource;
    if (FileReadToString(shaderSource, shaderFile) < 0)
    {
        DebuggerPrintf("[DX12Helper] Failed to load shader: %s\n", shaderFile);
        return nullptr;
    }
    ID3DBlob* cs = nullptr;
    if (!CompileShaderToByteCode(&cs, shaderFile, shaderSource.c_str(), entryPoint, "cs_5_1", includeDirectory))
    {
        ERROR_AND_DIE("Cannot compile this shader to create compute pso!");
        return nullptr;
    }
    
    // 创建 PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
    psoDesc.CS.BytecodeLength = cs->GetBufferSize();
    
    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    
    cs->Release();
    
    if (FAILED(hr))
    {
        DebuggerPrintf("[DX12Helper] Failed to create Compute PSO: %s\n", shaderFile);
        return nullptr;
    }
    
    pso->SetName(debugName);
    return pso;
}

    void CreateBuffer(
    ID3D12Device* device,
    ID3D12DescriptorHeap* descriptorHeap,
    UINT descriptorSize,
    ID3D12Resource** outResource,
    uint32_t elementCount,
    uint32_t elementSize,
    int srvSlot,
    int uavSlot,
    const wchar_t* debugName)
{
    uint32_t bufferSize = elementCount * elementSize;
    // 创建 Buffer
    D3D12_HEAP_PROPERTIES defaultProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC desc = CreateBufferDesc(
        bufferSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );
    
    HRESULT hr = device->CreateCommittedResource(
        &defaultProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(outResource)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create buffer");
    (*outResource)->SetName(debugName);
    
    // 创建 SRV
    if (srvSlot >= 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        // 判断类型
        if (elementSize == 4)  // float
        {
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 8)  // float2
        {
            srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 12)  // float3
        {
            srvDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 16)  // float4
        {
            srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elementCount;
        }
        else  // StructuredBuffer
        {
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = elementCount;
            srvDesc.Buffer.StructureByteStride = elementSize;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    srvSlot,
    descriptorSize);
        device->CreateShaderResourceView(*outResource, &srvDesc, srvHandle);
    }
    // 创建 UAV
    if (uavSlot >= 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        
        if (elementSize == 4)
        {
            uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 8)
        {
            uavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 12)
        {
            uavDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = elementCount;
        }
        else if (elementSize == 16)
        {
            uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = elementCount;
        }
        else
        {
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = elementCount;
            uavDesc.Buffer.StructureByteStride = elementSize;
        }
        
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    uavSlot,
    descriptorSize);
        device->CreateUnorderedAccessView(*outResource, nullptr, &uavDesc, uavHandle);
    }
}

    void CreateTexture2D(
    ID3D12Device* device,
    ID3D12DescriptorHeap* descriptorHeap,
    UINT descriptorSize,
    ID3D12Resource** outResource,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    int srvSlot,
    int uavSlot,
    const wchar_t* debugName,
    D3D12_RESOURCE_STATES initialState)
{
    // 创建 Texture2D
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES defaultProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(outResource)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Texture2D");
    (*outResource)->SetName(debugName);
    
    // 创建 SRV
    if (srvSlot >= 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    srvSlot,
    descriptorSize);
        device->CreateShaderResourceView(*outResource, &srvDesc, srvHandle);
    }
    
    // 创建 UAV
    if (uavSlot >= 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    uavSlot,
    descriptorSize);
        device->CreateUnorderedAccessView(*outResource, nullptr, &uavDesc, uavHandle);
    }
}

// 创建 Texture3D + SRV + UAV
void CreateTexture3D(
    ID3D12Device* device,
    ID3D12DescriptorHeap* descriptorHeap,
    UINT descriptorSize,
    ID3D12Resource** outResource,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    DXGI_FORMAT format,
    int srvSlot,
    int uavSlot,
    const wchar_t* debugName)
{
    // 创建 Texture3D
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = (UINT16)depth;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    D3D12_HEAP_PROPERTIES defaultProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(outResource)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Texture3D");
    (*outResource)->SetName(debugName);
    
    // 创建 SRV
    if (srvSlot >= 0)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    srvSlot,
    descriptorSize);
        device->CreateShaderResourceView(*outResource, &srvDesc, srvHandle);
    }
    
    // 创建 UAV
    if (uavSlot >= 0)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = depth;
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = OffsetHandle(
    descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
    uavSlot,
    descriptorSize);
        device->CreateUnorderedAccessView(*outResource, nullptr, &uavDesc, uavHandle);
    }
}

ID3D12Resource* CreateUploadBuffer(
    ID3D12Device* device,
    uint64_t bufferSize,
    const wchar_t* debugName)
{
    D3D12_HEAP_PROPERTIES uploadProps = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC bufferDesc = CreateBufferDesc(bufferSize);
    
    ID3D12Resource* buffer = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &uploadProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Upload Buffer");
    buffer->SetName(debugName);
    
    return buffer;
}

ID3D12Resource* CreateDefaultBuffer(
    ID3D12Device* device,
    uint64_t bufferSize,
    D3D12_RESOURCE_FLAGS flags,
    const wchar_t* debugName)
{
    D3D12_HEAP_PROPERTIES defaultProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC bufferDesc = CreateBufferDesc(bufferSize, flags);
    
    ID3D12Resource* buffer = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &defaultProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&buffer)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Default Buffer");
    buffer->SetName(debugName);
    
    return buffer;
}

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
    ID3D12DescriptorHeap* heap,
    UINT descriptorSize,
    int index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptorSize;
    return handle;
}

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
        ID3D12DescriptorHeap* heap,
        UINT descriptorSize,
        int index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptorSize;
    return handle;
}

void TransitionResource(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
    {
        DebuggerPrintf("Before and after states are the same, no transition needed.\n");
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList->ResourceBarrier(1, &barrier);
}

    void UAVBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    cmdList->ResourceBarrier(1, &barrier);
}

    void AliasingBarrier(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resourceBefore,
        ID3D12Resource* resourceAfter)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Aliasing.pResourceBefore = resourceBefore;
    barrier.Aliasing.pResourceAfter = resourceAfter;
    
    commandList->ResourceBarrier(1, &barrier);
}

void SetDebugName(ID3D12Object* object, const wchar_t* name)
{
    if (object)
    {
        object->SetName(name);
    }
}

void PrintHResultError(HRESULT hr, const char* context)
{
    DebuggerPrintf("[DX12Helper] %s failed with HRESULT: 0x%08X\n", context, hr);
    
    // 常见错误码
    switch (hr)
    {
    case E_OUTOFMEMORY:
        DebuggerPrintf("  -> E_OUTOFMEMORY: Out of memory\n");
        break;
    case E_INVALIDARG:
        DebuggerPrintf("  -> E_INVALIDARG: Invalid argument\n");
        break;
    case DXGI_ERROR_DEVICE_REMOVED:
        DebuggerPrintf("  -> DXGI_ERROR_DEVICE_REMOVED: Device removed\n");
        break;
    case DXGI_ERROR_DEVICE_RESET:
        DebuggerPrintf("  -> DXGI_ERROR_DEVICE_RESET: Device reset\n");
        break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        DebuggerPrintf("  -> DXGI_ERROR_DRIVER_INTERNAL_ERROR: Driver internal error\n");
        break;
    default:
        break;
    }
}

UINT GetFormatBytesPerPixel(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;
        
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 12;
        
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return 8;
        
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
        return 4;
        
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        return 2;
        
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 1;
        
    default:
        DebuggerPrintf("[DX12Helper] Unknown format: %d\n", format);
        return 0;
    }
}

bool IsDepthFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D16_UNORM:
        return true;
    default:
        return false;
    }
}

bool IsUAVCompatibleFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SINT:
        return true;
    default:
        return false;
    }
}

void WaitForGPU(
    ID3D12CommandQueue* commandQueue,
    ID3D12Fence* fence,
    UINT64& fenceValue,
    HANDLE fenceEvent)
{
    const UINT64 currentFenceValue = fenceValue;
    commandQueue->Signal(fence, currentFenceValue);
    fenceValue++;
    
    if (fence->GetCompletedValue() < currentFenceValue)
    {
        fence->SetEventOnCompletion(currentFenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}
    D3D12_HEAP_PROPERTIES DX12Helper::GetDefaultHeapProperties()
{
    return CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
}
    
D3D12_HEAP_PROPERTIES GetUploadHeapProperties()
{
    return CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
}

D3D12_HEAP_PROPERTIES GetReadbackHeapProperties()
{
    return CreateHeapProperties(D3D12_HEAP_TYPE_READBACK);
}

} // namespace DX12Helper
#endif