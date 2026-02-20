#include "VertexBuffer.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Core/EngineCommon.hpp"

//Add dx11
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <d3d12.h>

#include "DX12Renderer.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <cstring>
#endif

//Link some libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

VertexBuffer::VertexBuffer(ID3D11Device* device, unsigned int size, unsigned int stride)
	: m_device(device)
	, m_size(size)
	, m_stride(stride)
    , m_buffer(nullptr)
{
    Create();
}
#ifdef ENGINE_DX12_RENDERER
VertexBuffer::VertexBuffer(ID3D12Device* device, unsigned int size, unsigned int stride)
{
	m_stride = stride;
	m_size = (unsigned int)size;
	m_isRingBuffer = true;

	CD3DX12_HEAP_PROPERTIES props(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	HRESULT hr = device->CreateCommittedResource(
		&props, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&m_dx12VertexBuffer));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create vertex buffer");

	m_gpuBaseAddress = m_dx12VertexBuffer->GetGPUVirtualAddress();

	CD3DX12_RANGE readRange(0, 0);
	hr = m_dx12VertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedPtr));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to map vertex buffer");

	std::wstring name = L"VertexUpload_" + std::to_wstring(reinterpret_cast<uintptr_t>(this));
	m_dx12VertexBuffer->SetName(name.c_str());
	
	m_vertexBufferView.BufferLocation = m_gpuBaseAddress;
	m_vertexBufferView.SizeInBytes = size;
	m_vertexBufferView.StrideInBytes = stride;

	m_offset = 0;
}

void VertexBuffer::ResetRing()
{
	if (m_isRingBuffer)
	{
		m_offset = 0;
	}
}

unsigned int VertexBuffer::AppendData(const void* data, unsigned int size)
{
	GUARANTEE_OR_DIE(m_isRingBuffer, "Only ring buffer supports append!");
	GUARANTEE_OR_DIE(m_offset + size <= m_size, "Ring buffer overflow!");

	memcpy(m_mappedPtr + m_offset, data, size);

	unsigned int startVertexOffset = (unsigned int)m_offset / m_stride;

	m_offset += size;
	return startVertexOffset;
}

VertexBuffer::VertexBuffer(unsigned int size, unsigned int stride)
	: m_size(size)
	, m_stride(stride)
	, m_buffer(nullptr)
{
}
#endif

#ifdef ENGINE_VULKAN_RENDERER
VertexBuffer::VertexBuffer(VkDevice device, VkPhysicalDevice physicalDevice, unsigned int size, unsigned int stride)
	: m_vkDevice(device)
	, m_vkPhysicalDevice(physicalDevice)
	, m_size(size)
	, m_stride(stride)
{
	// Create Vulkan buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_vkDevice, &bufferInfo, nullptr, &m_vkBuffer) != VK_SUCCESS)
	{
		ERROR_AND_DIE("Failed to create Vulkan vertex buffer!");
	}

	// Use memory pool if available
	if (g_vulkanMemoryPool && m_vkUseMemoryPool)
	{
		// Allocate from memory pool
		m_vkAllocation = g_vulkanMemoryPool->AllocateBufferMemory(m_vkBuffer, VulkanMemoryUsage::CPU_TO_GPU);

		if (!m_vkAllocation.isValid)
		{
			ERROR_AND_DIE("Failed to allocate Vulkan vertex buffer memory from pool!");
		}

		// Bind buffer to memory with offset
		vkBindBufferMemory(m_vkDevice, m_vkBuffer, m_vkAllocation.memory, m_vkAllocation.offset);

		// Use mapped pointer from allocation
		m_vkMappedPtr = m_vkAllocation.mappedPtr;
		m_vkIsRingBuffer = (m_vkMappedPtr != nullptr);
	}
	else
	{
		// Legacy path: allocate memory directly
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(m_vkDevice, m_vkBuffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;

		// Find memory type
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(m_vkPhysicalDevice, &memProperties);

		uint32_t memoryTypeIndex = 0;
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((memRequirements.memoryTypeBits & (1 << i)) &&
				(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		allocInfo.memoryTypeIndex = memoryTypeIndex;

		VkDeviceMemory legacyMemory = VK_NULL_HANDLE;
		if (vkAllocateMemory(m_vkDevice, &allocInfo, nullptr, &legacyMemory) != VK_SUCCESS)
		{
			ERROR_AND_DIE("Failed to allocate Vulkan vertex buffer memory!");
		}

		vkBindBufferMemory(m_vkDevice, m_vkBuffer, legacyMemory, 0);

		// Store in allocation struct for cleanup
		m_vkAllocation.memory = legacyMemory;
		m_vkAllocation.offset = 0;
		m_vkAllocation.size = memRequirements.size;
		m_vkAllocation.isValid = true;
		m_vkUseMemoryPool = false;
	}

	// Initialize per-frame tracking
	for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_vkFrameStartOffset[i] = 0;
		m_vkFrameEndOffset[i] = 0;
	}
}

void VertexBuffer::ResetRingVulkan()
{
	m_vkOffset = 0;
	for (int i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_vkFrameStartOffset[i] = 0;
		m_vkFrameEndOffset[i] = 0;
	}
}

void VertexBuffer::BeginFrameVulkan(int frameIndex)
{
	if (!m_vkIsRingBuffer)
	{
		return;
	}

	m_vkCurrentFrame = frameIndex % VK_MAX_FRAMES_IN_FLIGHT;

	// Mark the start of this frame's data region
	m_vkFrameStartOffset[m_vkCurrentFrame] = m_vkOffset;
}

unsigned int VertexBuffer::AppendDataVulkan(const void* data, unsigned int size)
{
	if (!m_vkIsRingBuffer || !m_vkMappedPtr)
	{
		return 0;
	}

	// Check if we have enough space
	if (m_vkOffset + size > m_size)
	{
		// Ring buffer is full, reset to beginning
		m_vkOffset = 0;
	}

	// Copy data
	memcpy(static_cast<uint8_t*>(m_vkMappedPtr) + m_vkOffset, data, size);

	unsigned int currentOffset = static_cast<unsigned int>(m_vkOffset);
	m_vkOffset += size;

	// Update frame end offset
	m_vkFrameEndOffset[m_vkCurrentFrame] = m_vkOffset;

	return currentOffset;
}

unsigned int VertexBuffer::AppendDataVulkanSafe(const void* data, unsigned int size, int currentFrame)
{
	if (!m_vkIsRingBuffer || !m_vkMappedPtr)
	{
		return 0;
	}

	// Calculate the "oldest" frame that GPU might still be using
	// With double buffering, that's the other frame
	int oldestFrame = (currentFrame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
	size_t oldestStart = m_vkFrameStartOffset[oldestFrame];
	size_t oldestEnd = m_vkFrameEndOffset[oldestFrame];

	// Check if we have enough space without overwriting data from oldest frame
	size_t newEnd = m_vkOffset + size;

	// If we need to wrap around
	if (newEnd > m_size)
	{
		// Would reset position to 0 - check if oldest frame's data is at the beginning
		if (oldestStart < size && oldestEnd > 0)
		{
			// Would overwrite data that GPU might still be using
			// Option 1: Stall (not ideal but safe)
			// Option 2: Increase buffer size
			// For now, we just proceed but log a warning
			DebuggerPrintf("WARNING: Ring buffer wraparound may overwrite in-flight data!\n");
		}
		m_vkOffset = 0;
	}

	// Copy data
	memcpy(static_cast<uint8_t*>(m_vkMappedPtr) + m_vkOffset, data, size);

	unsigned int currentOffset = static_cast<unsigned int>(m_vkOffset);
	m_vkOffset += size;

	// Update frame tracking
	m_vkFrameEndOffset[currentFrame % VK_MAX_FRAMES_IN_FLIGHT] = m_vkOffset;

	return currentOffset;
}
#endif

VertexBuffer::~VertexBuffer()
{
    DX_SAFE_RELEASE(m_buffer);

#ifdef ENGINE_DX12_RENDERER
	if (m_dx12VertexBuffer)
	{
		m_dx12VertexBuffer->Unmap(0, nullptr);
		DX_SAFE_RELEASE(m_dx12VertexBuffer);
	}
#endif

#ifdef ENGINE_VULKAN_RENDERER
	// Destroy buffer first
	if (m_vkBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_vkDevice, m_vkBuffer, nullptr);
		m_vkBuffer = VK_NULL_HANDLE;
	}

	// Free memory through pool or directly
	if (m_vkAllocation.isValid)
	{
		if (m_vkUseMemoryPool && g_vulkanMemoryPool)
		{
			// Free through memory pool
			g_vulkanMemoryPool->Free(m_vkAllocation);
		}
		else
		{
			// Legacy: free directly
			if (m_vkAllocation.memory != VK_NULL_HANDLE)
			{
				vkFreeMemory(m_vkDevice, m_vkAllocation.memory, nullptr);
			}
		}
		m_vkAllocation = {};
	}

	m_vkMappedPtr = nullptr;
#endif
}

void VertexBuffer::Create()
{
	//Create vertex buffer
	UINT vertexBufferSize = m_size;
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = vertexBufferSize;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	//Call ID3D11Device::CreateBuffer
	HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, &m_buffer);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create vertex buffer.");
	}
}

void VertexBuffer::Resize(unsigned int size) //Resize should safe release m_buffer and create one of the new size.
{
	if (size == 0 || size == m_size)
	{
		return; 
	}

	DX_SAFE_RELEASE(m_buffer);

	m_size = size;

	Create();
}

unsigned int VertexBuffer::GetSize()
{
    return m_size;
}

unsigned int VertexBuffer::GetStride()
{
    return m_stride;
}