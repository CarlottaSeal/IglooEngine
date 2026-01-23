#pragma once

#include "Engine/Core/EngineCommon.hpp"
#ifdef ENGINE_DX12_RENDERER

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace DX12Helper
{
    bool CompileShaderToByteCode(
        ID3DBlob** outShaderByteCode,
        const char* debugName,
        const char* source,
        const char* entryPoint,
        const char* target,// "vs_5_0", "ps_5_1", "cs_5_1", etc.
        const char* includeDirectory = nullptr);  

    ID3D12PipelineState* CreateComputePSO(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        const char* shaderFile,
        const char* entryPoint,
        const wchar_t* debugName,
        const char* includeDirectory = nullptr);  

    // 创建 Buffer + SRV + UAV（自动写入 Descriptor Heap）
    void CreateBuffer(
        ID3D12Device* device,
        ID3D12DescriptorHeap* descriptorHeap,
        UINT descriptorSize,
        ID3D12Resource** outResource,
        uint32_t elementCount,
        uint32_t elementSize,
        int srvSlot,  // -1 表示不创建
        int uavSlot,  // -1 表示不创建
        const wchar_t* debugName);
    
    // 创建 Texture2D + SRV + UAV（自动写入 Descriptor Heap）
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
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
    
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
        const wchar_t* debugName);
    
    // 创建 Upload Buffer（用于 CPU -> GPU 数据传输）
    ID3D12Resource* CreateUploadBuffer(
        ID3D12Device* device,
        uint64_t bufferSize,
        const wchar_t* debugName);
    
    ID3D12Resource* CreateDefaultBuffer(
        ID3D12Device* device,
        uint64_t bufferSize,
        D3D12_RESOURCE_FLAGS flags,
        const wchar_t* debugName);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
        ID3D12DescriptorHeap* heap,
        UINT descriptorSize,
        int index);
    
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
        ID3D12DescriptorHeap* heap,
        UINT descriptorSize,
        int index);

    void TransitionResource(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES beforeState,
        D3D12_RESOURCE_STATES afterState);
    
    void UAVBarrier(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resource);
    
    void AliasingBarrier(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resourceBefore,
        ID3D12Resource* resourceAfter);

  
    void SetDebugName(ID3D12Object* object, const wchar_t* name);
    
    UINT GetFormatBytesPerPixel(DXGI_FORMAT format);
    
    bool IsDepthFormat(DXGI_FORMAT format);
    
    bool IsUAVCompatibleFormat(DXGI_FORMAT format);

    template<typename T>
    inline T AlignUp(T value, T alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }
    
    constexpr UINT CONSTANT_BUFFER_ALIGNMENT = 256;
    
    inline UINT AlignConstantBuffer(UINT size)
    {
        return AlignUp(size, CONSTANT_BUFFER_ALIGNMENT);
    }
    
    void WaitForGPU(
        ID3D12CommandQueue* commandQueue,
        ID3D12Fence* fence,
        UINT64& fenceValue,
        HANDLE fenceEvent);

    
    D3D12_HEAP_PROPERTIES GetDefaultHeapProperties();
    D3D12_HEAP_PROPERTIES GetUploadHeapProperties();
    D3D12_HEAP_PROPERTIES GetReadbackHeapProperties();
    
} // namespace DX12Helper

#endif // ENGINE_DX12_RENDERER