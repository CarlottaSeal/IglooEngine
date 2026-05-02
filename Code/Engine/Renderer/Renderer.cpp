
#include "Engine/Renderer/DX11Renderer.hpp"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Renderer/BMFont.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Shader.hpp"
#include "Engine/Renderer/VertexBuffer.hpp"
#include "Engine/Renderer/IndexBuffer.hpp"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"

#include "Engine/Core/EngineCommon.hpp"
#include <windows.h>
//#include <gl/gl.h>
#include <string.h>

#include "ThirdParty/stb/stb_image.h"

#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Image.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/Mat44.hpp"
#include "Engine/Window/Window.hpp"

//#pragma comment( lib, "opengl32" )

//Add dx11
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "DX12Renderer.hpp"

#ifdef ENGINE_VULKAN_RENDERER
#include "Engine/Renderer/VulkanRenderer.h"
#endif

//Link some libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(ENGINE_DEBUG_RENDER)
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

//HDC g_displayDeviceContext = nullptr;				// ...becomes void* Window::m_displayContext
HGLRC g_openGLRenderingContext = nullptr;			// ...becomes void* Renderer::m_apiRenderingContext

// extern Window* g_theWindow;
//
#if defined(ENGINE_DEBUG_RENDER)
void* m_dxgiDebug = nullptr;
void* m_dxgiDebugModule = nullptr;
#endif

 #if defined(OPAQUE)
 #undef OPAQUE
 #endif

Renderer::Renderer(RendererConfig config)
    :m_config(config)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer = new DX11Renderer(config);
#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer = new DX12Renderer(config);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer = new VulkanRenderer(config);
#endif
}

void Renderer::Startup() 
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->Startup();
	#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->Startup();
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->Startup();
#endif
}

void Renderer::ShutDown()
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->ShutDown();
	#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->ShutDown();
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->ShutDown();
#endif
}

void Renderer::BeginFrame() 
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BeginFrame();
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BeginFrame();
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BeginFrame();
#endif
}

void Renderer::EndFrame() 
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->EndFrame();
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->EndFrame();
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->EndFrame();
#endif
}

void Renderer::ClearScreen(const Rgba8 & clearColor)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->ClearScreen(clearColor);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->ClearScreen(clearColor);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->ClearScreen(clearColor);
#endif
}

void Renderer::ClearScreen()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->ClearScreen(Rgba8::MAGENTA);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->ClearScreen(Rgba8::MAGENTA);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->ClearScreen(Rgba8::MAGENTA);
#endif
}

void Renderer::BeginCamera(const Camera& camera)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BeginCamera(camera);
	#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BeginCamera(camera);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BeginCamera(camera);
#endif
}

void Renderer::EndCamera(const Camera& camera)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->EndCamera(camera);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->EndCamera(camera);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->EndCamera(camera);
#endif
}

void Renderer::SetViewport(const AABB2& normalizedViewport)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetViewport(normalizedViewport);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetViewport(normalizedViewport);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetViewport(normalizedViewport);
#endif
}

void Renderer::DrawVertexArray(int numVerts, const Vertex_PCU* verts)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(numVerts, verts);
	#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexArray(numVerts, verts);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexArray(numVerts, verts);
#endif
}

void Renderer::DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(verts);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexArray(verts);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexArray(verts);
#endif
}

void Renderer::DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(numVerts, verts);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexArray(numVerts, verts);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexArray(numVerts, verts);
#endif
}

void Renderer::DrawVertexArray(const std::vector<Vertex_Font>& verts)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(verts);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(verts);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(verts);
#endif
}

void Renderer::DrawVertexArray(int numVerts, const Vertex_Font* verts)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(numVerts, verts);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(numVerts); UNUSED(verts);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(numVerts); UNUSED(verts);
#endif
}

void Renderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexIndexArray(numVerts, verts, numIndices, indices);
#endif
#ifdef ENGINE_DX12_RENDERER
	//m_dx12Renderer->DrawVertexIndexArray()
	UNUSED(numVerts);
	UNUSED(numIndices);
	UNUSED(indices);
	UNUSED(verts);
	ERROR_AND_DIE("Cannot use DX12 in this way!")
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexIndexArray(numVerts, verts, numIndices, indices);
#endif
}

void Renderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices, VertexBuffer* vbo, IndexBuffer* ibo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexIndexArray(numVerts, verts, numIndices, indices, vbo, ibo);
#endif
#ifdef ENGINE_DX12_RENDERER
	//m_dx12Renderer->DrawVertexIndexArray()
	UNUSED(numVerts);
	UNUSED(verts);
	UNUSED(numIndices);
	UNUSED(indices);
	UNUSED(vbo);
	UNUSED(ibo);
	ERROR_AND_DIE("Cannot use DX12 in this way!")
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexIndexArray(numVerts, verts, numIndices, indices, vbo, ibo);
#endif
}

void Renderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexIndexArray(verts, indices);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexIndexArray(verts, indices);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexIndexArray(verts, indices);
#endif
}

void Renderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices, VertexBuffer* vbo, IndexBuffer* ibo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexIndexArray(verts, indices, vbo, ibo);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(verts);
	UNUSED(indices);
	UNUSED(vbo);
	UNUSED(ibo);
	ERROR_AND_DIE("Cannot use DX12 in this way!")
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexIndexArray(verts, indices, vbo, ibo);
#endif
}

void Renderer::DrawVertexArray(const std::vector<Vertex_PCU>& verts)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexArray(verts);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexArray(verts);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexArray(verts);
#endif
}

void Renderer::DrawAABB2(const AABB2& bounds, const Rgba8& color)
{
	Vertex_PCU verts[6];

	Vec2 bottomLeft = bounds.m_mins;
	Vec2 topLeft = Vec2(bounds.m_mins.x, bounds.m_maxs.y);
	Vec2 topRight = bounds.m_maxs;
	Vec2 bottomRight = Vec2(bounds.m_maxs.x, bounds.m_mins.y);

	verts[0] = Vertex_PCU(Vec3(bottomLeft.x, bottomLeft.y, 0.f), color, Vec2(0.f, 0.f));
	verts[1] = Vertex_PCU(Vec3(topLeft.x, topLeft.y, 0.f), color, Vec2(0.f, 1.f));
	verts[2] = Vertex_PCU(Vec3(topRight.x, topRight.y, 0.f), color, Vec2(1.f, 1.f));

	verts[3] = Vertex_PCU(Vec3(bottomLeft.x, bottomLeft.y, 0.f), color, Vec2(0.f, 0.f));
	verts[4] = Vertex_PCU(Vec3(topRight.x, topRight.y, 0.f), color, Vec2(1.f, 1.f));
	verts[5] = Vertex_PCU(Vec3(bottomRight.x, bottomRight.y, 0.f), color, Vec2(1.f, 0.f));

	DrawVertexArray(6, verts);
}

Image* Renderer::CreateImageFromFile(char const* imageFilePath)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateImageFromFile(imageFilePath);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateImageFromFile(imageFilePath);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateImageFromFile(imageFilePath);
#endif
}

Texture* Renderer::CreateTextureFromImage(const Image& image, bool usingMipmaps)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateTextureFromImage(image, usingMipmaps);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(usingMipmaps);
	return m_dx12Renderer->CreateTextureFromImage(image);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateTextureFromImage(image, usingMipmaps);
#endif
}

Texture* Renderer::CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateOrGetTextureFromFile(imageFilePath, usingMipmaps);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(usingMipmaps);
	return m_dx12Renderer->CreateOrGetTextureFromFile(imageFilePath);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateOrGetTextureFromFile(imageFilePath, usingMipmaps);
#endif
}

Texture* Renderer::CreateTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateTextureFromFile(imageFilePath, usingMipmaps);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(usingMipmaps);
	return m_dx12Renderer->CreateTextureFromFile(imageFilePath);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateTextureFromFile(imageFilePath, usingMipmaps);
#endif
}

Texture* Renderer::GetTextureForFileName(const char* imageFilePath)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->GetTextureForFileName(imageFilePath);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->GetTextureByFileName(imageFilePath);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->GetTextureForFileName(imageFilePath);
#endif
}

//------------------------------------------------------------------------------------------------
Texture* Renderer::CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel, uint8_t* texelData, bool usingMipmaps)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateTextureFromData(name, dimensions, bytesPerTexel, texelData, usingMipmaps);
#endif
 #ifdef ENGINE_DX12_RENDERER
	UNUSED(name);
	UNUSED(dimensions);
	UNUSED(bytesPerTexel);
	UNUSED(texelData);
	UNUSED(usingMipmaps);
 	ERROR_AND_DIE("Cannot use DX12 in this way!")
 	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateTextureFromData(name, dimensions, bytesPerTexel, texelData, usingMipmaps);
#endif
}

//-----------------------------------------------------------------------------------------------
void Renderer::BindTexture(const Texture* texture, int slot)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindTexture(texture,slot);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BindTexture(texture,slot);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindTexture(texture, slot);
#endif
}

BitmapFont* Renderer::CreateOrGetBitmapFont(const char* bitmapFontFilePathWithNoExtension)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateOrGetBitmapFont(bitmapFontFilePathWithNoExtension);
#endif
	#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateOrGetBitmapFont(bitmapFontFilePathWithNoExtension);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateOrGetBitmapFont(bitmapFontFilePathWithNoExtension);
#endif
}

BitmapFont* Renderer::CreateOrGetProportionalFont(const char* bitmapFontFilePathWithNoExtension)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateOrGetProportionalFont(bitmapFontFilePathWithNoExtension);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(bitmapFontFilePathWithNoExtension);
	return nullptr;
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(bitmapFontFilePathWithNoExtension);
	return nullptr;
#endif
}

BMFont* Renderer::CreateOrGetBMFont(const char* fntFilePath)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateOrGetBMFont(fntFilePath);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(fntFilePath);
	return nullptr;
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(fntFilePath);
	return nullptr;
#endif
}

void Renderer::SetFontConstants(float sdfThreshold, float sdfSmoothRange, float time, float weight)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetFontConstants(sdfThreshold, sdfSmoothRange, time, weight);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(sdfThreshold); UNUSED(sdfSmoothRange); UNUSED(time); UNUSED(weight);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(sdfThreshold); UNUSED(sdfSmoothRange); UNUSED(time); UNUSED(weight);
#endif
}

Shader* Renderer::CreateShader(char const* shaderName, char const* shaderSource, VertexType vertexType)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateShader(shaderName, shaderSource, vertexType);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateShader(shaderName, shaderSource, vertexType);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateShader(shaderName, shaderSource, vertexType);
#endif
}

Shader* Renderer::CreateShader(char const* shaderName, VertexType vertexType)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateShader(shaderName, vertexType);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateShader(shaderName, vertexType);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateShader(shaderName, vertexType);
#endif
}

Shader* Renderer::CreateOrGetShader(char const* shaderName, VertexType vertexType)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateOrGetShader(shaderName, vertexType);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateShader(shaderName, vertexType);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateOrGetShader(shaderName, vertexType);
#endif
}

bool Renderer::CompileShaderToByteCode(char const* name, char const* source, char const* entryPoint, char const* target, std::vector<unsigned char>& outByteCode, ID3DBlob** shaderByteCode)
{//只会在内部被call
#ifdef ENGINE_DX11_RENDERER
	UNUSED(shaderByteCode);
	return m_dx11Renderer->CompileShaderToByteCode(outByteCode, name, source, entryPoint, target);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(outByteCode);
	return m_dx12Renderer->CompileShaderToByteCode(shaderByteCode, name, source, entryPoint, target);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(name);
	UNUSED(source);
	UNUSED(entryPoint);
	UNUSED(target);
	UNUSED(outByteCode);
	UNUSED(shaderByteCode);
	return false; // Vulkan uses SPIR-V, not HLSL
#endif
}

void Renderer::BindShader(Shader* shader)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindShader(shader);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BindShader(shader);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindShader(shader);
#endif
}

VertexBuffer* Renderer::CreateVertexBuffer(const unsigned int size, unsigned int stride)
{
	#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateVertexBuffer(size, stride);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateVertexBuffer(size, stride);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateVertexBuffer(size, stride);
#endif
}

void Renderer::CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CopyCPUToGPU(data, size, vbo);
#endif
	
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->CopyCPUToGPU(data, size, vbo);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->CopyCPUToGPU(data, size, vbo);
#endif
}

void Renderer::BindVertexBuffer(VertexBuffer* vbo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindVertexBuffer(vbo);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BindVertexBuffer(vbo);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindVertexBuffer(vbo);
#endif
}

IndexBuffer* Renderer::CreateIndexBuffer(const unsigned int size, unsigned int stride)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateIndexBuffer(size, stride);
#endif
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->CreateIndexBuffer(size, stride);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateIndexBuffer(size, stride);
#endif
}

void Renderer::BindIndexBuffer(IndexBuffer* ibo)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindIndexBuffer(ibo);
#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BindIndexBuffer(ibo);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindIndexBuffer(ibo);
#endif
}

void Renderer::DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, PrimitiveTopology topology)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawIndexBuffer(vbo, ibo, indexCount, topology);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(topology)
	m_dx12Renderer->DrawIndexedVertexBuffer(vbo, ibo, indexCount);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawIndexBuffer(vbo, ibo, indexCount, topology);
#endif
}

void Renderer::CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CopyCPUToGPU(data, size, ibo);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->CopyCPUToGPU(data, size, ibo);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->CopyCPUToGPU(data, size, ibo);
#endif
}

ConstantBuffer* Renderer::CreateConstantBuffer(const unsigned int size)
{
#ifdef ENGINE_DX11_RENDERER
	return m_dx11Renderer->CreateConstantBuffer(size);
#endif
	#ifdef ENGINE_DX12_RENDERER
	UNUSED(size);
	ERROR_AND_DIE("Cannot create a ConstantBuffer in DX12 interface!");
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	return m_vulkanRenderer->CreateConstantBuffer(size);
#endif
}

void Renderer::CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CopyCPUToGPU(data, size, cbo);
#endif
#ifdef ENGINE_DX12_RENDERER
	//m_dx12Renderer->CopyCPUToGPU(data, size, cbo);
	UNUSED(data);
	UNUSED(size);
	UNUSED(cbo);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->CopyCPUToGPU(data, size, cbo);
#endif
}

void Renderer::BindConstantBuffer(int slot, ConstantBuffer* cbo)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindConstantBuffer(slot, cbo);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BindConstantBuffer(slot, cbo);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindConstantBuffer(slot, cbo);
#endif
}

void Renderer::DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->DrawVertexBuffer(vbo, vertexCount);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawVertexBuffer(vbo, vertexCount);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->DrawVertexBuffer(vbo, vertexCount);
#endif
}

void Renderer::SetBlendMode(BlendMode blendMode)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetBlendMode(blendMode);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetBlendMode(blendMode);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetBlendMode(blendMode);
#endif
}

void Renderer::SetBlendModeIfChanged()
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetBlendModeIfChanged();
#endif
#ifdef ENGINE_DX12_RENDERER
	//m_dx12Renderer->SetBlendModeIfChanged();
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetBlendModeIfChanged();
#endif
}

void Renderer::SetRasterizerModeIfChanged()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetRasterizerModeIfChanged();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetRasterizerModeIfChanged();
#endif
}

void Renderer::SetRasterizerMode(RasterizerMode rasterizerMode)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetRasterizerMode(rasterizerMode);
#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetRasterizerMode(rasterizerMode);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetRasterizerMode(rasterizerMode);
#endif
}

void Renderer::SetSamplerMode(SamplerMode samplerMode, int slot)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetSamplerMode(samplerMode, slot);
#endif
	#ifdef ENGINE_DX12_RENDERER
	UNUSED(slot);
	m_dx12Renderer->SetSamplerMode(samplerMode);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetSamplerMode(samplerMode, slot);
#endif
}

void Renderer::SetSamplerModeIfChanged()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetSamplerModeIfChanged();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetSamplerModeIfChanged();
#endif
}

void Renderer::SetSamplerModeIfChanged(int slot)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetSamplerModeIfChanged(slot);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(slot);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetSamplerModeIfChanged(slot);
#endif
}

void Renderer::SetDepthMode(DepthMode depthMode)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetDepthMode(depthMode);
#endif
	#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetDepthMode(depthMode);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetDepthMode(depthMode);
#endif
}

void Renderer::SetDepthModeIfChanged()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetDepthModeIfChanged();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetDepthModeIfChanged();
#endif
}

void Renderer::SetRenderMode(RenderMode renderMode)
{
#ifdef ENGINE_DX11_RENDERER
	UNUSED(renderMode);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->BeginRenderPass(renderMode);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	UNUSED(renderMode); // Vulkan doesn't use this interface
#endif
}

void Renderer::SetGeneralLightConstants(const Rgba8 sunColor, const Vec3& sunNormal, int numLights,
                                        std::vector<Rgba8> colors, std::vector<Vec3> worldPositions, std::vector<Vec3> spotForwards,
                                        std::vector<float> ambiences, std::vector<float> innerRadii, std::vector<float> outerRadii,
                                        std::vector<float> innerDotThresholds, std::vector<float> outerDotThresholds)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetGeneralLightConstants(sunColor, sunNormal, numLights,
										colors, worldPositions, spotForwards,
										ambiences, innerRadii, outerRadii,
										innerDotThresholds, outerDotThresholds);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetGeneralLightConstants(sunColor, sunNormal, numLights,
		colors, worldPositions, spotForwards,
		ambiences, innerRadii, outerRadii,
		innerDotThresholds, outerDotThresholds);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetGeneralLightConstants(sunColor, sunNormal, numLights,
		colors, worldPositions, spotForwards,
		ambiences, innerRadii, outerRadii,
		innerDotThresholds, outerDotThresholds);
#endif
}

void Renderer::SetModelConstants(const Mat44& modelToWorldTransform, const Rgba8& modelColor)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetModelConstants(modelToWorldTransform, modelColor);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetModelConstants(modelToWorldTransform, modelColor);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetModelConstants(modelToWorldTransform, modelColor);
#endif
}

void Renderer::SetMaterialConstants(const Texture* diffuseTex, const Texture* normalTex, const Texture* specularTex)
{
#ifdef ENGINE_DX11_RENDERER
	UNUSED(diffuseTex);
	UNUSED(normalTex);
	UNUSED(specularTex);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetMaterialConstants(diffuseTex, normalTex, specularTex);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetMaterialConstants(diffuseTex, normalTex, specularTex);
#endif
}

void Renderer::ResetInstanceData()
{
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->ResetInstanceData();
#endif
}

uint32_t Renderer::AppendInstanceData(const Mat44& worldMatrix, const Rgba8& color)
{
#ifdef ENGINE_DX12_RENDERER
	return m_dx12Renderer->AppendInstanceData(worldMatrix, color);
#else
	UNUSED(worldMatrix);
	UNUSED(color);
	return 0;
#endif
}

void Renderer::DrawIndexedInstancedBatch(VertexBuffer* vbo, IndexBuffer* ibo,
	int indexCount, uint32_t startInstance, uint32_t instanceCount)
{
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->DrawIndexedInstancedBatch(vbo, ibo, indexCount, startInstance, instanceCount);
#else
	UNUSED(vbo);
	UNUSED(ibo);
	UNUSED(indexCount);
	UNUSED(startInstance);
	UNUSED(instanceCount);
#endif
}

void Renderer::SetShadowConstants(const Mat44& lightViewProjectionMatrix)
{
	#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetShadowConstants(lightViewProjectionMatrix);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(lightViewProjectionMatrix);
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetShadowConstants(lightViewProjectionMatrix);
#endif
}

void Renderer::SetPerFrameConstants(const float time, const int debugInt, const float debugFloat)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetPerFrameConstants(time, debugInt, debugFloat);
#endif
#ifdef ENGINE_DX12_RENDERER
	m_dx12Renderer->SetPerFrameConstants(time, debugInt, debugFloat);
	#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->SetPerFrameConstants(time, debugInt, debugFloat);
#endif
}

//------------------------------------------------------
//Functions called in Startup()
void Renderer::CreateDeviceAndSwapChain(unsigned int deviceFlags)
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateDeviceAndSwapChain(deviceFlags);
#endif
#ifdef ENGINE_DX12_RENDERER
	UNUSED(deviceFlags);
#endif
}

void Renderer::GetBackBufferTexture()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->GetBackBufferTexture();
#endif
}

void Renderer::SetDefaultTexture()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->SetDefaultTexture();
#endif
}

void Renderer::CreateAndBindDefaultShader()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateAndBindDefaultShader();
#endif
}

void Renderer::CreateSampleStates()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateSampleStates();
#endif
}

void Renderer::CreateBlendStates()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateBlendStates();
#endif
}

void Renderer::CreateRasterizerStates()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateRasterizerStates();
#endif
}

void Renderer::CreateDepthStencilStates()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateDepthStencilStates();
#endif
}

void Renderer::CreateDepthStencilTextureAndView()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateDepthStencilTextureAndView();
#endif
}

void Renderer::CreateShadowMapResources()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateShadowMapResources();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->CreateShadowMapResources();
#endif
}

void Renderer::BeginShadowPass()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BeginShadowPass();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BeginShadowPass();
#endif
}

void Renderer::EndShadowPass()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->EndShadowPass();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->EndShadowPass();
#endif
}

void Renderer::CreateShadowMapShader() //Only create
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->CreateShadowMapShader();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->CreateShadowMapShader();
#endif
}

void Renderer::BindShadowMapTextureAndSampler()
{
#ifdef ENGINE_DX11_RENDERER
	m_dx11Renderer->BindShadowMapTextureAndSampler();
#endif
#ifdef ENGINE_VULKAN_RENDERER
	m_vulkanRenderer->BindShadowMapTextureAndSampler();
#endif
}