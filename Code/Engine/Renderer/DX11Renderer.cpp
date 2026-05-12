//#define WIN32_LEAN_AND_MEAN
#include "Engine/Renderer/DX11Renderer.hpp"

#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Renderer/BMFont.hpp"
#include "Engine/Renderer/Renderer.hpp"
//#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Renderer/Shader.hpp"
#include "Engine/Renderer/VertexBuffer.hpp"
#include "Engine/Renderer/IndexBuffer.hpp"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/DefaultShader.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"

#include "Engine/Core/FileUtils.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Core/StringUtils.hpp"
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

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"
#include "ThirdParty/ImGui/imgui_impl_dx11.h"

#ifdef ENGINE_DX11_RENDERER

//#pragma comment( lib, "opengl32" )

//Add dx11
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
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

#if defined(OPAQUE)
#undef OPAQUE
#endif

DX11Renderer::DX11Renderer(RendererConfig config)
{
	m_config = config;
}

void DX11Renderer::Startup()
{
	//CreateRenderingContext();
	//Create debug module
#if defined(ENGINE_DEBUG_RENDER)
	m_dxgiDebugModule = (void*)::LoadLibraryA("DXGIDebug.dll");
	if (m_dxgiDebugModule == nullptr)
	{
		ERROR_AND_DIE("Could not load dxgidebug.dll.");
	}

	typedef HRESULT(WINAPI* GetDebugModuleCB)(REFIID, void**);
	((GetDebugModuleCB)::GetProcAddress((HMODULE)m_dxgiDebugModule, "DXGIGetDebugInterface"))
		(__uuidof(IDXGIDebug), &m_dxgiDebug);

	if (m_dxgiDebug == nullptr)
	{
		ERROR_AND_DIE("Could not load debug module.");
	}
#endif

	//Create a local DXGI_SWAP_CHAIN_DESC variable and set its values as follows
	unsigned int deviceFlags = 0;
#if defined(ENGINE_DEBUG_RENDER)
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	CreateDeviceAndSwapChain(deviceFlags);
	GetBackBufferTexture();

	CreateAndBindDefaultShader();

	//Create vertex buffer big enough
	m_immediateVBO = CreateVertexBuffer(sizeof(Vertex_PCU), sizeof(Vertex_PCU));
	m_immediateVBOForVertex_PCUTBN = CreateVertexBuffer(sizeof(Vertex_PCUTBN), sizeof(Vertex_PCUTBN));
	m_immediateVBOForVertex_Font = CreateVertexBuffer(sizeof(Vertex_Font), sizeof(Vertex_Font));
	m_immediateIBO = CreateIndexBuffer(sizeof(unsigned int), sizeof(unsigned int));

	//Create a constant buffer large enough to fit the structure contents.
	m_cameraCBO = CreateConstantBuffer(sizeof(CameraConstants));
	m_modelCBO = CreateConstantBuffer(sizeof(ModelConstants));

	m_generalLightCBO = CreateConstantBuffer(sizeof(GeneralLightConstants));
#ifdef ENGINE_PAST_VERSION_LIGHTS
	m_lightCBO = CreateConstantBuffer(sizeof(LightConstants));
	m_pointLightCBO = CreateConstantBuffer(sizeof(PointLightConstants) * 10); //Max: 10 dianguang
	m_spotLightCBO = CreateConstantBuffer(sizeof(SpotLightConstants) * 4); //Max: 4 sheguang
#endif
	m_shadowCBO = CreateConstantBuffer(sizeof(Mat44)); //TODO: 有明显问题
	m_perFrameCBO = CreateConstantBuffer(sizeof(PerFrameConstants));
	m_fontCBO = CreateConstantBuffer(sizeof(FontConstants));

	CreateRasterizerStates();
	SetRasterizerMode(m_desiredRasterizerMode);

	CreateBlendStates();
	SetBlendMode(m_desiredBlendMode);

	SetDefaultTexture();
	BindTexture(m_defaultTexture);
	BindTexture(m_defaultNormalTexture, 1);
	BindTexture(m_defaultSpecTexture, 2);

	CreateSampleStates();
	SetSamplerMode(m_desiredSamplerMode);

	CreateDepthStencilTextureAndView();
	CreateDepthStencilStates();
	SetDepthMode(m_desiredDepthMode);

	CreateShadowMapShader();
	CreateShadowMapResources(); //ShadowMap
	BindShadowMapTextureAndSampler();

	ImGuiStartUp();
}

void DX11Renderer::ShutDown()
{
	/*    HDC hdc = GetDC(static_cast<HWND>(m_windowHandle));
		wglDeleteContext(wglGetCurrentContext());
		ReleaseDC(static_cast<HWND>(m_windowHandle), hdc);  */

	// Drop every AddRef the device context is holding via IA/VS/PS/OM/RS bindings,
	// otherwise those bound shaders/states/views leak when the context is released.
	if (m_deviceContext)
	{
		m_deviceContext->ClearState();
		m_deviceContext->Flush();
	}

	ImGuiShutdown();

	for (Shader* shader : m_loadedShaders)
	{
		delete shader;
		shader = nullptr;
	}
	m_loadedShaders.clear();
	//Delete all the shaders in the cache
	//delete m_currentShader;
	m_currentShader = nullptr;
	//delete m_defaultShader;
	m_defaultShader = nullptr;

	for (Texture* texture : m_loadedTextures)
	{
		delete texture;
		texture = nullptr;
	}
	m_loadedTextures.clear();

	m_defaultTexture = nullptr;

	for (BitmapFont* bitmapFont : m_loadedFonts)
	{
		delete bitmapFont;
		bitmapFont = nullptr;
	}
	m_loadedFonts.clear();

	delete m_immediateVBO;
	m_immediateVBO = nullptr;

	delete m_immediateVBOForVertex_PCUTBN;
	m_immediateVBOForVertex_PCUTBN = nullptr;

	delete m_immediateVBOForVertex_Font;
	m_immediateVBOForVertex_Font = nullptr;

	delete m_immediateIBO;
	m_immediateIBO = nullptr;

	delete m_cameraCBO;
	m_cameraCBO = nullptr;

	delete m_modelCBO;
	m_modelCBO = nullptr;

	delete m_generalLightCBO;
	m_generalLightCBO = nullptr;

#ifdef ENGINE_PAST_VERSION_LIGHTS
	delete m_lightCBO;
	m_lightCBO = nullptr;
	delete m_pointLightCBO;
	m_pointLightCBO = nullptr;
	delete m_spotLightCBO;
	m_spotLightCBO = nullptr;
#endif
	delete m_shadowCBO;
	m_shadowCBO = nullptr;

	delete m_perFrameCBO;
	m_perFrameCBO = nullptr;

	delete m_fontCBO;
	m_fontCBO = nullptr;

	//clear states and null their common pointers
	for (ID3D11BlendState*& blendState : m_blendStates)
	{
		DX_SAFE_RELEASE(blendState);
	}
	for (ID3D11RasterizerState*& rasterizerState : m_rasterizerStates)
	{
		DX_SAFE_RELEASE(rasterizerState);
	}
	for (ID3D11SamplerState*& samplerState : m_samplerStates)
	{
		DX_SAFE_RELEASE(samplerState);
	}
	for (ID3D11DepthStencilState*& depthStencilState : m_depthStencilStates)
	{
		DX_SAFE_RELEASE(depthStencilState);
	}
	m_rasterizerState = nullptr;
	m_blendState = nullptr;
	m_samplerState = nullptr;
	m_depthStencilState = nullptr;

	m_shaderSource = nullptr;

	//Release all DirectX objects and check for memory leaks here
	DX_SAFE_RELEASE(m_depthStencilTexture)
		DX_SAFE_RELEASE(m_depthStencilDSV) //depth mode de
		DX_SAFE_RELEASE(m_shadowDSV) //TODO: check shadowmap function
		DX_SAFE_RELEASE(m_shadowSRV) //TODO: check shadowmap function
		DX_SAFE_RELEASE(m_shadowDepthTexture) //TODO: check shadowmap function
		DX_SAFE_RELEASE(m_renderTargetView)
		DX_SAFE_RELEASE(m_swapChain)
		DX_SAFE_RELEASE(m_deviceContext)
		DX_SAFE_RELEASE(m_device)

		//report error leaks and release debug module
#if defined(ENGINE_DEBUG_RENDER)
		((IDXGIDebug*)m_dxgiDebug)->ReportLiveObjects(
			DXGI_DEBUG_ALL,
			(DXGI_DEBUG_RLO_FLAGS)(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)
		);

	((IDXGIDebug*)m_dxgiDebug)->Release();
	m_dxgiDebug = nullptr;

	::FreeLibrary((HMODULE)m_dxgiDebugModule);
	m_dxgiDebugModule = nullptr;
#endif
}

//void Renderer::CreateRenderingContext()
//{
//    PIXELFORMATDESCRIPTOR pixelFormatDescriptor;
//    memset(&pixelFormatDescriptor, 0, sizeof(pixelFormatDescriptor));
//    pixelFormatDescriptor.nSize = sizeof(pixelFormatDescriptor);
//    pixelFormatDescriptor.nVersion = 1;
//    pixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
//    pixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
//    pixelFormatDescriptor.cColorBits = 24;
//    pixelFormatDescriptor.cDepthBits = 24;
//    pixelFormatDescriptor.cAccumBits = 0;
//    pixelFormatDescriptor.cStencilBits = 8;
//
//    // These two OpenGL-like functions (wglCreateContext and wglMakeCurrent) will remain here for now.
//    int pixelFormatCode = ChoosePixelFormat((HDC)m_config.m_window->GetDisplayContext(), &pixelFormatDescriptor);
//    SetPixelFormat((HDC)m_config.m_window->GetDisplayContext(), pixelFormatCode, &pixelFormatDescriptor);
//    g_openGLRenderingContext = wglCreateContext((HDC)m_config.m_window->GetDisplayContext());
//    wglMakeCurrent((HDC)m_config.m_window->GetDisplayContext(), g_openGLRenderingContext);
//
//    // #SD1ToDo: move all OpenGL functions (including those below) to Renderer.cpp (only!)
//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//}

void DX11Renderer::BeginFrame()
{
	//Set render target, also set the depth stencil view
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilDSV);
	ImGuiBeginFrame();
}

void DX11Renderer::EndFrame()
{
	if (m_config.m_window)
	{
		//SwapBuffers((HDC)m_config.m_window->GetDisplayContext());
	}

	ImGuiEndFrame();
	
	//Present
	HRESULT hr;
	//Sleep(1);
	hr = m_swapChain->Present(0, 0);
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		ERROR_AND_DIE("Device has been lost, application will now terminate.");
	}
}

void DX11Renderer::ImGuiStartUp()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// 如果你在 DX12 里启了 Docking/Viewports，这里也能开：
	// io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();

	// 注意：DX11 后端只需要 Device + DeviceContext
	// 这些成员在 CreateDeviceAndSwapChain() 里创建过：m_device, m_deviceContext
	ImGui_ImplWin32_Init(g_theWindow->GetHwnd());
	ImGui_ImplDX11_Init(m_device, m_deviceContext);
}

void DX11Renderer::ImGuiBeginFrame()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void DX11Renderer::ImGuiEndFrame()
{
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void DX11Renderer::ImGuiShutdown()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyPlatformWindows();
	ImGui::DestroyContext();
}

void DX11Renderer::ClearScreen(const Rgba8& clearColor)
{
	/*glClearColor(clearColor.r / 255.0f, clearColor.g / 255.0f, clearColor.b / 255.0f, clearColor.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);*/

	//Clear the screen
	float colorAsFloats[4];
	clearColor.GetAsFloats(colorAsFloats);
	m_deviceContext->ClearRenderTargetView(m_renderTargetView, colorAsFloats);
	//When clearing the screen, also clear the depth stencil view
	m_deviceContext->ClearDepthStencilView(m_depthStencilDSV,
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
}

void DX11Renderer::ClearScreen()
{
	//Clear the screen
	Rgba8 clearColor(255, 0, 255, 255);  //Use magenta
	float colorAsFloats[4];
	clearColor.GetAsFloats(colorAsFloats);
	m_deviceContext->ClearRenderTargetView(m_renderTargetView, colorAsFloats);
	//When clearing the screen, also clear the depth stencil view
	m_deviceContext->ClearDepthStencilView(m_depthStencilDSV,
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
}

void DX11Renderer::BeginCamera(const Camera& camera)
{
	//   //glLoadIdentity();
	//   //Vec2 bottomLeft = camera.GetOrthoBottomLeft();
	//   //Vec2 topRight = camera.GetOrthoTopRight();
	//   //glOrtho(bottomLeft.x, topRight.x, bottomLeft.y, topRight.y, 0.0f, 1.0f);  // 设置正交视角

	   //Set the viewport
	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.f;
	viewport.TopLeftY = 0.f;
	viewport.Width = (float)g_theWindow->GetClientDimensions().x;
	viewport.Height = (float)g_theWindow->GetClientDimensions().y;
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;

	//Call ID3D11DeviceContext::RSSetViewports
	m_deviceContext->RSSetViewports(1, &viewport);

	/*Vec2 bottomLeft = camera.GetOrthographicBottomLeft();
	Vec2 topRight = camera.GetOrthographicTopRight();*/

	CameraConstants localCameraConstants;
	localCameraConstants.WorldToCameraTransform = camera.GetWorldToCameraTransform();
	localCameraConstants.CameraToRenderTransform = camera.GetCameraToRenderTransform();
	localCameraConstants.RenderToClipTransform = camera.GetRenderToClipTransform();
	localCameraConstants.CameraWorldPosition = camera.GetPosition();
	/*localCameraConstants.OrthoMinX = bottomLeft.x;
	localCameraConstants.OrthoMinY = bottomLeft.y;
	localCameraConstants.OrthoMinZ = 0.f;
	localCameraConstants.OrthoMaxX = topRight.x;
	localCameraConstants.OrthoMaxY = topRight.y;
	localCameraConstants.OrthoMaxZ = 1.f;*/

	CopyCPUToGPU(&localCameraConstants, sizeof(CameraConstants), m_cameraCBO);
	BindConstantBuffer(k_cameraConstantsSlot, m_cameraCBO);

	SetModelConstants();
}

void DX11Renderer::EndCamera(const Camera& camera)
{
	(void)camera;
}

void DX11Renderer::SetViewport(const AABB2& normalizedViewport)
{
	IntVec2 clientDimensions = g_theWindow->GetClientDimensions();

	Vec2 screenMins = Vec2(
		normalizedViewport.m_mins.x * clientDimensions.x,
		normalizedViewport.m_mins.y * clientDimensions.y);

	Vec2 screenMaxs = Vec2(
		normalizedViewport.m_maxs.x * clientDimensions.x,
		normalizedViewport.m_maxs.y * clientDimensions.y);

	Vec2 screenSize = screenMaxs - screenMins;

	D3D11_VIEWPORT dxViewport = {};
	dxViewport.TopLeftX = screenMins.x;
	dxViewport.TopLeftY = screenMins.y;
	dxViewport.Width = screenSize.x;
	dxViewport.Height = screenSize.y;
	dxViewport.MinDepth = 0.f;
	dxViewport.MaxDepth = 1.f;

	m_deviceContext->RSSetViewports(1, &dxViewport);
}

void DX11Renderer::DrawVertexArray(int numVerts, const Vertex_PCU* verts)
{
	/*glBegin(GL_TRIANGLES);

	for (int i = 0; i < numVerts; ++i)
	{
		const Vertex_PCU& vertex = verts[i];
		glColor4ub(vertex.m_color.r, vertex.m_color.g, vertex.m_color.b, vertex.m_color.a);
		glTexCoord2f(vertex.m_uvTextColors.x, vertex.m_uvTextColors.y);
		glVertex3f(vertex.m_position.x, vertex.m_position.y, vertex.m_position.z);
	}

	glEnd();*/

	if (numVerts == 0 || verts == nullptr)
	{
		return;
	}

	unsigned int vertexBufferSize = numVerts * sizeof(Vertex_PCU);
	CopyCPUToGPU(verts, vertexBufferSize, m_immediateVBO);
	DrawVertexBuffer(m_immediateVBO, numVerts);
}

void DX11Renderer::DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts)
{
	DrawVertexArray(static_cast<int>(verts.size()), verts.data());
}

void DX11Renderer::DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts)
{
	if (numVerts == 0 || verts == nullptr)
	{
		return;
	}

	unsigned int vertexBufferSize = numVerts * sizeof(Vertex_PCUTBN);
	CopyCPUToGPU(verts, vertexBufferSize, m_immediateVBOForVertex_PCUTBN);
	DrawVertexBuffer(m_immediateVBOForVertex_PCUTBN, numVerts);
}

void DX11Renderer::DrawVertexArray(const std::vector<Vertex_Font>& verts)
{
	DrawVertexArray(static_cast<int>(verts.size()), verts.data());
}

void DX11Renderer::DrawVertexArray(int numVerts, const Vertex_Font* verts)
{
	if (numVerts == 0 || verts == nullptr)
		return;

	unsigned int vertexBufferSize = numVerts * sizeof(Vertex_Font);
	CopyCPUToGPU(verts, vertexBufferSize, m_immediateVBOForVertex_Font);
	DrawVertexBuffer(m_immediateVBOForVertex_Font, numVerts);
}

void DX11Renderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices)
{
	if (numVerts == 0 || verts == nullptr || numIndices == 0 || indices == nullptr)
	{
		return;
	}

	unsigned int vertexBufferSize = numVerts * sizeof(Vertex_PCUTBN);
	unsigned int indexBufferSize = numIndices * sizeof(unsigned int);

	CopyCPUToGPU(verts, vertexBufferSize, m_immediateVBO);
	CopyCPUToGPU(indices, indexBufferSize, m_immediateIBO);

	//DrawVertexBuffer(m_immediateVBO, numVerts);
	//DrawIndexBuffer(m_immediateIBO, numIndices);
	BindVertexBuffer(m_immediateVBO);
	BindIndexBuffer(m_immediateIBO);
	m_deviceContext->DrawIndexed(numIndices, 0, 0);
}

void DX11Renderer::DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices, VertexBuffer* vbo, IndexBuffer* ibo)
{
	if (numVerts == 0 || verts == nullptr || numIndices == 0 || indices == nullptr)
	{
		return;
	}

	unsigned int vertexBufferSize = numVerts * sizeof(Vertex_PCUTBN);
	unsigned int indexBufferSize = numIndices * sizeof(unsigned int);

	CopyCPUToGPU(verts, vertexBufferSize, vbo);
	CopyCPUToGPU(indices, indexBufferSize, ibo);

	BindVertexBuffer(vbo);
	BindIndexBuffer(ibo);
	m_deviceContext->DrawIndexed(numIndices, 0, 0);
}

void DX11Renderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices)
{
	DrawVertexIndexArray(static_cast<int>(verts.size()), verts.data(), static_cast<int>(indices.size()), indices.data());
}

void DX11Renderer::DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices, VertexBuffer* vbo, IndexBuffer* ibo)
{
	DrawVertexIndexArray(static_cast<int>(verts.size()), verts.data(), static_cast<int>(indices.size()), indices.data(), vbo, ibo);
}

void DX11Renderer::DrawVertexArray(const std::vector<Vertex_PCU>& verts)
{
	DrawVertexArray(static_cast<int>(verts.size()), verts.data());
}

Image* DX11Renderer::CreateImageFromFile(char const* imageFilePath)
{
	return new Image(imageFilePath);
}

Texture* DX11Renderer::CreateTextureFromImage(const Image& image, bool usingMipmaps)
{
	Texture* newTexture = new Texture();
    newTexture->m_name = image.GetImageFilePath();
    newTexture->m_dimensions = image.GetDimensions();

    int width = image.GetDimensions().x;
    int height = image.GetDimensions().y;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;

    HRESULT hr;

    if (usingMipmaps)
    {
        textureDesc.MipLevels = 0;  
        textureDesc.Usage = D3D11_USAGE_DEFAULT; 
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;  
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;  

        hr = m_device->CreateTexture2D(&textureDesc, nullptr, &newTexture->m_texture);
        if (!SUCCEEDED(hr))
        {
            ERROR_AND_DIE(Stringf("CreateTexture2D failed for image file \"%s\".",
                image.GetImageFilePath().c_str()));
        }

        m_deviceContext->UpdateSubresource(
            newTexture->m_texture,
            0,                      
            nullptr,                
            image.GetRawData(),     
            width * 4,              
            0                       
        );

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = (UINT) - 1;  // -1 = 使用所有可用的 Mipmap 级别

        hr = m_device->CreateShaderResourceView(newTexture->m_texture, &srvDesc, &newTexture->m_shaderResourceView);
        if (!SUCCEEDED(hr))
        {
            ERROR_AND_DIE(Stringf("CreateShaderResourceView failed for image file \"%s\".",
                image.GetImageFilePath().c_str()));
        }
        m_deviceContext->GenerateMips(newTexture->m_shaderResourceView);
    }
    else
    {
        textureDesc.MipLevels = 1; 
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;  
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        textureDesc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA textureData;
        textureData.pSysMem = image.GetRawData();
        textureData.SysMemPitch = 4 * width;

        hr = m_device->CreateTexture2D(&textureDesc, &textureData, &newTexture->m_texture);
        if (!SUCCEEDED(hr))
        {
            ERROR_AND_DIE(Stringf("CreateTextureFromImage failed for image file \"%s\".",
                image.GetImageFilePath().c_str()));
        }
        hr = m_device->CreateShaderResourceView(newTexture->m_texture, NULL, &newTexture->m_shaderResourceView);
        if (!SUCCEEDED(hr))
        {
            ERROR_AND_DIE(Stringf("CreateShaderResourceView failed for image file \"%s\".",
                image.GetImageFilePath().c_str()));
        }
    }

    m_loadedTextures.push_back(newTexture);
    return newTexture;
}

Texture* DX11Renderer::CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps)
{
	// See if we already have this texture previously loaded
	Texture* existingTexture = GetTextureForFileName(imageFilePath);
	if (existingTexture)
	{
		return existingTexture;
	}

	// Never seen this texture before!  Let's load it.
	Texture* newTexture = CreateTextureFromFile(imageFilePath, usingMipmaps);
	return newTexture;
}

Texture* DX11Renderer::CreateTextureFromFile(char const* imageFilePath, bool usingMipmaps )
{
	IntVec2 dimensions = IntVec2::ZERO;		// This will be filled in for us to indicate image width & height
	int bytesPerTexel = 0; // This will be filled in for us to indicate how many color components the image had (e.g. 3=RGB=24bit, 4=RGBA=32bit)
	int numComponentsRequested = 0; // don't care; we support 3 (24-bit RGB) or 4 (32-bit RGBA)

	// Load (and decompress) the image RGB(A) bytes from a file on disk into a memory buffer (array of bytes)
	stbi_set_flip_vertically_on_load(1); // We prefer uvTexCoords has origin (0,0) at BOTTOM LEFT
	unsigned char* texelData = stbi_load(imageFilePath, &dimensions.x, &dimensions.y, &bytesPerTexel, numComponentsRequested);

	// Check if the load was successful
	GUARANTEE_OR_DIE(texelData, Stringf("Failed to load image \"%s\"", imageFilePath));

	Texture* newTexture = CreateTextureFromData(imageFilePath, dimensions, bytesPerTexel, texelData, usingMipmaps);

	// Free the raw image texel data now that we've sent a copy of it down to the GPU to be stored in video memory
	stbi_image_free(texelData);

	return newTexture;
}

Texture* DX11Renderer::GetTextureForFileName(const char* imageFilePath)
{
	for (int i = 0; i < m_loadedTextures.size(); i++)
	{
		if (m_loadedTextures[i]->GetImageFilePath() == imageFilePath)
		{
			return m_loadedTextures[i];
		}
	}
	return nullptr;
}

//------------------------------------------------------------------------------------------------
Texture* DX11Renderer::CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel, uint8_t* texelData, bool usingMipmaps)
{
	// Check if the load was successful
	GUARANTEE_OR_DIE(texelData, Stringf("CreateTextureFromData failed for \"%s\" - texelData was null!", name));
	GUARANTEE_OR_DIE(bytesPerTexel >= 3 && bytesPerTexel <= 4, Stringf("CreateTextureFromData failed for \"%s\" - unsupported BPP=%i (must be 3 or 4)", name, bytesPerTexel));
	GUARANTEE_OR_DIE(dimensions.x > 0 && dimensions.y > 0, Stringf("CreateTextureFromData failed for \"%s\" - illegal texture dimensions (%i x %i)", name, dimensions.x, dimensions.y));

	//Texture* newTexture = new Texture();
	//newTexture->m_name = name; // NOTE: m_name must be a std::string, otherwise it may point to temporary data!
	//newTexture->m_dimensions = dimensions;

	Image* image = CreateImageFromFile(name);
	// **转换 `texelData` 到 `m_rgbaTexelsData`**
	for (int i = 0; i < dimensions.x * dimensions.y; ++i)
	{
		uint8_t r = texelData[i * bytesPerTexel + 0];
		uint8_t g = texelData[i * bytesPerTexel + 1];
		uint8_t b = texelData[i * bytesPerTexel + 2];
		uint8_t a = (bytesPerTexel == 4) ? texelData[i * bytesPerTexel + 3] : 255;

		image->m_rgbaTexelsData[i] = Rgba8(r, g, b, a);
	}
	image->m_dimensions = dimensions;

	Texture* newTexture = CreateTextureFromImage(*image, usingMipmaps);

	return newTexture;

	//// Enable OpenGL texturing
	//glEnable(GL_TEXTURE_2D);
	//// Tell OpenGL that our pixel data is single-byte aligned
	//glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	//// Ask OpenGL for an unused texName (ID number) to use for this texture
	//glGenTextures(1, (GLuint*)&newTexture->m_openglTextureID);
	//// Tell OpenGL to bind (set) this as the currently active texture
	//glBindTexture(GL_TEXTURE_2D, newTexture->m_openglTextureID);
	//// Set texture clamp vs. wrap (repeat) default settings
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // GL_CLAMP or GL_REPEAT
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); // GL_CLAMP or GL_REPEAT
	//// Set magnification (texel > pixel) and minification (texel < pixel) filters
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // one of: GL_NEAREST, GL_LINEAR
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // one of: GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR
	//// Pick the appropriate OpenGL format (RGB or RGBA) for this texel data
	//GLenum bufferFormat = GL_RGBA; // the format our source pixel data is in; any of: GL_RGB, GL_RGBA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, ...
	//if (bytesPerTexel == 3)
	//{
	//	bufferFormat = GL_RGB;
	//}
	//GLenum internalFormat = bufferFormat; // the format we want the texture to be on the card; technically allows us to translate into a different texture format as we upload to OpenGL
	//// Upload the image texel data (raw pixels bytes) to OpenGL under this textureID
	//glTexImage2D(			// Upload this pixel data to our new OpenGL texture
	//	GL_TEXTURE_2D,		// Creating this as a 2d texture
	//	0,					// Which mipmap level to use as the "root" (0 = the highest-quality, full-res image), if mipmaps are enabled
	//	internalFormat,		// Type of texel format we want OpenGL to use for this texture internally on the video card
	//	dimensions.x,		// Texel-width of image; for maximum compatibility, use 2^N + 2^B, where N is some integer in the range [3,11], and B is the border thickness [0,1]
	//	dimensions.y,		// Texel-height of image; for maximum compatibility, use 2^M + 2^B, where M is some integer in the range [3,11], and B is the border thickness [0,1]
	//	0,					// Border size, in texels (must be 0 or 1, recommend 0)
	//	bufferFormat,		// Pixel format describing the composition of the pixel data in buffer
	//	GL_UNSIGNED_BYTE,	// Pixel color components are unsigned bytes (one byte per color channel/component)
	//	texelData);		// Address of the actual pixel data bytes/buffer in system memory
}

//-----------------------------------------------------------------------------------------------
void DX11Renderer::BindTexture(const Texture* texture, int slot)
{
	//if (texture)
	//{
	//	glEnable(GL_TEXTURE_2D);
	//	glBindTexture(GL_TEXTURE_2D, texture->m_openglTextureID);
	//}
	//else
	//{
	//	glDisable(GL_TEXTURE_2D);
	//}
	if (!texture && slot == 0)
	{
		BindTexture(m_defaultTexture, 0);
		return;
	}
	if (!texture && slot == 1)
	{
		BindTexture(m_defaultNormalTexture, 1);
		return;
	}
	if (!texture && slot == 2)
	{
		BindTexture(m_defaultSpecTexture, 2);
		return;
	}

	m_deviceContext->PSSetShaderResources(slot, 1, &texture->m_shaderResourceView);

	m_deviceContext->PSSetSamplers(slot, 1, &m_samplerState);
}

BitmapFont* DX11Renderer::CreateOrGetBitmapFont(const char* bitmapFontFilePathWithNoExtension)
{
	std::string fontTextureFilePath = std::string(bitmapFontFilePathWithNoExtension) + ".png";

	Texture* bitmapTexture = CreateOrGetTextureFromFile(fontTextureFilePath.c_str());
	if (!bitmapTexture)
	{
		return nullptr;
	}

	BitmapFont* bitmapFont = new BitmapFont(bitmapFontFilePathWithNoExtension, *bitmapTexture);
	if (!bitmapFont)
	{
		ERROR_AND_DIE(Stringf("Unkown bitmapFont"));
	}

	m_loadedFonts.push_back(bitmapFont);

	return bitmapFont;
}

BitmapFont* DX11Renderer::CreateOrGetProportionalFont(const char* bitmapFontFilePathWithNoExtension)
{
	std::string fontTextureFilePath = std::string(bitmapFontFilePathWithNoExtension) + ".png";
	Texture* bitmapTexture = CreateOrGetTextureFromFile(fontTextureFilePath.c_str());
	if (!bitmapTexture)
		return nullptr;

	BitmapFont* bitmapFont = new BitmapFont(bitmapFontFilePathWithNoExtension, *bitmapTexture, true);
	m_loadedFonts.push_back(bitmapFont);
	return bitmapFont;
}

BMFont* DX11Renderer::CreateOrGetBMFont(const char* fntFilePath)
{
	// Check cache
	for (BMFont* font : m_loadedBMFonts)
	{
		if (font->m_fntFilePath == fntFilePath)
			return font;
	}

	// Need a Renderer* to pass to BMFont - get it from the global
	extern Renderer* g_theRenderer;
	BMFont* font = new BMFont(fntFilePath, g_theRenderer);
	m_loadedBMFonts.push_back(font);
	return font;
}

void DX11Renderer::SetFontConstants(float sdfThreshold, float sdfSmoothRange, float time, float weight)
{
	FontConstants fc;
	fc.SDFThreshold = sdfThreshold;
	fc.SDFSmoothRange = sdfSmoothRange;
	fc.Time = time;
	fc.Weight = weight;
	CopyCPUToGPU(&fc, sizeof(fc), m_fontCBO);
	BindConstantBuffer(k_fontConstantsSlot, m_fontCBO);
}

Shader* DX11Renderer::CreateShader(char const* shaderName, char const* shaderSource, VertexType vertexType)
{
	std::vector<unsigned char> shaderByteCodeForVertex;
	std::vector<unsigned char> shaderByteCodeForPixel;

	ShaderConfig usedShaderConfig;
	usedShaderConfig.m_name = shaderName;

	CompileShaderToByteCode(shaderByteCodeForVertex, shaderName, shaderSource, usedShaderConfig.m_vertexEntryPoint.c_str(), "vs_5_0");
	ID3D11VertexShader* vertexShader = nullptr;
	m_device->CreateVertexShader(shaderByteCodeForVertex.data(), shaderByteCodeForVertex.size(), NULL, &vertexShader);

	CompileShaderToByteCode(shaderByteCodeForPixel, shaderName, shaderSource, usedShaderConfig.m_pixelEntryPoint.c_str(), "ps_5_0");
	ID3D11PixelShader* pixelShader = nullptr;
	m_device->CreatePixelShader(shaderByteCodeForPixel.data(), shaderByteCodeForPixel.size(), NULL, &pixelShader);
	//shaderByteCodeForVertex.clear();

	HRESULT hr;
	//Create a local array of input element descriptions that defines the vertex layout
	ID3D11InputLayout* inputLayout = nullptr;
	if (vertexType == VertexType::VERTEX_PCU)
	{
		D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		//Call ID3D11Device:: Create Input Layout
		//!!!!!Only use the byteCode of vertex shader to create inputLayout!!!!!!
		UINT numElements = ARRAYSIZE(inputElementDesc);
		hr = m_device->CreateInputLayout(
			inputElementDesc, numElements,
			shaderByteCodeForVertex.data(),
			shaderByteCodeForVertex.size(),
			&inputLayout
		);
		if (FAILED(hr))
		{
			ERROR_AND_DIE("Fail to create a inputlayout for VERTEX_PCU");
		}
	}
	if (vertexType == VertexType::VERTEX_PCUTBN)
	{
		D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		//Call ID3D11Device:: Create Input Layout
	//!!!!!Only use the byteCode of vertex shader to create inputLayout!!!!!!
		UINT numElements = ARRAYSIZE(inputElementDesc);
		hr = m_device->CreateInputLayout(
			inputElementDesc, numElements,
			shaderByteCodeForVertex.data(),
			shaderByteCodeForVertex.size(),
			&inputLayout
		);
		if (FAILED(hr))
		{
			ERROR_AND_DIE("Fail to create a inputlayout for VERTEX_PCUTBN");
		}
	}
	if (vertexType == VertexType::VERTEX_FONT)
	{
		D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 3, DXGI_FORMAT_R32_SINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		UINT numElements = ARRAYSIZE(inputElementDesc);
		hr = m_device->CreateInputLayout(
			inputElementDesc, numElements,
			shaderByteCodeForVertex.data(),
			shaderByteCodeForVertex.size(),
			&inputLayout
		);
		if (FAILED(hr))
		{
			ERROR_AND_DIE("Fail to create a inputlayout for VERTEX_FONT");
		}
	}

	Shader* newShader = new Shader(usedShaderConfig);
	newShader->m_vertexShader = vertexShader;
	newShader->m_pixelShader = pixelShader;
	newShader->m_inputLayout = inputLayout;

	m_loadedShaders.push_back(newShader);
	return newShader;
}

Shader* DX11Renderer::CreateShader(char const* shaderName, VertexType vertexType)
{
	//std::string shaderHLSLName = "Data/Shaders/" + std::string(shaderName) + ".hlsl";
	std::string shaderHLSLName = std::string(shaderName) + ".hlsl";

	std::string shaderSource;
	int result = FileReadToString(shaderSource, shaderHLSLName);
	if (result < 0)
	{
		ERROR_AND_DIE("Fail to create shader from this shaderName");
	}

	return CreateShader(shaderName, shaderSource.c_str(), vertexType);
}

Shader* DX11Renderer::CreateOrGetShader(char const* shaderName, VertexType vertexType)
{
	for (Shader* shader : m_loadedShaders)
	{
		if (shader->GetName() == shaderName)
		{
			return shader;
		}
	}

	//std::string shaderHLSLName = "Data/Shaders/" + std::string(shaderName) + ".hlsl";
	std::string shaderHLSLName = std::string(shaderName) + ".hlsl";

	std::string shaderSource;
	int result = FileReadToString(shaderSource, shaderHLSLName);
	if (result < 0)
	{
		ERROR_AND_DIE("Fail to create shader from this shaderName");
	}

	return CreateShader(shaderName, shaderSource.c_str(), vertexType);
}

bool DX11Renderer::CompileShaderToByteCode(std::vector<unsigned char>& outByteCode, char const* name, char const* source, char const* entryPoint, char const* target)
{
	DWORD shaderFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(ENGINE_DEBUG_RENDER)
	shaderFlags |= D3DCOMPILE_DEBUG;
	shaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
	shaderFlags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
#endif

	ID3DBlob* shaderBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	HRESULT hr = D3DCompile(
		source, strlen(source),
		name, nullptr, nullptr,
		entryPoint, target, shaderFlags,
		0, &shaderBlob, &errorBlob
	);

	if (FAILED(hr))
	{
		if (errorBlob)
		{
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			DebuggerPrintf("[D3DCompile Error]: %s\n", (char*)errorBlob->GetBufferPointer());
			errorBlob->Release();
		}
		std::string errorName = Stringf("Could not compile shader [%s] entry [%s]", name, entryPoint);
		ERROR_AND_DIE(errorName.c_str());
	}

	if (shaderBlob)
	{
		outByteCode.resize(shaderBlob->GetBufferSize());
		memcpy(outByteCode.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
		shaderBlob->Release();
	}

	if (errorBlob)
		errorBlob->Release();

	return true;
	// 	//Set compile flags
	// 	DWORD shaderFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	// #if defined(ENGINE_DEBUG_RENDER)
	// 	shaderFlags = D3DCOMPILE_DEBUG;
	// 	shaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
	// 	shaderFlags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
	// #endif
	// 	ID3DBlob* shaderBlob = NULL;
	// 	ID3DBlob* errorBlob = NULL;
	//
	// 	//Compile
	// 	HRESULT hr;
	// 	hr = D3DCompile(
	// 		source, strlen(source),
	// 		name, nullptr, nullptr,
	// 		entryPoint, target, shaderFlags,
	// 		0, &shaderBlob, &errorBlob
	// 	);
	// 	if (SUCCEEDED(hr))
	// 	{
	// 		outByteCode.resize(shaderBlob->GetBufferSize());
	// 		memcpy(
	// 			outByteCode.data(),
	// 			shaderBlob->GetBufferPointer(),
	// 			shaderBlob->GetBufferSize()
	// 		);
	//
	// 		shaderBlob->Release();
	// 		if (errorBlob != nullptr)
	// 		{
	// 			errorBlob->Release();
	// 		}
	//
	// 		return true;
	// 	}
	// 	else
	// 	{
	// 		if (errorBlob != NULL)
	// 		{
	// 			DebuggerPrintf((char*)errorBlob->GetBufferPointer());
	// 			errorBlob->Release();
	// 		}
	// 		ERROR_AND_DIE(Stringf("Could not compile vertex shader."));
	// 	}
}

void DX11Renderer::BindShader(Shader* shader)
{
	if (!shader)
	{
		BindShader(m_defaultShader);
		return;
	}

	//Call VSSetShader and PSSetShader with the provided shader parameter
	m_deviceContext->VSSetShader(shader->m_vertexShader, nullptr, 0);
	m_deviceContext->PSSetShader(shader->m_pixelShader, nullptr, 0);
	//Also set the input layout with IASetInputLayout.
	m_deviceContext->IASetInputLayout(shader->m_inputLayout);
}

VertexBuffer* DX11Renderer::CreateVertexBuffer(const unsigned int size, unsigned int stride)
{
	//return new VertexBuffer(m_device, size, stride);
	if (size == 0 || stride == 0)
	{
		ERROR_AND_DIE("Invalid vertex buffer size or stride.");
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = size;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	VertexBuffer* vertexBuffer = new VertexBuffer(m_device, size, stride);

	return vertexBuffer;
}

void DX11Renderer::CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo)
{
	if (vbo->m_size < size)
	{
		vbo->Resize(size);
	}

	//Copy vertices
	D3D11_MAPPED_SUBRESOURCE resource;
	m_deviceContext->Map(vbo->m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, data, size);
	m_deviceContext->Unmap(vbo->m_buffer, 0);
}

void DX11Renderer::BindVertexBuffer(VertexBuffer* vbo)
{
	//Set pipeline state
	UINT startOffset = 0; //代表从缓冲区的哪个字节开始读取，一般都是使用整个缓冲区
	m_deviceContext->IASetVertexBuffers(0, 1, &vbo->m_buffer, &vbo->m_stride, &startOffset);
	m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

IndexBuffer* DX11Renderer::CreateIndexBuffer(const unsigned int size, unsigned int stride)
{
	if (size == 0 || stride == 0)
	{
		ERROR_AND_DIE("Invalid index buffer size or stride.");
	}

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = size;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	IndexBuffer* indexBuffer = new IndexBuffer(m_device, size, stride);

	return indexBuffer;
}

void DX11Renderer::BindIndexBuffer(IndexBuffer* ibo)
{
	m_deviceContext->IASetIndexBuffer(ibo->m_buffer, DXGI_FORMAT_R32_UINT, 0);
	m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void DX11Renderer::DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, PrimitiveTopology topology)
{
	BindVertexBuffer(vbo);
	BindIndexBuffer(ibo);
	SetBlendModeIfChanged();
	SetRasterizerModeIfChanged();
	SetDepthModeIfChanged();
	SetSamplerModeIfChanged();

	switch(topology)
	{
	case PRIMITIVE_LINES:
		m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		break;
	case PRIMITIVE_LINE_STRIP:
		m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		break;
	case PRIMITIVE_POINTS:
		m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		break;
	default:
		m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;
	}
	
	m_deviceContext->DrawIndexed(indexCount, 0, 0);

	//m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void DX11Renderer::CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo)
{
	if (ibo->m_size < size)
	{
		ibo->Resize(size);
	}

	//Copy vertices
	D3D11_MAPPED_SUBRESOURCE resource;
	m_deviceContext->Map(ibo->m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, data, size);
	m_deviceContext->Unmap(ibo->m_buffer, 0);
}

ConstantBuffer* DX11Renderer::CreateConstantBuffer(const unsigned int size)
{
	if (size == 0)
	{
		ERROR_AND_DIE("Invalid constant buffer size.");
	}
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.ByteWidth = (UINT)size;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ConstantBuffer* constantBuffer = new ConstantBuffer(m_device, size);

	return constantBuffer;
}

void DX11Renderer::CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo)
{
	//Copy vertices
	D3D11_MAPPED_SUBRESOURCE resource;
	m_deviceContext->Map(cbo->m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
	memcpy(resource.pData, data, size);
	m_deviceContext->Unmap(cbo->m_buffer, 0);
}

void DX11Renderer::BindConstantBuffer(int slot, ConstantBuffer* cbo)
{
	m_deviceContext->VSSetConstantBuffers(slot, 1, &cbo->m_buffer);
	m_deviceContext->PSSetConstantBuffers(slot, 1, &cbo->m_buffer);
}

void DX11Renderer::DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount)
{
	BindVertexBuffer(vbo);
	SetBlendModeIfChanged();
	m_deviceContext->Draw(vertexCount, 0);
}

void DX11Renderer::SetBlendMode(BlendMode blendMode)
{
	m_desiredBlendMode = blendMode;
	SetBlendModeIfChanged();
}

void DX11Renderer::SetBlendModeIfChanged()
{
	ID3D11BlendState* desiredBlendState = m_blendStates[static_cast<int>(m_desiredBlendMode)];
	if (m_blendState != desiredBlendState)
	{
		m_blendState = desiredBlendState;
		float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		UINT sampleMask = 0xffffffff;
		m_deviceContext->OMSetBlendState(m_blendState, blendFactor, sampleMask);
	}
}

void DX11Renderer::SetRasterizerModeIfChanged()
{
	ID3D11RasterizerState* desiredRasterizerState = m_rasterizerStates[static_cast<int>(m_desiredRasterizerMode)];
	if (m_rasterizerState != desiredRasterizerState)
	{
		m_rasterizerState = m_rasterizerStates[(int)m_desiredRasterizerMode];
		//m_deviceContext->RSSetState(m_rasterizerState);
		m_deviceContext->RSSetState(m_rasterizerStates[(int)m_desiredRasterizerMode]);
	}
}

void DX11Renderer::SetRasterizerMode(RasterizerMode rasterizerMode)
{
	m_desiredRasterizerMode = rasterizerMode;
	SetRasterizerModeIfChanged();
	//D3D11_RASTERIZER_DESC rasterizerDesc = {};
	////rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	////rasterizerDesc.CullMode = D3D11_CULL_NONE;
	///*rasterizerDesc.FrontCounterClockwise = true;
	//rasterizerDesc.DepthClipEnable = true;
	//rasterizerDesc.AntialiasedLineEnable = true;*/
	//rasterizerDesc.FrontCounterClockwise = true;
	//rasterizerDesc.DepthBias = 0;
	//rasterizerDesc.DepthBiasClamp = 0.f;
	//rasterizerDesc.SlopeScaledDepthBias = 0.f;
	//rasterizerDesc.DepthClipEnable = true;
	//rasterizerDesc.ScissorEnable = false;
	//rasterizerDesc.MultisampleEnable = false;
	//rasterizerDesc.AntialiasedLineEnable = true;

	//if (rasterizerMode == RasterizerMode::SOLID_CULL_BACK)
	//{
	//	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	//	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	//}
	//if (rasterizerMode == RasterizerMode::SOLID_CULL_NONE)
	//{
	//	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	//	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	//}
	//if (rasterizerMode == RasterizerMode::WIREFRAME_CULL_NONE)
	//{
	//	rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	//	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	//}
	//if (rasterizerMode == RasterizerMode::WIREFRAME_CULL_BACK)
	//{
	//	rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	//	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	//}

	//HRESULT hr = m_device->CreateRasterizerState(&rasterizerDesc,
	//	&m_rasterizerStates[(int)m_desiredRasterizerMode]);
	//if (!SUCCEEDED(hr))
	//{
	//	ERROR_AND_DIE("CreateRasterizerState for RasterizerMode::SOLID_CULL_NONE failed.");
	//}
	////Call D3D11DeviceContext::RSSettState
	//m_deviceContext->RSSetState(m_rasterizerStates[(int)m_desiredRasterizerMode]);
}

void DX11Renderer::SetSamplerMode(SamplerMode samplerMode, int slot)
{
	UNUSED(slot);
	// if (slot ==0)
	// {
	m_desiredSamplerMode = samplerMode;
	SetSamplerModeIfChanged();
	// 	}
	// 	else
	// 	{
	// 		m_desiredSamplerMode = samplerMode;
	// 		SetSamplerModeIfChanged(slot);
	// 	}
}

void DX11Renderer::SetSamplerModeIfChanged()
{
	ID3D11SamplerState* desiredSamplerState = m_samplerStates[(int)m_desiredSamplerMode];
	if (m_samplerState != desiredSamplerState)
	{
		m_samplerState = m_samplerStates[(int)m_desiredSamplerMode];
		//m_deviceContext->PSSetSamplers(0, 1, &m_samplerState);
		m_deviceContext->PSSetSamplers(0, 1, &m_samplerStates[(int)m_desiredSamplerMode]);
	}
}

void DX11Renderer::SetSamplerModeIfChanged(int slot)
{
	ID3D11SamplerState* desiredSamplerState = m_samplerStates[(int)m_desiredSamplerMode];
	if (m_samplerState != desiredSamplerState)
	{
		m_samplerState = m_samplerStates[(int)m_desiredSamplerMode];
		//m_deviceContext->PSSetSamplers(0, 1, &m_samplerState);
		m_deviceContext->PSSetSamplers(slot, 2, &m_samplerStates[(int)m_desiredSamplerMode]);
	}
}

void DX11Renderer::SetDepthMode(DepthMode depthMode)
{
	m_desiredDepthMode = depthMode;
	SetDepthModeIfChanged();
}

void DX11Renderer::SetDepthModeIfChanged()
{
	ID3D11DepthStencilState* desiredDepthStencilState = m_depthStencilStates[(int)m_desiredDepthMode];
	if (m_depthStencilState != desiredDepthStencilState)
	{
		//m_deviceContext->OMSetDepthStencilState(m_depthStencilState, 0);
		m_depthStencilState = m_depthStencilStates[(int)m_desiredDepthMode];
		m_deviceContext->OMSetDepthStencilState(m_depthStencilStates[(int)m_desiredDepthMode], 0);
	}
}

void DX11Renderer::SetGeneralLightConstants(const Rgba8 sunColor, const Vec3& sunNormal, int numLights,
	std::vector<Rgba8> colors, std::vector<Vec3> worldPositions, std::vector<Vec3> spotForwards,
	std::vector<float> ambiences, std::vector<float> innerRadii, std::vector<float> outerRadii,
	std::vector<float> innerDotThresholds, std::vector<float> outerDotThresholds)
{
	if (numLights > s_maxLights)
	{
		ERROR_AND_DIE("Cannot handle this many lights!")
	}
	GeneralLightConstants lightConstant = {};
	float sunColorAsFloats[4];
	sunColor.GetAsFloats(sunColorAsFloats);
	lightConstant.SunColor[0] = sunColorAsFloats[0];
	lightConstant.SunColor[1] = sunColorAsFloats[1];
	lightConstant.SunColor[2] = sunColorAsFloats[2];
	lightConstant.SunColor[3] = sunColorAsFloats[3];
	lightConstant.SunNormal[0] = sunNormal.x;
	lightConstant.SunNormal[1] = sunNormal.y;
	lightConstant.SunNormal[2] = sunNormal.z;
	lightConstant.NumLights = numLights;

	for (int i = 0; i < numLights; i++)
	{
		GeneralLight light;

		float lightColorAsFloats[4];
		colors[i].GetAsFloats(lightColorAsFloats);
		light.Color[0] = lightColorAsFloats[0];
		light.Color[1] = lightColorAsFloats[1];
		light.Color[2] = lightColorAsFloats[2];
		light.Color[3] = lightColorAsFloats[3];
		light.WorldPosition[0] = worldPositions[i].x;
		light.WorldPosition[1] = worldPositions[i].y;
		light.WorldPosition[2] = worldPositions[i].z;

		light.SpotForward[0] = spotForwards[i].x;
		light.SpotForward[1] = spotForwards[i].y;
		light.SpotForward[2] = spotForwards[i].z;
		light.Ambience = ambiences[i];
		light.InnerRadius = innerRadii[i];
		light.OuterRadius = outerRadii[i];
		light.InnerDotThreshold = innerDotThresholds[i];
		light.OuterDotThreshold = outerDotThresholds[i];

		lightConstant.LightsArray[i] = light;
	}

	CopyCPUToGPU(&lightConstant, sizeof(GeneralLightConstants), m_generalLightCBO);
	BindConstantBuffer(k_generalLightConstantsSlot, m_generalLightCBO);
}

#ifdef ENGINE_PAST_VERSION_LIGHTS
void DX11Renderer::SetLightConstants(const Vec3& sunDirection, const float sunIntensity, const float ambientIntensity, const Rgba8& ambientColor)
{
	LightConstants lightConstants = {};
	lightConstants.SunDirection[0] = sunDirection.x;
	lightConstants.SunDirection[1] = sunDirection.y;
	lightConstants.SunDirection[2] = sunDirection.z;
	lightConstants.SunIntensity = sunIntensity;
	lightConstants.AmbientIntensity = ambientIntensity;
	/*lightConstants.AmbientLightColor[0] = ambientColor.r;
	lightConstants.AmbientLightColor[1] = ambientColor.g;
	lightConstants.AmbientLightColor[2] = ambientColor.b;*/
	float ambientColorAsFloats[4];
	ambientColor.GetAsFloats(ambientColorAsFloats);
	lightConstants.AmbientLightColor[0] = ambientColorAsFloats[0];
	lightConstants.AmbientLightColor[1] = ambientColorAsFloats[1];
	lightConstants.AmbientLightColor[2] = ambientColorAsFloats[2];
	//memcpy(lightConstants.AmbientIntensity, ambientIntensity, sizeof(float) * 4);

	CopyCPUToGPU(&lightConstants, sizeof(LightConstants), m_lightCBO);
	BindConstantBuffer(k_lightConstantsSlot, m_lightCBO);
}

void DX11Renderer::SetPointLightConstants(const std::vector<Vec3>& pos, std::vector<float> intensity, const std::vector<Rgba8>& color, std::vector<float> range)
{
	std::vector<PointLightConstants> pointLightConstantsArray;
	for (int i = 0; i < 10; i++)
	{
		PointLightConstants pointLightConstants = {};
		pointLightConstants.PointLightPosition[0] = pos[i].x;
		pointLightConstants.PointLightPosition[1] = pos[i].y;
		pointLightConstants.PointLightPosition[2] = pos[i].z;
		float pointColorAsFloats[4];
		color[i].GetAsFloats(pointColorAsFloats);
		pointLightConstants.LightColor[0] = pointColorAsFloats[0];
		pointLightConstants.LightColor[1] = pointColorAsFloats[1];
		pointLightConstants.LightColor[2] = pointColorAsFloats[2];
		pointLightConstants.LightIntensity = intensity[i];
		pointLightConstants.LightRange = range[i];
		pointLightConstantsArray.push_back(pointLightConstants);

	}
	CopyCPUToGPU(pointLightConstantsArray.data(), sizeof(PointLightConstants) * 10, m_pointLightCBO);
	BindConstantBuffer(k_pointLightConstantsSlot, m_pointLightCBO);
}

void DX11Renderer::SetSpotLightConstants(const std::vector<Vec3>& pos, std::vector<float> cutOff, const std::vector<Vec3>& dir, const std::vector<Rgba8>& color)
{
	std::vector<SpotLightConstants> spotLightConstantsArray = {};
	for (int i = 0; i < 4; i++)
	{
		SpotLightConstants spotLightConstants = {};
		spotLightConstants.SpotLightPosition = pos[i];
		spotLightConstants.SpotLightCutOff = cutOff[i];
		spotLightConstants.SpotLightDirection = dir[i].GetNormalized();
		float spotColorAsFloats[4];
		color[i].GetAsFloats(spotColorAsFloats);
		spotLightConstants.SpotLightColor[0] = spotColorAsFloats[0];
		spotLightConstants.SpotLightColor[1] = spotColorAsFloats[1];
		spotLightConstants.SpotLightColor[2] = spotColorAsFloats[2];
		spotLightConstantsArray.push_back(spotLightConstants);
	}
	CopyCPUToGPU(spotLightConstantsArray.data(), sizeof(SpotLightConstants) * 4, m_spotLightCBO);
	BindConstantBuffer(k_spotLightConstantsSlot, m_spotLightCBO);
}
#endif

void DX11Renderer::SetModelConstants(const Mat44& modelToWorldTransform, const Rgba8& modelColor)
{
	ModelConstants modelConstants = {};
	modelConstants.ModelToWorldTransform = modelToWorldTransform;
	modelColor.GetAsFloats(modelConstants.ModelColor);

	CopyCPUToGPU(&modelConstants, sizeof(ModelConstants), m_modelCBO);
	BindConstantBuffer(k_modelConstantsSlot, m_modelCBO);
}

void DX11Renderer::SetShadowConstants(const Mat44& lightViewProjectionMatrix) //TODO:改了
{
	ShadowConstants shadowConstants = {};
	shadowConstants.LightWorldToCamera = lightViewProjectionMatrix;

	CopyCPUToGPU(&shadowConstants, sizeof(ShadowConstants), m_shadowCBO);
	BindConstantBuffer(k_shadowConstantsSlot, m_shadowCBO);
}

void DX11Renderer::SetPerFrameConstants(const float time, const int debugInt, const float debugFloat)
{
	PerFrameConstants perFrameConstants = {};
	perFrameConstants.Time = time;
	perFrameConstants.DebugInt = debugInt;
	perFrameConstants.DebugFloat = debugFloat;

	CopyCPUToGPU(&perFrameConstants, sizeof(perFrameConstants), m_perFrameCBO);
	BindConstantBuffer(k_perFrameConstantsSlot, m_perFrameCBO);
}

//------------------------------------------------------
//Functions called in Startup()
void DX11Renderer::CreateDeviceAndSwapChain(unsigned int deviceFlags)
{
	//Create device and swap chain
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferDesc.Width = g_theWindow->GetClientDimensions().x;
	swapChainDesc.BufferDesc.Height = g_theWindow->GetClientDimensions().y;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.OutputWindow = (HWND)g_theWindow->GetHwnd();
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	//Call D3D11CreateDeviceAndSwapChain
	HRESULT hr;
	hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, deviceFlags,
		nullptr, 0, D3D11_SDK_VERSION, &swapChainDesc,
		&m_swapChain, &m_device, nullptr, &m_deviceContext);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create D3D 11 device and swap chain.");
	}
}

void DX11Renderer::GetBackBufferTexture()
{
	//Get back buffer texture
	HRESULT hr;
	ID3D11Texture2D* backBuffer;
	hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not get swap chain buffer.");
	}
	hr = m_device->CreateRenderTargetView(backBuffer, NULL, &m_renderTargetView);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could create render target view for swap chain buffer.");
	}

	backBuffer->Release();
}

void DX11Renderer::SetDefaultTexture()
{
	//Set default texture
	Image* image = new Image(IntVec2(2, 2), Rgba8::WHITE);
	image->m_imageFilePath = "Default";
	m_defaultTexture = CreateTextureFromImage(*image);

	Image* image1 = new Image(IntVec2(2, 2), Rgba8(127, 127, 255));
	image1->m_imageFilePath = "DefaultNormal";
	m_defaultNormalTexture = CreateTextureFromImage(*image1); 

	Image* image2 = new Image(IntVec2(2, 2), Rgba8(127, 127, 0));
	image1->m_imageFilePath = "DefaultSpec";
	m_defaultSpecTexture = CreateTextureFromImage(*image2);
}

void DX11Renderer::CreateAndBindDefaultShader()
{
	//Create and bind a shader
	//m_defaultShader = CreateShader("Default");
	m_defaultShader = CreateShader("Default", m_shaderSource);
	BindShader(m_defaultShader);
}

void DX11Renderer::CreateSampleStates()
{
	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	HRESULT hr;
	hr = m_device->CreateSamplerState(&samplerDesc, &m_samplerStates[(int)SamplerMode::POINT_CLAMP]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateSamplerState for SamplerMode::POINT_CLAMP failed.");
	}

	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

	hr = m_device->CreateSamplerState(&samplerDesc, &m_samplerStates[(int)SamplerMode::BILINEAR_WRAP]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateSamplerState for SamplerMode::BILINEAR_WRAP failed.");
	}

	//m_deviceContext->PSSetSamplers(0, 1, &m_samplerState);
}

void DX11Renderer::CreateBlendStates()
{
	//Create a blend state for opaque rendering
	HRESULT hr;
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlend;
	blendDesc.RenderTarget[0].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlend;
	blendDesc.RenderTarget[0].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOp;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = m_device->CreateBlendState(&blendDesc, &m_blendStates[(int)(BlendMode::OPAQUE)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateBlendState for BlendMode::OPAQUE failed.");
	}
	//Create a alpha state
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	hr = m_device->CreateBlendState(&blendDesc, &m_blendStates[(int)(BlendMode::ALPHA)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateBlendState for BlendMode::ALPHA failed.");
	}
	//Create a additive state
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	hr = m_device->CreateBlendState(&blendDesc, &m_blendStates[(int)(BlendMode::ADDITIVE)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateBlendState for BlendMode::ADDITIVE failed.");
	}
}

void DX11Renderer::CreateRasterizerStates()
{
	//Create rasterizer states into the array
	HRESULT hr;
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.FrontCounterClockwise = true;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.DepthBiasClamp = 0.f;
	rasterizerDesc.SlopeScaledDepthBias = 0.f;
	rasterizerDesc.DepthClipEnable = true;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = true;
	//Call ID3D11Device::CreateRasterizerState
	hr = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerStates[(int)RasterizerMode::SOLID_CULL_BACK]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create rasterizer state:SOLID_CULL_BACK.");
	}

	rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	hr = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerStates[(int)RasterizerMode::WIREFRAME_CULL_NONE]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create rasterizer state:WIREFRAME_CULL_NONE.");
	}

	rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	hr = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerStates[(int)RasterizerMode::WIREFRAME_CULL_BACK]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create rasterizer state:WIREFRAME_CULL_BACK.");
	}

	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	hr = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerStates[(int)RasterizerMode::SOLID_CULL_NONE]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create rasterizer state:SOLID_CULL_NONE.");
	}
	//Call D3D11DeviceContext::RSSettState
	//m_deviceContext->RSSetState(m_rasterizerState);
}

void DX11Renderer::CreateDepthStencilStates()
{
	HRESULT hr;
	//Create depth stencil states
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
	hr = m_device->CreateDepthStencilState(&depthStencilDesc,
		&m_depthStencilStates[(int)(DepthMode::DISABLED)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateDepthStencilState for DepthMode::DISABLED failed.");
	}

	depthStencilDesc.DepthEnable = TRUE;

	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc,
		&m_depthStencilStates[(int)(DepthMode::READ_ONLY_ALWAYS)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateDepthStencilState for DepthMode::READ_ONLY_ALWAYS failed.");
	}

	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc,
		&m_depthStencilStates[(int)(DepthMode::READ_ONLY_LESS_EQUAL)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateDepthStencilState for DepthMode::READ_ONLY_LESS_EQUAL failed.");
	}

	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	hr = m_device->CreateDepthStencilState(&depthStencilDesc,
		&m_depthStencilStates[(int)(DepthMode::READ_WRITE_LESS_EQUAL)]);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("CreateDepthStencilState for DepthMode::READ_WRITE_LESS_EQUAL failed.");
	}
}

void DX11Renderer::CreateDepthStencilTextureAndView()
{
	HRESULT hr;
	//Create depth stencil texture and view
	D3D11_TEXTURE2D_DESC depthTextureDesc = {};
	depthTextureDesc.Width = m_config.m_window->GetClientDimensions().x;
	depthTextureDesc.Height = m_config.m_window->GetClientDimensions().y;
	depthTextureDesc.MipLevels = 1;
	depthTextureDesc.ArraySize = 1;
	depthTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	depthTextureDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthTextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthTextureDesc.SampleDesc.Count = 1;

	hr = m_device->CreateTexture2D(&depthTextureDesc, nullptr, &m_depthStencilTexture);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create texture for depth stencil.");
	}

	hr = m_device->CreateDepthStencilView(m_depthStencilTexture, nullptr, &m_depthStencilDSV);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Could not create depth stencil view.");
	}
}

void DX11Renderer::CreateShadowMapResources()
{
	// Create the Depth Texture for Shadow Map
	D3D11_TEXTURE2D_DESC shadowDepthDesc = {};
	shadowDepthDesc.Width = 1024; // Shadow map分辨率，可以自由调 TODO: change from config
	shadowDepthDesc.Height = 1024;
	shadowDepthDesc.MipLevels = 1;
	shadowDepthDesc.ArraySize = 1;
	shadowDepthDesc.Format = DXGI_FORMAT_R32_TYPELESS; // 只需要Depth，不需要Stencil
	shadowDepthDesc.SampleDesc.Count = 1;
	shadowDepthDesc.Usage = D3D11_USAGE_DEFAULT;
	shadowDepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	//ID3D11Texture2D* depthTexture = nullptr;
	HRESULT hr = m_device->CreateTexture2D(&shadowDepthDesc, nullptr, &m_shadowDepthTexture);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Failed to create Shadow Map Depth Texture");
	}

	// Create DepthStencilView
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT; // ← DSV视图需要显式指定
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = m_device->CreateDepthStencilView(m_shadowDepthTexture, &dsvDesc, &m_shadowDSV);
	if (!SUCCEEDED(hr))
	{
		ERROR_AND_DIE("Failed to create Shadow Map Depth Stencil View");
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;   // 注意：匹配 DepthTexture 的格式
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	hr = m_device->CreateShaderResourceView(m_shadowDepthTexture, &srvDesc, &m_shadowSRV);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create SRV for shadow map");
}

void DX11Renderer::BeginShadowPass()
{
	// 绑定 Shadow Map 的 DSV，不绑定ColorRTV
	m_deviceContext->OMSetRenderTargets(0, nullptr, m_shadowDSV);

	// 设置专门的Viewport
	D3D11_VIEWPORT shadowViewport = {};
	shadowViewport.TopLeftX = 0;
	shadowViewport.TopLeftY = 0;
	shadowViewport.Width = 1024.0f;
	shadowViewport.Height = 1024.0f;
	shadowViewport.MinDepth = 0.0f;
	shadowViewport.MaxDepth = 1.0f;
	m_deviceContext->RSSetViewports(1, &shadowViewport);

	// Clear depth
	m_deviceContext->ClearDepthStencilView(m_shadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void DX11Renderer::EndShadowPass()
{
	// 重新绑定回屏幕
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilDSV);

	//// 恢复Viewport
	//D3D11_VIEWPORT screenViewport = {};
	//screenViewport.TopLeftX = 0;
	//screenViewport.TopLeftY = 0;
	//screenViewport.Width = (float)g_theWindow->GetClientDimensions().x;
	//screenViewport.Height = (float)g_theWindow->GetClientDimensions().y;
	//screenViewport.MinDepth = 0.0f;
	//screenViewport.MaxDepth = 1.0f;
	//m_deviceContext->RSSetViewports(1, &screenViewport);
}

void DX11Renderer::CreateShadowMapShader() //Only create
{
	m_shadowPassShader = CreateOrGetShader("Data/Shaders/Shadow", VertexType::VERTEX_PCUTBN);
}

void DX11Renderer::BindShadowMapTextureAndSampler()
{
	m_deviceContext->PSSetShaderResources(1, 1, &m_shadowSRV);     // bind to t1
	m_deviceContext->PSSetSamplers(1, 1, &m_samplerStates[(int)m_desiredSamplerMode]);
}

#endif