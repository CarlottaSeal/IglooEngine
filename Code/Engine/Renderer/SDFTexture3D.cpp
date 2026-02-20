#include "SDFTexture3D.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "DX12Renderer.hpp"

//Link some libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(ENGINE_DEBUG_RENDER)
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

SDFTexture3D::~SDFTexture3D()
{
#ifdef ENGINE_DX12_RENDERER
    DX_SAFE_RELEASE(m_sdfTexture3D);
	for (auto* buf : m_tempBuffers)
	{
		DX_SAFE_RELEASE(buf);
	}
#endif
}

SDFTexture3D::SDFTexture3D(int resolution)
    : m_resolution(resolution)
{
}
