#pragma once
#include "Engine/Core/EngineCommon.hpp"
#include <d3d12.h>

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include "Engine/Renderer/VulkanMemoryPool.hpp"
#endif

struct ID3D11Device;
struct ID3D11Buffer;

struct ID3D12Resource;
struct D3D12_CONSTANT_BUFFER_VIEW_DESC;

class ConstantBuffer
{
	friend class Renderer;
	friend class DX11Renderer;
	friend class DX12Renderer;
	friend class VulkanRenderer;

public:
	ConstantBuffer(ID3D11Device* device, size_t size);

#ifdef ENGINE_DX12_RENDERER
	ConstantBuffer(size_t size);
	ConstantBuffer(size_t multiBufferSize,  size_t originalSize);

	void AppendData(void const* data, size_t size, int currentFrameIndex);
	size_t GetFrameOffset(int frame);
	void ResetOffset();
	ID3D12Resource* GetDX12ConstantBuffer() const { return m_dx12ConstantBuffer; }
#endif

#ifdef ENGINE_VULKAN_RENDERER
	ConstantBuffer(VkDevice device, VkPhysicalDevice physicalDevice, size_t size);
	void AppendDataVulkan(void const* data, size_t size, int currentFrameIndex);
	void ResetOffsetVulkan();
	VkBuffer GetVulkanBuffer() const { return m_vkBuffer; }
	VkDeviceMemory GetVulkanMemory() const { return m_vkAllocation.memory; }
#endif
	
	ConstantBuffer(const ConstantBuffer& copy) = delete;
	virtual ~ConstantBuffer();

	void Create();

private:
	ID3D11Device* m_device = nullptr;
	ID3D11Buffer* m_buffer = nullptr;
	size_t m_size = 0;

#ifdef ENGINE_DX12_RENDERER
	ID3D12Resource* m_dx12ConstantBuffer = nullptr;
	D3D12_CONSTANT_BUFFER_VIEW_DESC* m_constantBufferView = nullptr;

	size_t m_maxSize = 0; 
	uint8_t* m_mappedPtr = nullptr;
	size_t m_offset = 0;
#endif

#ifdef ENGINE_VULKAN_RENDERER
	VkDevice m_vkDevice = VK_NULL_HANDLE;
	VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
	VkBuffer m_vkBuffer = VK_NULL_HANDLE;

	// Memory allocation from pool (replaces direct VkDeviceMemory)
	VulkanAllocation m_vkAllocation{};
	bool m_vkUseMemoryPool = true;  // Set to false for legacy behavior

	void* m_vkMappedPtr = nullptr;
	size_t m_vkOffset = 0;
	size_t m_vkMaxSize = 0;
#endif
};
