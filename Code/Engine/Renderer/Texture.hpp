#pragma once

#include <string>
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Math/IntVec2.hpp"

#ifdef ENGINE_VULKAN_RENDERER  
    #include <vulkan/vulkan.h>
#endif

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;

#ifdef ENGINE_DX12_RENDERER
    struct ID3D12Resource;
#endif

class Texture
{
    friend class Renderer;
    
#ifdef ENGINE_DX11_RENDERER
    friend class DX11Renderer;
#endif

#ifdef ENGINE_DX12_RENDERER
    friend class DX12Renderer;
#endif

#ifdef ENGINE_VULKAN_RENDERER
    friend class VulkanRenderer;
#endif

private:
    Texture();
    Texture(Texture const& copy) = delete;
    ~Texture();

public:
    IntVec2 GetDimensions() const { return m_dimensions; }
    std::string const& GetImageFilePath() const { return m_name; }

protected:
    std::string m_name;
    IntVec2 m_dimensions;
    
#ifdef ENGINE_DX11_RENDERER
    ID3D11Texture2D* m_texture = nullptr;
    ID3D11ShaderResourceView* m_shaderResourceView = nullptr;
#endif
    
#ifdef ENGINE_DX12_RENDERER
    ID3D12Resource* m_dx12Texture = nullptr;
    ID3D12Resource* m_textureBufferUploadHeap = nullptr;
    int m_textureDescIndex = -1;
#endif
    
#ifdef ENGINE_VULKAN_RENDERER
    VkImage m_vkImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkImageMemory = VK_NULL_HANDLE;
    VkImageView m_vkImageView = VK_NULL_HANDLE;
    VkSampler m_vkSampler = VK_NULL_HANDLE;
    
    // Descriptor set info
    int m_vkDescriptorIndex = -1;
    VkDescriptorSet m_vkDescriptorSet = VK_NULL_HANDLE;
#endif
};