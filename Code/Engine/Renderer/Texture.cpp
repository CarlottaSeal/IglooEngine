#include "Engine/Renderer/Texture.hpp"
#include "Engine/Renderer/Renderer.hpp"
//#include <d3d11.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>

//Link some libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(ENGINE_DEBUG_RENDER)
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

Texture::Texture()
{

}

Texture::~Texture()
{
#ifdef ENGINE_DX11_RENDERER
	DX_SAFE_RELEASE(m_texture);
	DX_SAFE_RELEASE(m_shaderResourceView);
#endif

#ifdef ENGINE_DX12_RENDERER
	DX_SAFE_RELEASE(m_textureBufferUploadHeap);
	DX_SAFE_RELEASE(m_dx12Texture);
#endif

	// Note: Vulkan resources are cleaned up by VulkanRenderer
}