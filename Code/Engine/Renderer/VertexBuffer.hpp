#pragma once
#include "Engine/Core/EngineCommon.hpp"
#include <d3d11.h>
#include <d3d12.h>

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include "Engine/Renderer/VulkanMemoryPool.hpp"
#endif

struct ID3D11Device;
struct ID3D11Buffer;

struct ID3D12Resource;
struct D3D12_VERTEX_BUFFER_VIEW;

class VertexBuffer
{
	friend class Renderer;
	friend class DX11Renderer;
	friend class DX12Renderer;
	friend class VulkanRenderer;

public:
	VertexBuffer(ID3D11Device* device, unsigned int size, unsigned int stride);

#ifdef ENGINE_DX12_RENDERER
	VertexBuffer(ID3D12Device* device, unsigned int size, unsigned int stride);
	void ResetRing();
	unsigned int AppendData(const void* data, unsigned int size);
	
	VertexBuffer(unsigned int size, unsigned int stride);
#endif

#ifdef ENGINE_VULKAN_RENDERER
	VertexBuffer(VkDevice device, VkPhysicalDevice physicalDevice, unsigned int size, unsigned int stride);
	void ResetRingVulkan();
	unsigned int AppendDataVulkan(const void* data, unsigned int size);
#endif

	VertexBuffer(const VertexBuffer& copy) = delete;
	virtual ~VertexBuffer();

	void Create();
	void Resize(unsigned int size);

	unsigned int GetSize();
	unsigned int GetStride();

private:
	ID3D11Device* m_device = nullptr;
	ID3D11Buffer* m_buffer = nullptr;
	unsigned int m_size = 0;
	unsigned int m_stride = 0;

#ifdef ENGINE_DX12_RENDERER
	ID3D12Resource* m_dx12VertexBuffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};

	D3D12_GPU_VIRTUAL_ADDRESS m_gpuBaseAddress = 0;
	size_t m_offset = 0;
	uint8_t* m_mappedPtr = nullptr;
	
	bool m_isRingBuffer = false; 
#endif

#ifdef ENGINE_VULKAN_RENDERER
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkBuffer m_vkBuffer = VK_NULL_HANDLE;

	// Memory allocation from pool (replaces direct VkDeviceMemory)
	VulkanAllocation m_vkAllocation{};
	bool m_vkUseMemoryPool = true;  // Set to false for legacy behavior

	// Ring buffer support for Vulkan with per-frame synchronization
	static const int VK_MAX_FRAMES_IN_FLIGHT = 2;
	void* m_vkMappedPtr = nullptr;
	size_t m_vkOffset = 0;
	size_t m_vkFrameStartOffset[VK_MAX_FRAMES_IN_FLIGHT] = {0, 0};  // Track start offset per frame
	size_t m_vkFrameEndOffset[VK_MAX_FRAMES_IN_FLIGHT] = {0, 0};    // Track end offset per frame
	int m_vkCurrentFrame = 0;
	bool m_vkIsRingBuffer = false;

	// Safe append that respects frame boundaries
	unsigned int AppendDataVulkanSafe(const void* data, unsigned int size, int currentFrame);
	void BeginFrameVulkan(int frameIndex);
#endif
};
