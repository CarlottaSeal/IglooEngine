#include "ConstantBuffer.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/EngineCommon.hpp"

//Add dx11
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "RenderCommon.h"

#ifdef ENGINE_VULKAN_RENDERER
#include <vulkan/vulkan.h>
#include <cstring>
#endif

//Link some libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

ConstantBuffer::ConstantBuffer(ID3D11Device* device, size_t size)
	:m_size(size)
	,m_device(device)
{
	Create();
}
#ifdef ENGINE_DX12_RENDERER
ConstantBuffer::ConstantBuffer(size_t size)
	:m_size(size)
{
//#ifdef ENGINE_DX12_RENDERER
	m_constantBufferView = new D3D12_CONSTANT_BUFFER_VIEW_DESC();
//#endif
}

ConstantBuffer::ConstantBuffer(size_t multiBufferSize, size_t originalSize)
	:m_maxSize(multiBufferSize)
	,m_size((originalSize))
	//,m_frameOffsets(multiBufferSize/originalSize, 0)
{
	m_constantBufferView = new D3D12_CONSTANT_BUFFER_VIEW_DESC();
}

void ConstantBuffer::AppendData(void const* data, size_t size, int currentDraw)
{
	size_t alignedSize = AlignUp(size, 256);
	size_t offset = currentDraw * alignedSize;
	GUARANTEE_OR_DIE(offset + alignedSize <= m_maxSize, "ConstantBuffer overflow!");

	memcpy(m_mappedPtr + offset, data, size);
	m_offset = offset;
}

size_t ConstantBuffer::GetFrameOffset(int frame)
{
	return frame * m_size;
}

void ConstantBuffer::ResetOffset()
{
	m_offset = 0;
}
#endif

#ifdef ENGINE_VULKAN_RENDERER
ConstantBuffer::ConstantBuffer(VkDevice device, VkPhysicalDevice physicalDevice, size_t size)
	: m_vkDevice(device)
	, m_vkPhysicalDevice(physicalDevice)
	, m_size(size)
{
	// Align size to minimum uniform buffer offset alignment (256 bytes)
	const VkDeviceSize minAlignment = 256;
	m_vkMaxSize = (size + minAlignment - 1) & ~(minAlignment - 1);

	// Create Vulkan buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = m_vkMaxSize;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_vkDevice, &bufferInfo, nullptr, &m_vkBuffer) != VK_SUCCESS)
	{
		ERROR_AND_DIE("Failed to create Vulkan constant buffer!");
	}

	// Use memory pool if available
	if (g_vulkanMemoryPool && m_vkUseMemoryPool)
	{
		// Allocate from memory pool
		m_vkAllocation = g_vulkanMemoryPool->AllocateBufferMemory(m_vkBuffer, VulkanMemoryUsage::CPU_TO_GPU);

		if (!m_vkAllocation.isValid)
		{
			ERROR_AND_DIE("Failed to allocate Vulkan constant buffer memory from pool!");
		}

		// Bind buffer to memory with offset
		vkBindBufferMemory(m_vkDevice, m_vkBuffer, m_vkAllocation.memory, m_vkAllocation.offset);

		// Use mapped pointer from allocation
		m_vkMappedPtr = m_vkAllocation.mappedPtr;
	}
	else
	{
		// Legacy path: allocate memory directly
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(m_vkDevice, m_vkBuffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;

		// Find memory type - needs to be HOST_VISIBLE and HOST_COHERENT
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
			ERROR_AND_DIE("Failed to allocate Vulkan constant buffer memory!");
		}

		vkBindBufferMemory(m_vkDevice, m_vkBuffer, legacyMemory, 0);

		// Store in allocation struct for cleanup
		m_vkAllocation.memory = legacyMemory;
		m_vkAllocation.offset = 0;
		m_vkAllocation.size = memRequirements.size;
		m_vkAllocation.isValid = true;
		m_vkUseMemoryPool = false;

		// Keep buffer mapped for frequent updates
		vkMapMemory(m_vkDevice, legacyMemory, 0, m_vkMaxSize, 0, &m_vkMappedPtr);
	}
}

void ConstantBuffer::AppendDataVulkan(void const* data, size_t size, int currentDraw)
{
	size_t alignedSize = (size + 255) & ~255;  // Align to 256 bytes
	size_t offset = currentDraw * alignedSize;
	
	if (offset + alignedSize > m_vkMaxSize)
	{
		ERROR_AND_DIE("Vulkan ConstantBuffer overflow!");
	}

	memcpy(static_cast<uint8_t*>(m_vkMappedPtr) + offset, data, size);
	m_vkOffset = offset;
}

void ConstantBuffer::ResetOffsetVulkan()
{
	m_vkOffset = 0;
}
#endif

ConstantBuffer::~ConstantBuffer()
{
	if(m_buffer)
		m_buffer->Release();

#ifdef ENGINE_DX12_RENDERER
	delete m_constantBufferView;
	m_constantBufferView = nullptr;
	DX_SAFE_RELEASE( m_dx12ConstantBuffer )
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
			// Free through memory pool (memory is persistently mapped in pool)
			g_vulkanMemoryPool->Free(m_vkAllocation);
		}
		else
		{
			// Legacy: unmap and free directly
			if (m_vkMappedPtr && m_vkAllocation.memory != VK_NULL_HANDLE)
			{
				vkUnmapMemory(m_vkDevice, m_vkAllocation.memory);
			}
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

void ConstantBuffer::Create()
{
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = (UINT)m_size;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HRESULT hr = m_device->CreateBuffer(&bufferDesc, nullptr, &m_buffer);
	if (!SUCCEEDED(hr))
	{
		DebuggerPrintf("CreateBuffer failed with HRESULT: 0x%08X\n", hr);
		ERROR_AND_DIE("Could not create constant buffer.");
	}
}