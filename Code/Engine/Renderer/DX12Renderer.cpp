#include "Engine/Renderer/DX12Renderer.hpp"


#ifdef ENGINE_DX12_RENDERER
#include "Renderer.hpp"
#include "ConstantBuffer.hpp"
#include "GIVisualization.h"
#include "IndexBuffer.hpp"
#include "Shader.hpp"
#include "VertexBuffer.hpp"
#include "SDFTexture3D.h"
#include "ShaderIncludeHandler.h"
#include "Cache/RadianceCacheManager.h"
#include "Cache/SurfaceCard.h"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Renderer/Texture.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Renderer/GI/DefaultGBufferShader.h"
#include "Engine/Core/DebugRenderSystem.hpp"
#include "Engine/Core/Image.hpp"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Object/Mesh/MeshObject.h"
#include "Cache/CardBVH.h"
#include "Cache/ScreenProbeFinalGather.h"
#include "Cache/CombineSurfaceCache.h"
#include "Cache/SurfaceRadiosityCache.h"
#include "GI/GISystem.h"

#include "ThirdParty/ImGui/imgui.h"
#include "ThirdParty/ImGui/implot.h"
#include "ThirdParty/ImGui/imgui_impl_dx12.h"
#include "ThirdParty/ImGui/imgui_impl_win32.h"
#include "ThirdParty/stb/stb_image.h"

// #include <wrl/client.h>
// using Microsoft::WRL::ComPtr;

extern GISystem* g_theGISystem;

#ifdef ENGINE_DEBUG_RENDER
#define DEBUG_TDR(msg) OutputDebugStringA("[TDR] " msg "\n")
#define CHECK_DEVICE() do { \
HRESULT hr = m_device->GetDeviceRemovedReason(); \
if (FAILED(hr)) { \
OutputDebugStringA("[TDR FATAL] Device Removed!\n"); \
DebugBreak(); \
} \
} while(0)
#else
#define DEBUG_TDR(msg)
#define CHECK_DEVICE()
#endif

DX12Renderer::DX12Renderer(RendererConfig config)
{
	m_config = config;

	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		m_renderTargets[i] = nullptr;
	}
}

DX12Renderer::~DX12Renderer()
{

}

void DX12Renderer::Startup()
{
#ifdef ENGINE_DEBUG_RENDER
	Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
	D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
	debugController->EnableDebugLayer();
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
	if (SUCCEEDED(debugController.As(&debugController1))) {
		debugController1->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	IDXGIFactory4* factory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create D3D12 DXGI factory");

	IDXGIAdapter1* hardwareAdapter;
	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		hardwareAdapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_device))))
		{
			break;
		}
	}

	//hr = D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_device));
	//GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create D3D12 device");
	hardwareAdapter->Release();

	// get the descriptor size
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_scuDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create D3D12 command queue!");

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_BUFFER_COUNT;
	swapChainDesc.BufferDesc.Width = m_config.m_window->GetClientDimensions().x;
	swapChainDesc.BufferDesc.Height = m_config.m_window->GetClientDimensions().y;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.OutputWindow = (HWND)m_config.m_window->GetHwnd();
	swapChainDesc.SampleDesc.Count = 1; // multisample count (no multisampling, so we just put 1, since we still need 1 sample)
	swapChainDesc.Windowed = TRUE;  //TODO: make full screen usable

	hr = factory->CreateSwapChain(
		m_commandQueue,
		&swapChainDesc,
		(IDXGISwapChain**)&m_swapChain
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create D3D12 swap chain!");
	factory->Release();

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		if (m_config.m_enableGI)
		{
			rtvHeapDesc.NumDescriptors = FRAME_BUFFER_COUNT + GBUFFER_COUNT + CARD_CAPTURE_RTV_COUNT;
		} //3层cardCapture
		else
		{
			rtvHeapDesc.NumDescriptors = FRAME_BUFFER_COUNT;
		}
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescHeap));

		GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create D3D12 descriptor heaps!");
	}

	// Create frame resources.
	{
		//二编：除非有特殊需求（比如调试低层句柄地址），建议统一使用 Offset() 方法，符合现代 C++ 封装风格
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
		{
			//Get the i buffer in the swap chain and store it in the i position of ID3D12Resource array
			//GetBuffer() really allocate memory in GPU, create the rtv
			hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
			GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot get D3D12 render target!");
			std::wstring rtName = L"BackBuffer_" + std::to_wstring(i);
			m_renderTargets[i]->SetName(rtName.c_str());
			
			//make a rtv 'Descriptor'
			//"create" a render target view which binds the swap chain buffer (ID3D12Resource[i]) to the rtv handle
			m_device->CreateRenderTargetView(m_renderTargets[i], nullptr, rtvHandle);

			//increment the rtv handle by offsetting the rtv descriptor size
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}
	//Create CA with 1st
	// -- Create the Command Allocators -- //
	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator[i]));
		if (FAILED(hr))
		{
			GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create all CA!");
		}
		std::wstring name = L"CA" + std::to_wstring(i);
		m_commandAllocator[i]->SetName(name.c_str());
	}

	CreateGraphicsRootSignature();

	m_constantBuffers.fill(nullptr);

	// Constant buffers: create descriptor heap and resource heap
//	+---------------------+
//  | Create CBV Heap     | -- D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV (Shader Visible)
//  +---------------------+
//           |
//           V
//  +---------------------+           +----------------------+
//  | Create Upload Heap  | --> -->   | Create ID3D12Resource|
//  +---------------------+           +----------------------+
//           |
//           V
//  +---------------------+
//  | Create CBV and write|
//  | to descriptor heap  |
//  +---------------------+
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	//heapDesc.NumDescriptors = NUM_CONSTANT_BUFFERS + MAX_TEXTURE_COUNT + GBUFFER_COUNT + SURFACE_CACHE_DESCRIPTOR_COUNT;  
	heapDesc.NumDescriptors = TOTAL_NUM_DESCRIPTORS;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cbvSrvDescHeap));
	m_cbvSrvDescHeap->SetName(L"Default CBVSRV heap");
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create constant buffer description heap!");

	// create a resource heap, descriptor heap, and pointer to cbv for each frame
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart());
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	//D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(1024 * 64);
	for (int cbSlot = 0; cbSlot < NUM_CONSTANT_BUFFERS; ++cbSlot)
	{
		size_t alignedSize = AlignUp(GetConstantBufferSize(cbSlot), 256);
		size_t totalBufferSize = alignedSize * 256;
    
		m_constantBuffers[cbSlot] = new ConstantBuffer(totalBufferSize, alignedSize);
		// 创建一个大buffer
		D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBufferSize);
		hr = m_device->CreateCommittedResource(
			&properties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffers[cbSlot]->m_dx12ConstantBuffer)
		);
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create constant buffer!");

		CD3DX12_RANGE range(0, 0);
		hr = m_constantBuffers[cbSlot]->m_dx12ConstantBuffer->Map(0, &range,
			reinterpret_cast<void**>(&m_constantBuffers[cbSlot]->m_mappedPtr));

		std::wstring cbName = L"ConstantBuffer_" + std::to_wstring(cbSlot);
		m_constantBuffers[cbSlot]->m_dx12ConstantBuffer->SetName(cbName.c_str());
	}

	// create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1 + SHADOW_MAP_DSV_COUNT;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvDescHeap));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot Create Depth Stencil Descriptor heap!");

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	CD3DX12_HEAP_PROPERTIES defaultProperties(D3D12_HEAP_TYPE_DEFAULT);
	//CD3DX12_RESOURCE_DESC depthStencilTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_config.m_window->GetClientDimensions().x, m_config.m_window->GetClientDimensions().y, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	CD3DX12_RESOURCE_DESC depthStencilTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS,
		m_config.m_window->GetClientDimensions().x, m_config.m_window->GetClientDimensions().y, 1, 1,
		1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	m_device->CreateCommittedResource(
		&defaultProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilTextureDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		IID_PPV_ARGS(&m_depthStencilBuffer)
	);
	m_depthStencilBuffer->SetName(L"DepthStencilBuffer");
	m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilDesc, m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());

	// Create the command list.
	//We need to create a direct command list so that we can execute our clear render target command. We do this by specifying D3D12_COMMAND_LIST_TYPE_DIRECT for the second parameter.
	//Since we only need one command list, which is reset each frame where we specify a command allocator, we just create this command list with the first command allocator.
	//When a command list is created, it is created in the "recording" state. We do not want to record to the command list yet, so we Close() the command list after we create it.
	hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[0], m_pipelineStateObject, IID_PPV_ARGS(&m_commandList));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Create CommandList!");
	m_commandList->SetName(L"Main Command List");	
	//hr = m_commandList->Close(); //第一帧end时close
	//GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 cannot close the new created CommandList!");
	//ID3D12CommandList* ppCommandLists[] = { m_commandList };
	//m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

	// Sets up root signature and descriptor heaps for the command list, prepare for binding resources
	m_commandList->SetGraphicsRootSignature(m_graphicsRootSignature);
	ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvDescHeap };
	m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	m_loadedTextures.clear();
	Image* whiteImage = new Image(IntVec2(4, 4), Rgba8::WHITE);
	m_defaultTexture = CreateTextureFromImage(*whiteImage);
	m_defaultTexture->m_name = "Default";
	m_loadedTextures.push_back(m_defaultTexture);
	
	Image* image1 = new Image(IntVec2(2, 2), Rgba8(127, 127, 255));
	m_defaultNormalTexture = CreateTextureFromImage(*image1);
	m_defaultNormalTexture->m_name = "DefaultNormal";
	m_loadedTextures.push_back(m_defaultNormalTexture);

	Image* image2 = new Image(IntVec2(2, 2), Rgba8(127, 127, 0));
	m_defaultSpecTexture = CreateTextureFromImage(*image2);
	m_defaultSpecTexture->m_name = "DefaultSpec";
	m_loadedTextures.push_back(m_defaultSpecTexture);

	// Create the pipeline state, which includes compiling and loading shaders.
	m_defaultShader = CreateShader("Default", m_shaderSource);
	m_desiredShader = m_defaultShader;

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		// create the fences
		for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
		{
			hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence[i]));
			GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Create Fence!");
			m_fenceValue[i] = 0; // set the initial fence value to 0

			m_fence[i]->SetName((L"Fence_" + std::to_wstring(i)).c_str());
		}
		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ERROR_AND_DIE("D3D12 Cannot Create Fence Event!");
		}
	}
	//Create Ring buffers
	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		m_frameVertexBuffers[i] = new VertexBuffer(m_device, VERTEX_RING_BUFFER_SIZE, sizeof(Vertex_PCU));
		m_frameVertexBuffersForTBN[i] = new VertexBuffer(m_device, VERTEX_RING_BUFFER_SIZE, sizeof(Vertex_PCUTBN));
		m_frameIndexBuffers[i] = new IndexBuffer(m_device, VERTEX_RING_BUFFER_SIZE, sizeof(Vertex_PCU));
	}
	// Wait for the command list to execute; we are reusing the same command 
	// list in our main loop but for now, we just want to wait for setup to 
	// complete before continuing.
	// for (int i = 0; i < FRAME_BUFFER_COUNT; ++i)
	// {
	// 	m_frameIndex = i;
	// }
	WaitForPreviousFrame();

	if (m_config.m_enableGI) //TODO: 这里的renderMode应该是一个组合管线，由多种搭配组合而成
	{
		StartupComputeQueue();

		CreateShadowMapResources();
		
		CreateCardCapturePassResources();
		CreateCardCapturePipelineStates();

		CreateGBufferPassResources();
		//m_gBuffer.m_depth = m_depthStencilBuffer;
		CreateDepthSRV();
		
		CreateComputeRootSignature();
		
		CreateRadianceCacheResources();
		CreateCompositeResources();

		CreateSDFGenerationRootSignature();
		CreateSDFGenerationPSO();
		
		CreateGlobalSDFPassResources();

		CreateRadianceCacheUpdatePSO();
		CreateRadianceCacheResources();

		CreateCombineSurfaceCacheResources();
		CreateSurfaceCacheRadiosityResources();
		CreateScreenProbeGatherResources();
		CreateGIVisualizationResources();
	}

	ImGuiStartUp();
}

void DX12Renderer::BeginFrame()
{
	//WaitForPreviousFrame();
	UINT frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	m_frameIndex = (int)frameIndex;
	m_frameVertexBuffers[m_frameIndex]->ResetRing();
	m_frameVertexBuffersForTBN[m_frameIndex]->ResetRing();
	m_frameIndexBuffers[m_frameIndex]->ResetRing();

	m_currentDrawIndex = 0;
	
	for (int i = 0; i < NUM_CONSTANT_BUFFERS; ++i)
	{
		m_constantBuffers[i]->ResetOffset();
	}

	m_radianceCache.BeginFrame();
	g_theGISystem->BeginFrame(m_frameIndex);

	m_commandList->SetGraphicsRootSignature(m_graphicsRootSignature);
	
	ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvSrvDescHeap };
	m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// 显式绑定srv槽 如果改成bindless的话就只需要这里绑定一次了 //二编：已改！
	CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
	                                               NUM_CONSTANT_BUFFERS, m_scuDescriptorSize);
	m_commandList->SetGraphicsRootDescriptorTable(14, descriptorHandle);

	ImGuiBeginFrame();

	m_currentRenderMode = RenderMode::UNKNOWN;
	m_currentActivePass = ActivePass::UNKNOWN;
}

void DX12Renderer::EndFrame()
{
	if (m_gBufferPassActive)
		EndGBufferPass();

	ImGuiEndFrame();

	if (m_currentActivePass == ActivePass::BACKBUFFER)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex],
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &barrier);
	}

	//m_currentRenderMode = RenderMode::UNKNOWN;
	//m_currentActivePass = ActivePass::UNKNOWN;
	m_forwardPassActive = false;

	HRESULT hr = m_commandList->Close();
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Failed to Close CommandList");

	ID3D12CommandList* listsToExecute[] = { m_commandList };
	m_commandQueue->ExecuteCommandLists(1, listsToExecute);

	const UINT currentFrameIndex = m_frameIndex;
	const UINT64 currentFenceValue = m_fenceValue[currentFrameIndex];

	hr = m_commandQueue->Signal(m_fence[currentFrameIndex], currentFenceValue);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Failed to signal fence");
	hr = m_swapChain->Present(1, 0);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Present failed");

	m_radianceCache.EndFrame(); //TODO: meishayong
	
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (m_fence[m_frameIndex]->GetCompletedValue() < m_fenceValue[m_frameIndex])
	{
		hr = m_fence[m_frameIndex]->SetEventOnCompletion(m_fenceValue[m_frameIndex], m_fenceEvent);
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Failed to SetEventOnCompletion");
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	hr = m_commandAllocator[m_frameIndex]->Reset();
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Failed to reset CommandAllocator");

	hr = m_commandList->Reset(m_commandAllocator[m_frameIndex], m_pipelineStateObject);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12: Failed to reset CommandList");

	m_fenceValue[m_frameIndex]++;

	if (m_fence[currentFrameIndex]->GetCompletedValue() < currentFenceValue)
	{
		hr = m_fence[currentFrameIndex]->SetEventOnCompletion(currentFenceValue, m_fenceEvent);
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to set completion event");
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	for (auto& res : m_previousFrameTempResources)
	{
		DX_SAFE_RELEASE(res);
	}
	m_previousFrameTempResources.clear();

	m_previousFrameTempResources = m_currentFrameTempResources;
	m_currentFrameTempResources.clear();

	m_hasBackBufferCleared = false;

	if (m_giSystem)
		m_giSystem->EndFrame();
}

void DX12Renderer::ShutDown()
{
	if (m_giSystem)
		m_giSystem->Shutdown();
	if (m_computeQueue)
		WaitForComputeQueue();
	for (int i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		m_frameIndex = i;
		WaitForPreviousFrame();
	}
	ImGuiShutDown();

	m_surfaceRadiosity->Shutdown();
	m_screenProbeFinalGather->Shutdown();
	m_combineSurfaceCache->Shutdown();
	m_giVisualization->Shutdown();
	delete m_surfaceRadiosity;
	m_surfaceRadiosity = nullptr;
	delete m_screenProbeFinalGather;
	m_screenProbeFinalGather = nullptr;
	delete m_combineSurfaceCache;
	m_combineSurfaceCache = nullptr;
	delete m_giVisualization;
	m_giVisualization = nullptr;
	
	ShutdownComputeQueue();
	ShutdownShadowMapPass();
	
	m_surfaceCache.Shutdown();
	
	for (BitmapFont* font : m_loadedFonts)
	{
		delete font;
		font = nullptr;
	}
	m_loadedFonts.clear();
	for (Texture* tex : m_loadedTextures)
	{
		delete tex;
		tex = nullptr;
	}
	m_loadedTextures.clear();
	for (Shader* shader : m_loadedShaders)
	{
		delete shader;
		shader = nullptr;
	}
	m_loadedShaders.clear();

	for (int j = 0; j < NUM_CONSTANT_BUFFERS; j++)
	{
		delete m_constantBuffers[j];
		m_constantBuffers[j] = nullptr;
	}

	for (int n = 0; n < FRAME_BUFFER_COUNT; n++)
	{
		delete m_frameVertexBuffers[n];
		m_frameVertexBuffers[n] = nullptr;
		delete m_frameIndexBuffers[n];
		m_frameIndexBuffers[n] = nullptr;
		delete m_frameVertexBuffersForTBN[n];
		m_frameVertexBuffersForTBN[n] = nullptr;
	}
	for (SDFTexture3D* sdf : m_loadedSDFs)
	{
		delete sdf;
	}
	m_loadedSDFs.clear();

	DX_SAFE_RELEASE(m_gBuffer.m_albedo);
	DX_SAFE_RELEASE(m_gBuffer.m_normal);
	DX_SAFE_RELEASE(m_gBuffer.m_worldPos);
	DX_SAFE_RELEASE(m_gBuffer.m_material);
	
	m_radianceCache.Shutdown();
    
	DX_SAFE_RELEASE(m_cardBVHNodeBuffer);
	DX_SAFE_RELEASE(m_cardBVHIndexBuffer);
	DX_SAFE_RELEASE(m_radianceCacheUpdatePSO);

	for (auto& pair : m_pipelineStatesConfiguration)
	{
		DX_SAFE_RELEASE(pair.second);
	}
	m_pipelineStatesConfiguration.clear();
	for (auto& gBufferPair : m_gBufferPipelineStatesConfiguration)
	{
		DX_SAFE_RELEASE(gBufferPair.second);
	}
	m_gBufferPipelineStatesConfiguration.clear();
	for (auto& cardCapturePair : m_cardCapturePSOConfiguration)
	{
		DX_SAFE_RELEASE(cardCapturePair.second);
	}
	m_cardCapturePSOConfiguration.clear();
	DX_SAFE_RELEASE(m_sdfGenerationPSO);

	ShutdownCompositePass();
	DX_SAFE_RELEASE(m_graphicsRootSignature); //its bound in pso
	DX_SAFE_RELEASE(m_computeRootSignature);
	DX_SAFE_RELEASE(m_sdfGenerationRootSignature);

	DX_SAFE_RELEASE(m_globalSDFTexture);
	DX_SAFE_RELEASE(m_voxelVisibilityBuffer);
	DX_SAFE_RELEASE(m_voxelLightingTexture);
	DX_SAFE_RELEASE(m_instanceInfoBuffer);
	DX_SAFE_RELEASE(m_instanceInfoUploadBuffer);
	DX_SAFE_RELEASE(m_buildGlobalSDFPSO);
	DX_SAFE_RELEASE(m_buildVisibilityPSO);
	DX_SAFE_RELEASE(m_injectLightingPSO);
	DX_SAFE_RELEASE(m_globalSDFRootSignature);

	DX_SAFE_RELEASE(m_swapChain);
	
	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		DX_SAFE_RELEASE(m_renderTargets[i]);
	}
	DX_SAFE_RELEASE(m_depthStencilBuffer);

	DX_SAFE_RELEASE(m_imguiSrvDescHeap);
	DX_SAFE_RELEASE(m_rtvDescHeap);
	DX_SAFE_RELEASE(m_dsvDescHeap);
	DX_SAFE_RELEASE(m_cbvSrvDescHeap);

	DX_SAFE_RELEASE(m_commandList);
	for (ID3D12CommandAllocator* ca : m_commandAllocator)
	{
		DX_SAFE_RELEASE(ca);
	}
	DX_SAFE_RELEASE(m_commandQueue);

	CloseHandle(m_fenceEvent);
	for (ID3D12Fence* fence : m_fence)
	{
		DX_SAFE_RELEASE(fence);
		fence = nullptr;
	}
	
	//ComPtr<ID3D12DebugDevice> debugDevice;
	//if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&debugDevice))))
	//{
	//	debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
	//}
	DX_SAFE_RELEASE(m_device);
}

void DX12Renderer::ClearScreen(Rgba8 const& clearColor)
{
	// // Indicate that the back buffer will be used as a render target.
	// CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	// m_commandList->ResourceBarrier(1, &barrier);
	//
	// CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	// CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());
	// m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	//
	// float colorAsFloats[4];
	// clearColor.GetAsFloats(colorAsFloats);
	// m_commandList->ClearRenderTargetView(rtvHandle, colorAsFloats, 0, nullptr);
	// m_commandList->ClearDepthStencilView(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	//if (m_currentRenderMode == RenderMode::FORWARD)
	//{
	//BeginForwardPass();
	//ClearForwardPassRTV(clearColor);
	UNUSED(clearColor); //TODO
	m_commandList->ClearDepthStencilView(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	//}
	//if (m_currentRenderMode == RenderMode::GI)
	//{
	//	BeginGBufferPass(clearColor);
	//}
}

void DX12Renderer::ClearScreen()
{
	ClearScreen(Rgba8::MAGENTA);
}

void DX12Renderer::BeginCamera(Camera const& camera)
{
	m_camera = camera;
	m_previousCam = m_currentCam;
	//CameraConstants cam;
	m_currentCam.WorldToCameraTransform = camera.GetWorldToCameraTransform();
	m_currentCam.CameraToRenderTransform = camera.GetCameraToRenderTransform();
	m_currentCam.RenderToClipTransform = camera.GetRenderToClipTransform();
	m_currentCam.CameraWorldPosition = camera.GetPosition();

	//D3D12_VIEWPORT viewport = {};
	m_viewport.TopLeftX = 0.f;
	m_viewport.TopLeftY = 0.f;
	m_viewport.Width = (float)m_config.m_window->GetClientDimensions().x;
	m_viewport.Height = (float)m_config.m_window->GetClientDimensions().y;
	m_viewport.MinDepth = 0.f;
	m_viewport.MaxDepth = 1.f;

	//m_viewport = &viewport;

	D3D12_RECT scissorRect;
	scissorRect.left = 0;
	scissorRect.right = m_config.m_window->GetClientDimensions().x;
	scissorRect.bottom = m_config.m_window->GetClientDimensions().y;
	scissorRect.top = 0;


	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &scissorRect);

	m_scissorRect = scissorRect;

	// UINT8* cbGPUAddress;
	// CD3DX12_RANGE readRange(0, 0);
	// m_frameConstantBuffers[m_frameIndex][k_cameraConstantsSlot]
	// 	->m_dx12ConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&cbGPUAddress));
	// memcpy(cbGPUAddress, &cam, sizeof(CameraConstants));
	// m_frameConstantBuffers[m_frameIndex][k_cameraConstantsSlot]
	// 	->m_dx12ConstantBuffer->Unmap(0, nullptr);
	// m_constantBuffers[k_cameraConstantsSlot]->AppendData(&cam, sizeof(CameraConstants), m_frameIndex); 
	// BindConstantBuffer(k_cameraConstantsSlot,
	// 	m_constantBuffers[k_cameraConstantsSlot]);
	m_constantBuffers[k_cameraConstantsSlot]->AppendData(&m_currentCam, sizeof(CameraConstants), m_currentDrawIndex); 
	BindConstantBuffer(k_cameraConstantsSlot,
		m_constantBuffers[k_cameraConstantsSlot]);
}

void DX12Renderer::EndCamera(Camera const& camera)
{
	UNUSED(camera);
}

void DX12Renderer::BeginForwardPass()
{
	if (m_currentActivePass != ActivePass::BACKBUFFER)
	{
		// Indicate that the back buffer will be used as a render target.
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);
	}
	m_currentActivePass = ActivePass::BACKBUFFER;
	m_forwardPassActive = true;
}

void DX12Renderer::ClearForwardPassRTV(Rgba8 const& clearColor)
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	float colorAsFloats[4];
	clearColor.GetAsFloats(colorAsFloats);
	m_commandList->ClearRenderTargetView(rtvHandle, colorAsFloats, 0, nullptr);

	m_hasBackBufferCleared = true;
}

void DX12Renderer::EndForwardPass()
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex],
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		);
	m_commandList->ResourceBarrier(1, &barrier);

	m_forwardPassActive = false;
}

void DX12Renderer::SetViewport(const AABB2& normalizedViewport)
{
	UNUSED(normalizedViewport);
	//TODO: make it work
}

void DX12Renderer::SetModelConstants(Mat44 const& modelMatrix, Rgba8 const& modelColor)
{
	ModelConstants modelConstants;
	modelConstants.ModelToWorldTransform = modelMatrix;
	modelColor.GetAsFloats(modelConstants.ModelColor);

	m_constantBuffers[k_modelConstantsSlot]->AppendData(&modelConstants, sizeof(ModelConstants), m_currentDrawIndex); 
	BindConstantBuffer(k_modelConstantsSlot, m_constantBuffers[k_modelConstantsSlot]);
}

void DX12Renderer::SetLightConstants(Vec3 const& lightPosition, float ambient, Mat44 const& lightViewMatrix, Mat44 const& lightProjectionMatrix)
{
	UNUSED(lightPosition); UNUSED(ambient); UNUSED(lightViewMatrix); UNUSED(lightProjectionMatrix);
}

void DX12Renderer::SetPerFrameConstants(const float time, const int debugInt, const float debugFloat)
{
	PerFrameConstants perFrameConstants = {};
	perFrameConstants.Time = time;
	perFrameConstants.DebugInt = debugInt;
	perFrameConstants.DebugFloat = debugFloat;

	m_constantBuffers[k_perFrameConstantsSlot]->AppendData(&perFrameConstants, sizeof(perFrameConstants), m_currentDrawIndex);
	BindConstantBuffer(k_perFrameConstantsSlot, m_constantBuffers[k_perFrameConstantsSlot]);
}

void DX12Renderer::SetGeneralLightConstants(Rgba8 sunColor, const Vec3& sunNormal, int numLights,
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
	m_lightConstant = lightConstant;
	m_constantBuffers[k_generalLightConstantsSlot]->AppendData(&lightConstant, sizeof(GeneralLightConstants), m_currentDrawIndex);
	BindConstantBuffer(k_generalLightConstantsSlot, m_constantBuffers[k_generalLightConstantsSlot]);
}

void DX12Renderer::SetMaterialConstants(const Texture* diffuseTex, const Texture* normalTex, const Texture* specTex)
{
	int index = k_materialConstantsSlot;
	ConstantBuffer* cb = m_constantBuffers[index];

	MaterialConstants materialConstant = {};
	if (diffuseTex)
		materialConstant.DiffuseId = diffuseTex->m_textureDescIndex;
	else
		materialConstant.DiffuseId = 0;
	if (normalTex)
		materialConstant.NormalId = normalTex->m_textureDescIndex;
	else
		materialConstant.NormalId = 1;

	if (specTex)
		materialConstant.SpecularId = specTex->m_textureDescIndex;
	else
		materialConstant.SpecularId = 2;
	
	// void* gpuPtr = nullptr;
	// CD3DX12_RANGE range(0, 0);
	// cb->m_dx12ConstantBuffer->Map(0, &range, &gpuPtr);
	// memcpy(gpuPtr, &materialConstant, sizeof(materialConstant));
	// cb->m_dx12ConstantBuffer->Unmap(0, nullptr);
	cb->AppendData(&materialConstant, sizeof(MaterialConstants), m_currentDrawIndex); 
	BindConstantBuffer(k_materialConstantsSlot, cb);
}

void DX12Renderer::DrawVertexArray(int numVertexes, const Vertex_PCU* vertex)
{
	if (numVertexes == 0)
	{
		return;
	}
	
	const unsigned int vertexBufferSize = sizeof(Vertex_PCU) * numVertexes;

	VertexBuffer* ringBuffer = m_frameVertexBuffers[m_frameIndex];

	// 拷贝数据进 ring buffer（内部更新 view）
	unsigned int offset = ringBuffer->AppendData(vertex, vertexBufferSize);

	// 3. 调用 Draw：用当前 view + stride
	DrawVertexBuffer(ringBuffer, numVertexes, (int)offset);
}

void DX12Renderer::DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts)
{
	UNUSED(numVerts);
	UNUSED(verts);
	ERROR_AND_DIE("Dont use this!")
}

void DX12Renderer::DrawVertexArray(std::vector<Vertex_PCU> const& verts)
{
	DrawVertexArray((int)verts.size(), verts.data());
}

void DX12Renderer::DrawVertexArray(std::vector<Vertex_PCUTBN> const& verts)
{
	if ((int)verts.size() < 3)
	{
		return;
	}

	const unsigned int vertexBufferSize = sizeof(Vertex_PCUTBN) * (int)verts.size();

	VertexBuffer* ringBuffer = m_frameVertexBuffers[m_frameIndex];

	unsigned int offset = ringBuffer->AppendData(verts.data(), vertexBufferSize);

	DrawVertexBuffer(ringBuffer, (int)verts.size(), (int)offset);
}

void DX12Renderer::DrawVertexIndexArray(std::vector<Vertex_PCU> const& verts, std::vector<unsigned int> const& indexes)
{
	int numOfIndexes = (int)indexes.size();
	if (numOfIndexes == 0)
	{
		return;
	}
	int numOfVerts = (int)verts.size();
	if (numOfVerts == 0)
	{
		return;
	}

	const unsigned int vertexBufferSize = sizeof(Vertex_PCU) * numOfVerts;
	const unsigned int indexBufferSize = sizeof(unsigned int) * numOfIndexes;

	VertexBuffer* ringVBO = m_frameVertexBuffers[m_frameIndex];
	IndexBuffer* ringIBO = m_frameIndexBuffers[m_frameIndex];

	ringVBO->AppendData(verts.data(), vertexBufferSize);       // 单位：字节
	unsigned int iboOffset = ringIBO->AppendData(indexes.data(), indexBufferSize);     // 单位：字节

	DrawIndexedVertexBuffer(ringVBO, ringIBO, numOfIndexes, iboOffset / sizeof(unsigned int)); //offset 以索引为单位
}

void DX12Renderer::DrawVertexIndexArray(std::vector<Vertex_PCUTBN> const& verts, std::vector<unsigned int> const& indexes)
{
	int numOfIndexes = (int)indexes.size();
	if (numOfIndexes == 0)
	{
		return;
	}
	int numOfVerts = (int)verts.size();
	if (numOfVerts == 0)
	{
		return;
	}

	const unsigned int vertexBufferSize = sizeof(Vertex_PCUTBN) * numOfVerts;
	const unsigned int indexBufferSize = sizeof(unsigned int) * numOfIndexes;

	VertexBuffer* ringVBO = m_frameVertexBuffers[m_frameIndex];
	IndexBuffer* ringIBO = m_frameIndexBuffers[m_frameIndex];

	ringVBO->AppendData(verts.data(), vertexBufferSize);     
	unsigned int iboOffset = ringIBO->AppendData(indexes.data(), indexBufferSize);    

	DrawIndexedVertexBuffer(ringVBO, ringIBO, numOfIndexes, (int)iboOffset / sizeof(unsigned int)); //offset 以索引为单位
}

void DX12Renderer::DrawVertexBuffer(VertexBuffer* vbo, int vertexCount, int vertexOffset /*= 0 */)
{
	// m_constantBuffers[k_cameraConstantsSlot]->AppendData(&m_currentCam, sizeof(CameraConstants), m_currentDrawIndex); 
	// BindConstantBuffer(k_cameraConstantsSlot,
	// 	m_constantBuffers[k_cameraConstantsSlot]);
	
	SetGraphicsStatesIfChanged();
	BindVertexBuffer(vbo);
	
	// offset 正确传入，单位是“顶点数”
	m_commandList->DrawInstanced(vertexCount, 1, vertexOffset, 0);
	m_currentDrawIndex ++;
}

void DX12Renderer::DrawIndexedVertexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, int indexCount, int indexOffset)
{
	SetGraphicsStatesIfChanged();
	BindVertexBuffer(vbo);
	BindIndexBuffer(ibo);
	m_commandList->DrawIndexedInstanced(indexCount, 1, indexOffset, 0, 0);

	m_currentDrawIndex++;
}

Shader* DX12Renderer::CreateShader(char const* shaderName, VertexType type)
{
	Shader* test = GetShaderByName(shaderName);
	if (test)
	{
		return test;
	}
	std::string shaderHLSLName = std::string(shaderName) + ".hlsl";

	std::string shaderSource;
	int result = FileReadToString(shaderSource, shaderHLSLName);
	if (result < 0)
	{
		ERROR_AND_DIE("Fail to create shader from this shaderName");
	}
	return CreateShader(shaderName, shaderSource.c_str(), type);
}

Shader* DX12Renderer::CreateShader(char const* shaderName, char const* shaderSource, VertexType type /*= VertexType::PCU */)
{
	Shader* test = GetShaderByName(shaderName);
	if (test)
	{
		return test;
	}

	ShaderConfig sConfig;
	sConfig.m_name = std::string(shaderName);
	Shader* shader = new Shader(sConfig);

	ID3DBlob* vertexShader;
	ID3DBlob* pixelShader;

	CompileShaderToByteCode(&vertexShader, "VertexShader", shaderSource, sConfig.m_vertexEntryPoint.c_str(), "vs_5_0");
	CompileShaderToByteCode(&pixelShader, "PixelShader", shaderSource, sConfig.m_pixelEntryPoint.c_str(), "ps_5_1");

	static D3D12_INPUT_ELEMENT_DESC inputElementDescsForPCU[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	static D3D12_INPUT_ELEMENT_DESC inputElementDescsForPCUTBN[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	shader->m_dx12VertexShader = vertexShader;
	shader->m_dx12PixelShader = pixelShader;
	shader->m_inputLayoutForVertex = new D3D12_INPUT_LAYOUT_DESC();
	if (type == VertexType::VERTEX_PCU)
	{
		shader->m_inputLayoutForVertex->pInputElementDescs = inputElementDescsForPCU;
		shader->m_inputLayoutForVertex->NumElements = _countof(inputElementDescsForPCU);
	}
	else if (type == VertexType::VERTEX_PCUTBN)
	{
		shader->m_inputLayoutForVertex->pInputElementDescs = inputElementDescsForPCUTBN;
		shader->m_inputLayoutForVertex->NumElements = _countof(inputElementDescsForPCUTBN);
	}

	shader->m_shaderIndex = (int)m_loadedShaders.size();

	GUARANTEE_OR_DIE(shader->m_shaderIndex <= 65535, "Cannot create more than 65535 shaders!");
	m_loadedShaders.push_back(shader);
	return shader;
}

bool DX12Renderer::CompileShaderToByteCode(ID3DBlob** shaderByteCode, char const* name, char const* source, char const* entryPoint, char const* target)
{
#if defined ENGINE_DEBUG_RENDER
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | 
		D3DCOMPILE_PREFER_FLOW_CONTROL | D3DCOMPILE_ENABLE_STRICTNESS	|
		D3DCOMPILE_DEBUG_NAME_FOR_SOURCE | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#else
	UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#endif

	ID3DBlob* shaderBlob = NULL;
	ID3DBlob* errorBlob = NULL;

	ShaderIncludeHandler includeHandler("Data/Shaders");

	// Compile shader
	HRESULT hr = D3DCompile(
		source, strlen(source),
		name, nullptr,  &includeHandler,
		entryPoint, target, compileFlags, 0,
		&shaderBlob, &errorBlob
	);
	if (SUCCEEDED(hr))
	{
		*shaderByteCode = shaderBlob;
		if (errorBlob != NULL)
		{
			errorBlob->Release();
		}
		return true;
	}
	else
	{
		if (errorBlob != NULL)
		{
			DebuggerPrintf((char*)errorBlob->GetBufferPointer());
		}
		if (errorBlob != NULL)
		{
			errorBlob->Release();
		}
		ERROR_AND_DIE(Stringf("Could not compile %s.", name));
	}
}

void DX12Renderer::BindShader(Shader* shader)
{
	if (shader == nullptr)
	{
		//m_desiredShader = m_defaultShader;
		if (m_currentRenderMode == RenderMode::GI)
			m_desiredShader = m_defaultGBufferShader;
		if(m_currentRenderMode == RenderMode::FORWARD)
			m_desiredShader = m_defaultShader;
	}
	else
	{
		m_desiredShader = shader;
	}
}

Shader* DX12Renderer::GetShaderByName(char const* shaderName)
{
	for (Shader* shader : m_loadedShaders)
	{
		std::string thisName = shader->GetName();
		if (thisName == shaderName)
		{
			return shader;
		}
	}
	return nullptr;
}

VertexBuffer* DX12Renderer::CreateVertexBuffer(unsigned int const size, unsigned int stride)
{
	// 仅创建upload heap，后续对于大的资产和需求应添加default heap
	// VertexBuffer* vertBuffer = new VertexBuffer(size, stride);
	//
	// CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	// CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	// HRESULT hr = m_device->CreateCommittedResource(
	// 	&heapProps,
	// 	D3D12_HEAP_FLAG_NONE,
	// 	&desc,
	// 	D3D12_RESOURCE_STATE_GENERIC_READ,
	// 	nullptr,
	// 	IID_PPV_ARGS(&vertBuffer->m_dx12VertexBuffer));
	// GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot Create Vertex Buffer!");
	VertexBuffer* vertBuffer = new VertexBuffer(m_device, size, stride);

	return vertBuffer;
}

void DX12Renderer::CopyCPUToGPU(void const* data, unsigned int size, VertexBuffer* vbo)
{
	UINT8* pVertexDataBegin;
	CD3DX12_RANGE readRange(0, 0);
	vbo->m_dx12VertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	memcpy(pVertexDataBegin, data, size);
	vbo->m_dx12VertexBuffer->Unmap(0, nullptr);

	vbo->m_vertexBufferView.BufferLocation = vbo->m_dx12VertexBuffer->GetGPUVirtualAddress();
	vbo->m_vertexBufferView.StrideInBytes = sizeof(Vertex_PCUTBN);
	vbo->m_vertexBufferView.SizeInBytes = (UINT)size;
}

void DX12Renderer::CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer* ibo)
{
	UINT8* pIndexDataBegin;
	CD3DX12_RANGE readRange(0, 0);       
	ibo->m_dx12IndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin));
	memcpy(pIndexDataBegin, data, size);
	ibo->m_dx12IndexBuffer->Unmap(0, nullptr);

	ibo->m_indexBufferView.BufferLocation = ibo->m_dx12IndexBuffer->GetGPUVirtualAddress();
	ibo->m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	ibo->m_indexBufferView.SizeInBytes = (UINT)size;
}

void DX12Renderer::CopyCPUToGPU(const void* data, unsigned int size, ID3D12Resource* buffer)
{
	void* mappedData = nullptr;
	D3D12_RANGE readRange = {0, 0};
	buffer->Map(0, &readRange, &mappedData);
	memcpy(mappedData, data, size);
	buffer->Unmap(0, nullptr);
}

void DX12Renderer::CopyUploadHeapToDefaultHeap(ID3D12Resource* defaultHeap, ID3D12Resource* uploadHeap)
{
	CD3DX12_RESOURCE_BARRIER before = CD3DX12_RESOURCE_BARRIER::Transition(
		defaultHeap,  // DEFAULT heap
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	m_commandList->ResourceBarrier(1, &before);
    
	m_commandList->CopyResource(
		defaultHeap,      // DEFAULT heap（目标）
		uploadHeap // UPLOAD heap（源）
	);
}

void DX12Renderer::BindIndexBuffer(IndexBuffer* ibo)
{
	m_commandList->IASetIndexBuffer(&ibo->m_indexBufferView);
}

Texture* DX12Renderer::CreateOrGetTextureFromFile(char const* imageFilePath)
{
	Texture* existingTexture = GetTextureByFileName(imageFilePath);
	if (existingTexture)
	{
		return existingTexture;
	}

	Texture* newTexture = CreateTextureFromFile(imageFilePath);
	return newTexture;
}

BitmapFont* DX12Renderer::CreateOrGetBitmapFont(char const* bitmapFontFilePathWithNoExtension)
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

Texture* DX12Renderer::GetTextureByFileName(char const* imageFilePath)
{
	for (int i = 0; i < m_loadedTextures.size(); i++)
	{
		if (m_loadedTextures[i] && m_loadedTextures[i]->GetImageFilePath() == imageFilePath)
		{
			return m_loadedTextures[i];
		}
	}
	return nullptr;
}

Image* DX12Renderer::CreateImageFromFile(char const* filePath)
{
	return new Image(filePath);
}

Texture* DX12Renderer::CreateTextureFromImage(Image const& image)
{
	Texture* newTexture = new Texture();
	newTexture->m_dimensions = image.GetDimensions();
	
	newTexture->m_textureDescIndex = (int)m_loadedTextures.size();

	GUARANTEE_OR_DIE(newTexture->m_textureDescIndex < 200, Stringf("Cannot create more than %d textures!", 200));

	IntVec2 dims = image.GetDimensions();
	int rowPitch = dims.x * 4; // 4 bytes per pixel (RGBA8)
	std::vector<unsigned char> flippedData(rowPitch * dims.y);
	unsigned char const* src = static_cast<unsigned char const*>(image.GetRawData());
	for (int y = 0; y < dims.y; ++y)
	{
		memcpy(&flippedData[y * rowPitch], &src[(dims.y - 1 - y) * rowPitch], rowPitch);
	}
	
	D3D12_RESOURCE_DESC resourceDescription = {};
	resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDescription.Alignment = 0; // may be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
	resourceDescription.Width = newTexture->m_dimensions.x; // width of the texture
	resourceDescription.Height = newTexture->m_dimensions.y; // height of the texture
	resourceDescription.DepthOrArraySize = 1; // if 3d image, depth of 3d image. Otherwise an array of 1D or 2D textures (we only have one image, so we set 1)
	resourceDescription.MipLevels = 1; // Number of mipmaps. We are not generating mipmaps for this texture, so we have only one level
	resourceDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // This is the dxgi format of the image (format of the pixels)
	resourceDescription.SampleDesc.Count = 1; // This is the number of samples per pixel, we just want 1 sample
	resourceDescription.SampleDesc.Quality = 0; // The quality level of the samples. Higher is better quality, but worse performance
	resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // The arrangement of the pixels. Setting to unknown lets the driver choose the most efficient one
	resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE; // no flags

	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// create a default heap where the upload heap will copy its contents into (contents being the texture)
	HRESULT hr = m_device->CreateCommittedResource(
		&properties, // a default heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&resourceDescription, // the description of our texture
		D3D12_RESOURCE_STATE_COPY_DEST, // We will copy the texture from the upload heap to here, so we start it out in a copy dest state
		nullptr, // used for render targets and depth/stencil buffers
		IID_PPV_ARGS(&newTexture->m_dx12Texture));

	std::wstring texName = L"Texture_" + std::to_wstring(newTexture->m_textureDescIndex);
	newTexture->m_dx12Texture->SetName(texName.c_str());

	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Cannot create texture committed resource!");

	// upload the texture to GPU; store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = flippedData.data();
	textureData.RowPitch = image.GetDimensions().x * 32 / 8; // size of all our triangle vertex data
	textureData.SlicePitch = image.GetDimensions().x * image.GetDimensions().y; // also the size of our triangle vertex data

	GUARANTEE_OR_DIE(newTexture->m_dx12Texture != nullptr, "Cannot create texture!");
	
	UINT64 textureUploadBufferSize; 
	m_device->GetCopyableFootprints(&resourceDescription, 0, 1, 0, nullptr, nullptr, nullptr, &textureUploadBufferSize);

	properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize);
	// now we create an upload heap to upload our texture to the GPU
	hr = m_device->CreateCommittedResource(
		&properties, // upload heap
		D3D12_HEAP_FLAG_NONE, // no flags
		&resourceDesc, // resource description for a buffer (storing the image data in this heap just to copy to the default heap)
		D3D12_RESOURCE_STATE_GENERIC_READ, // We will copy the contents from this heap to the default heap above
		nullptr,
		IID_PPV_ARGS(&newTexture->m_textureBufferUploadHeap));
	std::wstring upName = L"TextureUpload_" + std::to_wstring(newTexture->m_textureDescIndex);
	newTexture->m_textureBufferUploadHeap->SetName(upName.c_str());

	UpdateSubresources(m_commandList, newTexture->m_dx12Texture, newTexture->m_textureBufferUploadHeap, 0, 0, 1, &textureData);

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(newTexture->m_dx12Texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_commandList->ResourceBarrier(1, &barrier);
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
	                                               NUM_CONSTANT_BUFFERS +
	                                               newTexture->m_textureDescIndex, m_scuDescriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	m_device->CreateShaderResourceView(newTexture->m_dx12Texture, &srvDesc, descriptorHandle);

	//m_loadedTextures.push_back(newTexture);
	return newTexture;
}

void DX12Renderer::PushBackNewTextureManually(Texture* const tex)
{
	m_loadedTextures.push_back(tex);
}

void DX12Renderer::BindTexture(const Texture* texture, int slot)
{
	if (texture == nullptr)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		                                               NUM_CONSTANT_BUFFERS + m_defaultTexture->
		                                               m_textureDescIndex, m_scuDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(slot+NUM_CONSTANT_BUFFERS, descriptorHandle);
	}
	else
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHandle(m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		                                               NUM_CONSTANT_BUFFERS + texture->
		                                               m_textureDescIndex, m_scuDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(slot+NUM_CONSTANT_BUFFERS, descriptorHandle);
	}
	m_currentTexture = texture;

	//  Descriptor Heap Layout(CBV + SRV) :
	//	+-------------------------- + ------------------------------ - +
	//	| Constant Buffers(CBVs)	| Textures(SRVs)				  |
	//	| 0 1 2 ... N - 1			| [0] = white[1] = diffuseMap ... |
	//	+-------------------------- + ------------------------------ - +
	//				^							^
	//				|                           |
	//	m_numCBs * drawCall			+	texture->m_textureIndex
}

void DX12Renderer::SetBlendMode(BlendMode blendMode)
{
	m_desiredBlendMode = blendMode;
}

D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode)
{
	D3D12_BLEND_DESC blendDesc = {};
	if (mode == BlendMode::ADDITIVE)
	{
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlend;
		blendDesc.RenderTarget[0].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlend;
		blendDesc.RenderTarget[0].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOp;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	else if (mode == BlendMode::ALPHA)
	{
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		//blendDesc.RenderTarget[0].LogicOpEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlend;
		blendDesc.RenderTarget[0].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlend;
		blendDesc.RenderTarget[0].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOp;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	else //if (mode == BlendMode::OPAQUE)
	{
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = blendDesc.RenderTarget[0].SrcBlend;
		blendDesc.RenderTarget[0].DestBlendAlpha = blendDesc.RenderTarget[0].DestBlend;
		blendDesc.RenderTarget[0].BlendOpAlpha = blendDesc.RenderTarget[0].BlendOp;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
	return blendDesc;
}

D3D12_RASTERIZER_DESC MakeRasterizerDesc(RasterizerMode mode)
{
	D3D12_RASTERIZER_DESC rasterizerDesc = {};
	if (mode == RasterizerMode::SOLID_CULL_NONE)
	{
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDesc.FrontCounterClockwise = true;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.f;
		rasterizerDesc.SlopeScaledDepthBias = 0.f;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = true;
	}
	else if (mode == RasterizerMode::SOLID_CULL_BACK)
	{
		rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		rasterizerDesc.FrontCounterClockwise = true;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.f;
		rasterizerDesc.SlopeScaledDepthBias = 0.f;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = true;
	}
	else if (mode == RasterizerMode::WIREFRAME_CULL_BACK)
	{
		rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
		rasterizerDesc.FrontCounterClockwise = true;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.f;
		rasterizerDesc.SlopeScaledDepthBias = 0.f;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = true;
	}
	else if (mode == RasterizerMode::WIREFRAME_CULL_NONE)
	{
		rasterizerDesc.FillMode = D3D12_FILL_MODE_WIREFRAME;
		rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
		rasterizerDesc.FrontCounterClockwise = true;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0.f;
		rasterizerDesc.SlopeScaledDepthBias = 0.f;
		rasterizerDesc.DepthClipEnable = true;
		rasterizerDesc.MultisampleEnable = false;
		rasterizerDesc.AntialiasedLineEnable = true;
	}
	return rasterizerDesc;
}

D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode mode)
{
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};

	if (mode == DepthMode::DISABLED)
	{
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	}
	else
	{
		depthStencilDesc.DepthEnable = TRUE;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	}

	return depthStencilDesc;
}

void DX12Renderer::SetGraphicsStatesIfChanged()
{
	if (m_currentRasterizerMode == m_desiredRasterizerMode && m_currentBlendMode == m_desiredBlendMode
		&& m_currentDepthMode == m_desiredDepthMode && m_currentSamplerMode == m_desiredSamplerMode
		&& m_currentShader == m_desiredShader)
	{
		m_commandList->SetPipelineState(m_pipelineStateObject);
		return;
	}
	m_currentRasterizerMode = m_desiredRasterizerMode;
	m_currentBlendMode = m_desiredBlendMode;
	m_currentDepthMode = m_desiredDepthMode;
	m_currentSamplerMode = m_desiredSamplerMode;
	m_currentShader = m_desiredShader;

	int indexNum = (m_desiredShader->m_shaderIndex << 16) | ((int)m_desiredDepthMode << 12)
		| ((int)m_desiredSamplerMode << 8) | ((int)m_desiredRasterizerMode << 4) | ((int)m_desiredBlendMode);

	if (m_currentRenderMode == RenderMode::FORWARD)
	{
		auto iter = m_pipelineStatesConfiguration.find(indexNum);
		if (iter != m_pipelineStatesConfiguration.end())
		{
			m_commandList->SetPipelineState(iter->second);
			m_pipelineStateObject = iter->second;
			return;
		}
	}
	if (m_currentRenderMode == RenderMode::GI)
	{
		auto iter = m_gBufferPipelineStatesConfiguration.find(indexNum);
		if (iter != m_gBufferPipelineStatesConfiguration.end())
		{
			m_commandList->SetPipelineState(iter->second);
			m_pipelineStateObject = iter->second;
			return;
		}
	}
	
	ID3D12PipelineState* pipelineState = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = *m_desiredShader->m_inputLayoutForVertex;
	psoDesc.pRootSignature = m_graphicsRootSignature;
	psoDesc.VS =
	{
		reinterpret_cast<UINT8*>(m_desiredShader->m_dx12VertexShader->GetBufferPointer()),
		m_desiredShader->m_dx12VertexShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<UINT8*>(m_desiredShader->m_dx12PixelShader->GetBufferPointer()),
		m_desiredShader->m_dx12PixelShader->GetBufferSize()
	};
	psoDesc.RasterizerState = MakeRasterizerDesc(m_desiredRasterizerMode);
	psoDesc.BlendState = MakeBlendDesc(m_desiredBlendMode);
	psoDesc.DepthStencilState = MakeDepthStencilDesc(m_desiredDepthMode);
	//psoDesc.SampleDesc 因为没有multi sample，不用设置，sample count在创建swapchain时设置为1了
	psoDesc.SampleMask = 0xffffffff;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; //需要深度
	psoDesc.SampleDesc.Count = 1;
	if (m_currentRenderMode ==RenderMode::FORWARD)
	{
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	if (m_currentRenderMode == RenderMode::GI) //4个RTV+1个DSV（m_depth）
	{
		psoDesc.NumRenderTargets = 4;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;  
		psoDesc.RTVFormats[1] = DXGI_FORMAT_R10G10B10A2_UNORM;
		psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	}
	HRESULT hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Create Graphics Pipeline State!");
	std::wstring debugName = L"PSO_" 
    + std::wstring(m_desiredShader->m_config.m_name.begin(), m_desiredShader->m_config.m_name.end())
    + L"_Rs" + std::to_wstring((int)m_desiredRasterizerMode)
    + L"_Bl" + std::to_wstring((int)m_desiredBlendMode)
    + L"_Dp" + std::to_wstring((int)m_desiredDepthMode)
    + L"_Sp" + std::to_wstring((int)m_desiredSamplerMode)
	+ L"_NUMRTV" + std::to_wstring((int)psoDesc.NumRenderTargets);

	pipelineState->SetName(debugName.c_str());

	m_commandList->SetPipelineState(pipelineState);
	if (m_currentRenderMode == RenderMode::FORWARD)
		m_pipelineStatesConfiguration[indexNum] = pipelineState;
	if (m_currentRenderMode == RenderMode::GI)
		m_gBufferPipelineStatesConfiguration[indexNum] = pipelineState;
	
	m_pipelineStateObject = pipelineState;
}

void DX12Renderer::CreateDepthSRV()
{
	// 用depthBuffer创建
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;		//SRV格式与DSV格式不同
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		NUM_CONSTANT_BUFFERS + MAX_TEXTURE_COUNT + GBUFFER_COUNT,  // 在GBuffer SRV之后
		m_scuDescriptorSize
	);

	m_device->CreateShaderResourceView(
		m_depthStencilBuffer, 
		&srvDesc,
		srvHandle
	);
}

void DX12Renderer::CreateGBufferPassResources()
{
	D3D12_CLEAR_VALUE albedoClearValue = {};
	albedoClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	albedoClearValue.Color[0] = 0.0f; // R
	albedoClearValue.Color[1] = 0.0f; // G
	albedoClearValue.Color[2] = 0.0f; // B
	albedoClearValue.Color[3] = 1.0f; // A

	D3D12_CLEAR_VALUE normalClearValue = {};
	normalClearValue.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	normalClearValue.Color[0] = 0.5f; // X
	normalClearValue.Color[1] = 0.5f; // Y
	normalClearValue.Color[2] = 1.0f; // Z
	normalClearValue.Color[3] = 1.0f; // A

	D3D12_CLEAR_VALUE materialClearValue = {};
	materialClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	materialClearValue.Color[0] = 1.0f; // R
	materialClearValue.Color[1] = 0.0f; // G
	materialClearValue.Color[2] = 1.0f; // B
	materialClearValue.Color[3] = 0.0f; // A

	D3D12_CLEAR_VALUE motionClearValue = {};
	motionClearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	motionClearValue.Color[0] = 0.0f; // X
	motionClearValue.Color[1] = 0.0f; // Y
	motionClearValue.Color[2] = 0.0f; // Z
	motionClearValue.Color[3] = 0.0f; // A

	m_gBuffer.m_gBufferClearValues[0] = albedoClearValue;
	m_gBuffer.m_gBufferClearValues[1] = normalClearValue;
	m_gBuffer.m_gBufferClearValues[2] = materialClearValue;
	m_gBuffer.m_gBufferClearValues[3] = motionClearValue;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = m_config.m_window->GetClientDimensions().x;
	desc.Height = m_config.m_window->GetClientDimensions().y;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    
	HRESULT hr;
	// Albedo
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &albedoClearValue,
		IID_PPV_ARGS(&m_gBuffer.m_albedo));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create GBuffer albedo!")
	
		m_gBuffer.m_albedo->SetName(L"GBufferAlbedo");
    
	// Normal
	desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &normalClearValue,
		IID_PPV_ARGS(&m_gBuffer.m_normal));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create GBuffer normal!");
	m_gBuffer.m_normal->SetName(L"GBufferNormal");
    
	// Material
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &materialClearValue,
		IID_PPV_ARGS(&m_gBuffer.m_material));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create GBuffer material!");
	m_gBuffer.m_material->SetName(L"GBufferMaterial");
    
	// Motion Vectors -> 改为WorldPos
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &motionClearValue,
		IID_PPV_ARGS(&m_gBuffer.m_worldPos));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create GBuffer normal!");
	m_gBuffer.m_worldPos->SetName(L"GBufferWorldPos");

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		NUM_CONSTANT_BUFFERS + MAX_TEXTURE_COUNT,  // cbvsrv描述符堆中的实际位置
		m_scuDescriptorSize
	);

	// Create srv for GBuffer
	for (int i = 0; i < GBUFFER_COUNT; ++i)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		switch (i)
		{
		case 0: // Albedo
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case 1: // Normal  
			srvDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
			break;
		case 2: // Material
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case 3: // Motion -改为worldPos
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			break;
		}
		m_device->CreateShaderResourceView(
			m_gBuffer.GetResource(i),
			&srvDesc,
			srvHandle
		);
		srvHandle.Offset(1, m_scuDescriptorSize);
	}
	//创建完GBuffer的ID3D12Resources后补上它们的rtv
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());
	rtvHandle.Offset(FRAME_BUFFER_COUNT, m_rtvDescriptorSize);
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	// 建议为每个 GBuffer 明确 RTV 格式，避免误用 sRGB（GBuffer 通常线性）
	// Albedo
	{
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_device->CreateRenderTargetView(m_gBuffer.m_albedo, &rtvDesc, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}

	// Normal
	{
		rtvDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_device->CreateRenderTargetView(m_gBuffer.m_normal, &rtvDesc, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}
	// Material
	{
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_device->CreateRenderTargetView(m_gBuffer.m_material, &rtvDesc, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);
	}
	// Motion ->改为worldPos
	{
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		m_device->CreateRenderTargetView(m_gBuffer.m_worldPos, &rtvDesc, rtvHandle);
	}

	ShaderConfig cfg;
	cfg.m_name = "DefaultGBufferShader";
	cfg.m_vertexEntryPoint = "GBufferVS";
	cfg.m_pixelEntryPoint = "GBufferPS";

	m_defaultGBufferShader = new Shader(cfg);

	ID3DBlob* vs = nullptr;
	ID3DBlob* ps = nullptr;

	CompileShaderToByteCode(&vs, "GBufferVS", m_gBufferShaderSource, cfg.m_vertexEntryPoint.c_str(), "vs_5_0");  //TODO: æ”¹GBuffer
	CompileShaderToByteCode(&ps, "GBufferPS", m_gBufferShaderSource, cfg.m_pixelEntryPoint.c_str(), "ps_5_1");

	static D3D12_INPUT_ELEMENT_DESC kPCUTBN[] = {
		{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",     0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	m_defaultGBufferShader->m_dx12VertexShader = vs;
	m_defaultGBufferShader->m_dx12PixelShader = ps;
	m_defaultGBufferShader->m_inputLayoutForVertex = new D3D12_INPUT_LAYOUT_DESC();
	m_defaultGBufferShader->m_inputLayoutForVertex->pInputElementDescs = kPCUTBN;
	m_defaultGBufferShader->m_inputLayoutForVertex->NumElements = _countof(kPCUTBN);

	m_defaultGBufferShader->m_shaderIndex = (int)m_loadedShaders.size();

	GUARANTEE_OR_DIE(m_defaultGBufferShader->m_shaderIndex <= 65535, "Cannot create more than 65535 shaders!");
	m_loadedShaders.push_back(m_defaultGBufferShader);
}

void DX12Renderer::BeginGBufferPass()
{
	if (m_currentActivePass != ActivePass::GBUFFER)
	{
		CD3DX12_RESOURCE_BARRIER barriers[GBUFFER_COUNT];
		for (int i = 0; i < GBUFFER_COUNT; ++i)
		{
			barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
				m_gBuffer.GetResource(i),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);
		}
		// barriers[GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
		// 	m_depthStencilBuffer,
		// 	D3D12_RESOURCE_STATE_DEPTH_READ,
		// 	D3D12_RESOURCE_STATE_DEPTH_WRITE
		// 	);
		m_commandList->ResourceBarrier(GBUFFER_COUNT, barriers);
	}

	m_currentActivePass = ActivePass::GBUFFER;
	m_gBufferPassActive = true;
}

void DX12Renderer::ClearGBufferPassRTV()
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBUFFER_COUNT];
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE it(
			m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(),
			FRAME_BUFFER_COUNT,
			m_rtvDescriptorSize
		);
		for (int i = 0; i < GBUFFER_COUNT; ++i)
		{
			rtvHandles[i] = it;
			it.Offset(1, m_rtvDescriptorSize);
		}
	}
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());

	m_commandList->OMSetRenderTargets(GBUFFER_COUNT, rtvHandles, FALSE, &dsvHandle);

	for (int i = 0; i < GBUFFER_COUNT; ++i)
	{
		m_commandList->ClearRenderTargetView(rtvHandles[i], m_gBuffer.m_gBufferClearValues[i].Color, 0, nullptr);
	}
	m_commandList->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr
	);
}

void DX12Renderer::EndGBufferPass()
{
	BeginCardCapturePass();
	EndCardCapturePass();
	 //BeginRadianceCachePass();
	
	RenderingGlobalSDFPass();
	
	RenderingSurfaceCacheRadiosityPass();
	RenderingCombineSurfaceCachePass();
	RenderingScreenProbeGatherPass();

	RenderingCompositePass();
	
	CD3DX12_RESOURCE_BARRIER postCompositeBarriers[] = {
		// 1. ScreenIndirectLighting（Composite使用的）
		CD3DX12_RESOURCE_BARRIER::Transition(
			m_screenProbeFinalGather->GetIndirectLightingTexture(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON
		),
		// 2. GlobalSDF（Radiosity和ScreenProbe使用的）
		CD3DX12_RESOURCE_BARRIER::Transition(
			m_globalSDFTexture,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON
		),
		// 3. VoxelLighting（Radiosity和ScreenProbe使用的）+Composite也可以用
		CD3DX12_RESOURCE_BARRIER::Transition(
			m_voxelLightingTexture,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			m_depthStencilBuffer,
			D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
	m_surfaceCache.m_atlasTexture,
	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
	D3D12_RESOURCE_STATE_COMMON,
	D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
),
CD3DX12_RESOURCE_BARRIER::Transition(
	m_surfaceCache.m_cardMetadataBuffer,
	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
	D3D12_RESOURCE_STATE_COMMON
),
	};
	m_commandList->ResourceBarrier(_countof(postCompositeBarriers), postCompositeBarriers);
	
	// CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
	// 		m_depthStencilBuffer,
	// 		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
	// 		D3D12_RESOURCE_STATE_DEPTH_WRITE
	// 	);
	// m_commandList->ResourceBarrier(1, &barrier);

	m_gBufferPassActive = false;
	m_radianceCache.SwapBuffers();
}

void DX12Renderer::UnbindGBufferPassRT()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		m_frameIndex,
		m_rtvDescriptorSize
	);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
}

void DX12Renderer::CreateSDFGenerationRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE ranges[2];
    
	// [0] = Input SRVs (t0-t3): Vertex, Index, BVHNode, BVHTriIndices
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);
	// [1] = Output UAV (u0): SDF Texture3D
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
    
	CD3DX12_ROOT_PARAMETER params[3];
	params[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
	params[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
	params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init(3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    
	ID3DBlob* signature = nullptr;
	ID3DBlob* error = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signature,
		&error
	);
	if (FAILED(hr))
	{
		if (error)
		{
			DebuggerPrintf("[DX12] SDF Root Signature error: %s\n", 
						  (char*)error->GetBufferPointer());
			error->Release();
		}
		GUARANTEE_OR_DIE(false, "Failed to serialize SDF root signature!");
	}
	hr = m_device->CreateRootSignature(
		0,
		signature->GetBufferPointer(),
		signature->GetBufferSize(),
		IID_PPV_ARGS(&m_sdfGenerationRootSignature)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create SDF root signature!");
	m_sdfGenerationRootSignature->SetName(L"SDFGenerationRootSig");
	if (signature) signature->Release();
}

void DX12Renderer::CreateSDFGenerationPSO()
{
	std::string shaderSource;
	int result = FileReadToString(shaderSource, "Data/Shaders/SDFGeneration.hlsl");
	GUARANTEE_OR_DIE(result >= 0, "Failed to load SDFGeneration.hlsl");
    
	ID3DBlob* cs = nullptr;
	CompileShaderToByteCode(&cs, "SDFGenerate", shaderSource.c_str(), "main", "cs_5_1");
    
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_sdfGenerationRootSignature;
	desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
    
	HRESULT hr = m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_sdfGenerationPSO));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create SDF generation PSO!");
	m_sdfGenerationPSO->SetName(L"SDFGenerationPSO");
	cs->Release();
}

SDFTexture3D* DX12Renderer::CreateSDFTextureFromData(const std::vector<Vertex_PCUTBN>& vertices,
                                             const std::vector<uint32_t>& indices, const BVH& bvh, const AABB3& bounds, int resolution)
{
	if (m_nextSDFTextureIndex >= MAX_SDF_TEXTURE_COUNT)
	{
		DebuggerPrintf("[DX12] Cannot generating more SDF textures.");
		return nullptr;
	}
	SDFTexture3D* sdf = new SDFTexture3D(resolution);
	DebuggerPrintf("[DX12] Starting GPU SDF generation (resolution=%d) on Async Compute Queue\n", resolution);
    
    std::vector<GPUBVHNode> bvhNodes;
    std::vector<uint32_t> bvhTriIndices;
    bvh.FlattenForGPU(bvhNodes, bvhTriIndices);
    if (bvhNodes.empty())
    {
        DebuggerPrintf("[DX12] BVH is empty, cannot generate SDF\n");
        return nullptr;
    }
    DebuggerPrintf("[DX12] SDF Gen: %zu verts, %zu indices, %zu BVH nodes\n",
                   vertices.size(), indices.size(), bvhNodes.size());

	if (m_computeFenceValue > 0)  // 如果不是第一次调用
	{
		WaitForComputeQueue();
		DebuggerPrintf("[DX12] Previous SDF generation completed, starting new one\n");
	}
	// 1. Reset Compute Allocator和CommandList
	m_computeAllocator->Reset();
	m_computeCommandList->Reset(m_computeAllocator, nullptr);
	
    ID3D12Resource* vertexBuffer = CreateStructuredBuffer(sdf, 
                                                          vertices.data(), 
                                                          vertices.size(),
                                                          sizeof(Vertex_PCUTBN)
                                                          ,
                                                          L"SDF_VertexBuffer", m_computeCommandList
    );
    ID3D12Resource* indexBuffer = CreateStructuredBuffer(sdf,
                                                         indices.data(),
                                                         indices.size(),
                                                         sizeof(uint32_t)
                                                         ,
                                                         L"SDF_IndexBuffer", m_computeCommandList
    );
    ID3D12Resource* bvhNodeBuffer = CreateStructuredBuffer(sdf,
                                                           bvhNodes.data(),
                                                           bvhNodes.size(),
                                                           sizeof(GPUBVHNode)
                                                           ,
                                                           L"SDF_BVHNodeBuffer", m_computeCommandList
    );
    ID3D12Resource* bvhTriBuffer = CreateStructuredBuffer(sdf,
                                                          bvhTriIndices.data(),
                                                          bvhTriIndices.size(),
                                                          sizeof(uint32_t)
                                                          ,
                                                          L"SDF_BVHTriIndicesBuffer", m_computeCommandList
    );
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        SDF_GEN_VERTEX_SRV,
        m_scuDescriptorSize
    );
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.StructureByteStride = sizeof(Vertex_PCUTBN);
    srvDesc.Buffer.NumElements = (UINT)vertices.size();
    m_device->CreateShaderResourceView(vertexBuffer, &srvDesc, srvHandle);
    
    srvHandle.Offset(1, m_scuDescriptorSize);
    srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    srvDesc.Buffer.NumElements = (UINT)indices.size();
    m_device->CreateShaderResourceView(indexBuffer, &srvDesc, srvHandle);
    
    srvHandle.Offset(1, m_scuDescriptorSize);
    srvDesc.Buffer.StructureByteStride = sizeof(GPUBVHNode);
    srvDesc.Buffer.NumElements = (UINT)bvhNodes.size();
    m_device->CreateShaderResourceView(bvhNodeBuffer, &srvDesc, srvHandle);
    
    srvHandle.Offset(1, m_scuDescriptorSize);
    srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    srvDesc.Buffer.NumElements = (UINT)bvhTriIndices.size();
    m_device->CreateShaderResourceView(bvhTriBuffer, &srvDesc, srvHandle);
    
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = resolution;
    texDesc.Height = resolution;
    texDesc.DepthOrArraySize = resolution;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    
    CD3DX12_HEAP_PROPERTIES defaultProps(D3D12_HEAP_TYPE_DEFAULT);
    
    HRESULT hr = m_device->CreateCommittedResource(
        &defaultProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,  // 🔥 改成COMMON，由Compute Queue管理状态
        nullptr,
        IID_PPV_ARGS(&sdf->m_sdfTexture3D)
    );
    
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Create SDF Texture3D failed");
    sdf->m_sdfTexture3D->SetName(L"SDFTexture3D");
    
    // 创建临时UAV（固定slot，所有SDF生成共用）
    CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        SDF_GEN_OUTPUT_UAV,
        m_scuDescriptorSize
    );
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.MipSlice = 0;
    uavDesc.Texture3D.FirstWSlice = 0;
    uavDesc.Texture3D.WSize = resolution;
    m_device->CreateUnorderedAccessView(sdf->m_sdfTexture3D, nullptr, &uavDesc, uavHandle);
	
    // 2. 设置Compute Pipeline
    m_computeCommandList->SetPipelineState(m_sdfGenerationPSO);
    m_computeCommandList->SetComputeRootSignature(m_sdfGenerationRootSignature);
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvDescHeap };
    m_computeCommandList->SetDescriptorHeaps(1, heaps);
    
    // 3. 转换资源到UAV状态
    CD3DX12_RESOURCE_BARRIER toUAV = CD3DX12_RESOURCE_BARRIER::Transition(
        sdf->m_sdfTexture3D,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    m_computeCommandList->ResourceBarrier(1, &toUAV);
    
    // 4. 绑定SRVs (t0-t3)
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrvHandle(
        m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
        SDF_GEN_VERTEX_SRV,
        m_scuDescriptorSize
    );
    m_computeCommandList->SetComputeRootDescriptorTable(0, gpuSrvHandle);
    
    // 5. 绑定UAV (u0)
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuUavHandle(
        m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
        SDF_GEN_OUTPUT_UAV,
        m_scuDescriptorSize
    );
    m_computeCommandList->SetComputeRootDescriptorTable(1, gpuUavHandle);
    
    // 6. 设置常量缓冲区
    SDFGenerationConstants constants;
    constants.BoundsMin = bounds.m_mins;
    constants.Resolution = resolution;
    constants.BoundsMax = bounds.m_maxs;
    constants.NumTriangles = (uint32_t)(indices.size() / 3);
	constants.NumVertices = static_cast<uint32_t>(vertices.size());
	constants.NumBVHNodes = static_cast<uint32_t>(bvhNodes.size());

	m_computeConstantBuffer->AppendData(&constants, sizeof(SDFGenerationConstants), 0);
	D3D12_GPU_VIRTUAL_ADDRESS constantsBufferAddress = 
		m_computeConstantBuffer->m_dx12ConstantBuffer->GetGPUVirtualAddress()
		+ m_computeConstantBuffer->m_offset;
	m_computeCommandList->SetComputeRootConstantBufferView(2, constantsBufferAddress);
	// m_constantBuffers[k_sdfGenerationConstantsSlot]->AppendData(&constants, sizeof(SDFGenerationConstants), m_currentDrawIndex);
	// D3D12_GPU_VIRTUAL_ADDRESS constantsBufferAddress = m_constantBuffers[k_sdfGenerationConstantsSlot]->m_dx12ConstantBuffer->GetGPUVirtualAddress()
	// + m_constantBuffers[k_sdfGenerationConstantsSlot]->m_offset;
 //    m_computeCommandList->SetComputeRootConstantBufferView(2, constantsBufferAddress);
    
    // 7. Dispatch
    int numGroups = (resolution + 7) / 8;
    m_computeCommandList->Dispatch(numGroups, numGroups, numGroups);
    
    DebuggerPrintf("[DX12] Dispatched SDF generation: %dx%dx%d groups\n", 
                   numGroups, numGroups, numGroups);
    
    // 8. UAV Barrier
    CD3DX12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(sdf->m_sdfTexture3D);
    m_computeCommandList->ResourceBarrier(1, &uavBarrier);
    
    // 9. 转换到SRV状态
    auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        sdf->m_sdfTexture3D,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    m_computeCommandList->ResourceBarrier(1, &toSRV);
    
    // 关键：提交到Async Compute Queue
    // 10. Close CommandList
    m_computeCommandList->Close();
    // 11. 提交到Compute Queue（异步执行，不阻塞！）
    ID3D12CommandList* computeLists[] = { m_computeCommandList };
    m_computeQueue->ExecuteCommandLists(1, computeLists);
    // 12. Signal Fence（不等待完成）
    m_computeFenceValue++;
    m_computeQueue->Signal(m_computeFence, m_computeFenceValue);
    
    DebuggerPrintf("[DX12] SDF generation submitted to Async Compute Queue (fence=%llu)\n", 
                   m_computeFenceValue);
    // 注意：这些temp buffers不能立即释放，因为Compute Queue还在使用, createStructuredBuffer时储存到tempBuffers里了
    // 创建最终的SRV
    if (m_nextSDFDescriptorIndex >= SDF_TEXTURE_SRV_BASE + MAX_SDF_TEXTURE_COUNT)
    {
        DebuggerPrintf("[DX12] SDF descriptor pool exhausted!\n");
        delete sdf;
        return nullptr;
    }
    sdf->m_srvHeapIndex = m_nextSDFDescriptorIndex;
	m_nextSDFDescriptorIndex ++;
	sdf->m_sdfTextureIndex = m_nextSDFTextureIndex;
	m_nextSDFTextureIndex ++;
    CD3DX12_CPU_DESCRIPTOR_HANDLE finalSrvHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        sdf->m_srvHeapIndex,
        m_scuDescriptorSize
    );
    D3D12_SHADER_RESOURCE_VIEW_DESC finalSrvDesc = {};
    finalSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    finalSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    finalSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    finalSrvDesc.Texture3D.MipLevels = 1;
    finalSrvDesc.Texture3D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(sdf->m_sdfTexture3D, &finalSrvDesc, finalSrvHandle);
    m_loadedSDFs.push_back(sdf);
    DebuggerPrintf("[DX12] SDF registered, SRV index: %d\n", sdf->m_srvHeapIndex);
    // 立即返回 不等待Compute Queue完成
    // SDF会在后台异步生成
    return sdf;
}

ID3D12Resource* DX12Renderer::CreateStructuredBuffer(SDFTexture3D* sdfOwner, const void* data, size_t numElements, size_t elementSize,
                                                     const wchar_t* debugName, ID3D12GraphicsCommandList* commandList)
{
	UINT64 bufferSize = numElements * elementSize;
    
	// 1. 创建Default Heap buffer
	CD3DX12_HEAP_PROPERTIES defaultProps(D3D12_HEAP_TYPE_DEFAULT);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);
    
	ID3D12Resource* buffer = nullptr;
	HRESULT hr = m_device->CreateCommittedResource(
		&defaultProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&buffer)
	);
    
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Create structured buffer failed");
	buffer->SetName(debugName);
    
	// 2. 创建Upload buffer
	CD3DX12_HEAP_PROPERTIES uploadProps(D3D12_HEAP_TYPE_UPLOAD);
	auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    
	ID3D12Resource* uploadBuffer = nullptr;
	hr = m_device->CreateCommittedResource(
		&uploadProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);
    
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Create upload buffer failed");
	// 3. 上传数据
	void* mapped = nullptr;
	uploadBuffer->Map(0, nullptr, &mapped);
	memcpy(mapped, data, bufferSize);
	uploadBuffer->Unmap(0, nullptr);
	// 4. 拷贝到Default heap
	commandList->CopyResource(buffer, uploadBuffer);
    
	// 5. Transition到SRV可读
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	commandList->ResourceBarrier(1, &barrier);
	uploadBuffer->SetName(L"SDFStructureUploadBuffer");
    
	//m_currentFrameTempResources.push_back(uploadBuffer);
	if (sdfOwner)
	{
		sdfOwner->m_tempBuffers.push_back(uploadBuffer);
		sdfOwner->m_tempBuffers.push_back(buffer);  // default heap buffer也存
	}

	return buffer;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Renderer::GetSRVHandle(uint32_t index)
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(
		m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		index,
		m_scuDescriptorSize
	);
}

SDFTexture3D* DX12Renderer::CreateSDFTextureFromData(const std::vector<float>& data, int resolution)
{
	if (data.empty() || resolution <= 0)
    {
        ERROR_RECOVERABLE("CreateSDF3DFromData: invalid parameters");
        return nullptr;
    }
    size_t expectedSize = static_cast<size_t>(resolution) * resolution * resolution;
    if (data.size() != expectedSize)
    {
        ERROR_RECOVERABLE(Stringf("CreateSDF3DFromData: data size mismatch (expected %zu, got %zu)",
                                  expectedSize, data.size()).c_str());
        return nullptr;
    }
    ID3D12Device* device = m_device;
    ID3D12GraphicsCommandList* commandList = m_commandList;
    if (!device || !commandList)
    {
        ERROR_RECOVERABLE("CreateSDF3DFromData: invalid D3D12 objects");
        return nullptr;
    }
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = resolution;
    texDesc.Height = resolution;
    texDesc.DepthOrArraySize = (UINT16)resolution;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeapProps = {};
    defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* texture = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)
    );

    if (FAILED(hr) || !texture)
    {
        ERROR_RECOVERABLE("Failed to create 3D texture");
        return nullptr;
    }

    UINT64 uploadBufferSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* uploadBuffer = nullptr;
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );
    if (FAILED(hr) || !uploadBuffer)
    {
        ERROR_RECOVERABLE("Failed to create upload buffer");
        texture->Release();
        return nullptr;
    }

    void* mappedData = nullptr;
    hr = uploadBuffer->Map(0, nullptr, &mappedData);
    if (SUCCEEDED(hr) && mappedData)
    {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalBytes);

        uint8_t* dstData = static_cast<uint8_t*>(mappedData);
        for (int z = 0; z < resolution; z++)
        {
            for (int y = 0; y < resolution; y++)
            {
                size_t srcOffset = (z * resolution * resolution + y * resolution) * sizeof(float);
                size_t dstOffset = z * layout.Footprint.Depth * layout.Footprint.RowPitch +
                                  y * layout.Footprint.RowPitch;
                
                memcpy(
                    dstData + dstOffset,
                    reinterpret_cast<const uint8_t*>(data.data()) + srcOffset,
                    resolution * sizeof(float)
                );
            }
        }
        uploadBuffer->Unmap(0, nullptr);
    }
    else
    {
        ERROR_RECOVERABLE("Failed to map upload buffer");
        uploadBuffer->Release();
        texture->Release();
        return nullptr;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, nullptr, nullptr, nullptr);

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer;
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = layout;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = texture;
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;
    commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    hr = commandList->Close();
    if (FAILED(hr))
    {
        ERROR_RECOVERABLE("Failed to close command list");
        uploadBuffer->Release();
        texture->Release();
        return nullptr;
    }
    ID3D12CommandList* cmdLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, cmdLists);

    WaitForPreviousFrame();
    uploadBuffer->Release();

    UINT srvIndex = m_nextSDFDescriptorIndex;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = GetCPUDescriptorHandle(m_cbvSrvDescHeap, srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Texture3D.MipLevels = 1;

    device->CreateShaderResourceView(texture, &srvDesc, srvHandle);

    SDFTexture3D* sdfTexture = new SDFTexture3D(resolution);
	sdfTexture->m_srvHeapIndex = m_nextSDFDescriptorIndex;
	m_nextSDFDescriptorIndex ++;
	sdfTexture->m_sdfTextureIndex = m_nextSDFTextureIndex;
	m_nextSDFTextureIndex ++;

    DebuggerPrintf("[DX12Renderer] Created SDF3D from data: resolution=%d, %zu floats\n",
                   resolution, data.size());
    return sdfTexture;
}

std::vector<float> DX12Renderer::ReadbackSDF3DData(const SDFTexture3D* sdf)
{
	std::vector<float> result;
    
    if (!sdf || !sdf->GetResource())
    {
        ERROR_RECOVERABLE("ReadbackSDF3DData: invalid SDF texture");
        return result;
    }
    ID3D12Device* device = m_device;
    ID3D12GraphicsCommandList* commandList = m_commandList;
    ID3D12Resource* resource = sdf->GetResource();
    if (!device || !commandList || !resource)
    {
        ERROR_RECOVERABLE("ReadbackSDF3DData: invalid D3D12 objects");
        return result;
    }
    D3D12_RESOURCE_DESC desc = resource->GetDesc();
    UINT64 totalSize = 0;
    
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalSize);
	
    D3D12_HEAP_PROPERTIES readbackHeapProps = {};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackBufferDesc = {};
    readbackBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackBufferDesc.Width = totalSize;
    readbackBufferDesc.Height = 1;
    readbackBufferDesc.DepthOrArraySize = 1;
    readbackBufferDesc.MipLevels = 1;
    readbackBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackBufferDesc.SampleDesc.Count = 1;
    readbackBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readbackBuffer = nullptr;
    HRESULT hr = device->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
    if (FAILED(hr) || !readbackBuffer)
    {
        ERROR_RECOVERABLE("Failed to create readback buffer");
        return result;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = resource;
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLocation.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = readbackBuffer;
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLocation.PlacedFootprint = layout;
    commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
    hr = commandList->Close();
    if (FAILED(hr))
    {
        ERROR_RECOVERABLE("Failed to close command list");
        readbackBuffer->Release();
        return result;
    }
    ID3D12CommandList* cmdLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, cmdLists);
    WaitForPreviousFrame();
    
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, totalSize };
    hr = readbackBuffer->Map(0, &readRange, &mappedData);

    if (SUCCEEDED(hr) && mappedData)
    {
        int res = sdf->GetResolution();
        result.resize(res * res * res);

        const uint8_t* srcData = static_cast<const uint8_t*>(mappedData);
        for (int z = 0; z < res; z++)
        {
            for (int y = 0; y < res; y++)
            {
                size_t srcOffset = z * layout.Footprint.Depth * layout.Footprint.RowPitch +
                                  y * layout.Footprint.RowPitch;
                size_t dstOffset = (z * res * res + y * res) * sizeof(float);
                
                memcpy(
                    reinterpret_cast<uint8_t*>(result.data()) + dstOffset,
                    srcData + srcOffset,
                    res * sizeof(float)
                );
            }
        }
        readbackBuffer->Unmap(0, nullptr);
        DebuggerPrintf("[DX12Renderer] Read back %zu floats from SDF3D\n", result.size());
    }
    else
    {
        ERROR_RECOVERABLE("Failed to map readback buffer");
    }
    readbackBuffer->Release();

	HRESULT resetHr = m_commandAllocator[m_frameIndex]->Reset();
	if (SUCCEEDED(resetHr))
	{
		resetHr = m_commandList->Reset(m_commandAllocator[m_frameIndex], m_pipelineStateObject);
		if (FAILED(resetHr))
		{
			ERROR_RECOVERABLE("Failed to reset CommandList after SDF readback");
		}
	}
	else
	{
		ERROR_RECOVERABLE("Failed to reset CommandAllocator after SDF readback");
	}
	
    return result;
}

void DX12Renderer::CreateShadowMapResources()
{
	D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = SHADOW_MAP_SIZE;
    desc.Height = SHADOW_MAP_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;  // 同时支持 DSV 和 SRV
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    
    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        //D3D12_RESOURCE_STATE_DEPTH_WRITE, //最好初始状态是srv
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowMapTexture)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create shadow map texture!");
    m_shadowMapTexture->SetName(L"ShadowMapTexture");
	//dsv creating
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	UINT dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	// 第 2 个槽位（索引 1）
	CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(
		m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		1,  // 索引 1（第 2 个）
		dsvDescriptorSize
	);
	m_device->CreateDepthStencilView(m_shadowMapTexture, &dsvDesc, shadowDsvHandle);
	m_shadowDsvHandle = shadowDsvHandle;
    // 创建 SRV（用于后续采样）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        SHADOW_MAP_SRV,
        m_scuDescriptorSize
    );
    m_device->CreateShaderResourceView(m_shadowMapTexture, &srvDesc, srvHandle);
	//PSO
	std::string shaderSource;
    FileReadToString(shaderSource, "Data/Shaders/Shadow.hlsl");
    ID3DBlob* vs = nullptr;
    CompileShaderToByteCode(&vs, "ShadowVS", shaderSource.c_str(), "VertexMain", "vs_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // 其他属性不需要，但为了兼容VBO，保留完整 layout
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_graphicsRootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { nullptr, 0 };  // 无 PS！
    
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    // Use FRONT culling - renders back faces to shadow map
    // This prevents light leaking through thin geometry (necks, edges)
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    // 硬件 depth bias - 比 shader bias 更稳定，不会闪烁
    psoDesc.RasterizerState.DepthBias = 5000;           // 固定偏移
    psoDesc.RasterizerState.DepthBiasClamp = 0.005f;    // 最大偏移限制
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f; // 斜率缩放偏移
    
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;  // 无 RTV！
    psoDesc.SampleDesc.Count = 1;
	hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shadowMapPSO));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Shadow Map PSO!");
    
    m_shadowMapPSO->SetName(L"ShadowMapPSO");
    vs->Release();

	//TransitionResource(m_shadowMapTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE
	//	, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DX12Renderer::RenderingShadowMapPass()
{
	Vec3 sunDir = m_giSystem->m_scene->m_sunDirection.GetNormalized();
    AABB3 sceneBounds = m_giSystem->m_scene->m_sceneBounds;
    Vec3 sceneCenter = (sceneBounds.m_maxs + sceneBounds.m_mins) * 0.5f;
    float sceneRadius = sceneBounds.GetBoundsSize().GetLength() * 0.5f;

    if (sceneRadius < 1.0f) sceneRadius = 100.0f;

    Vec3 lightPos = sceneCenter - sunDir * sceneRadius * 2.0f;

    Vec3 forward = (sceneCenter - lightPos).GetNormalized();
    float yaw = Atan2Degrees(forward.y, forward.x);
    float pitch = Atan2Degrees(-forward.z, sqrtf(forward.x*forward.x + forward.y*forward.y));
    EulerAngles orientation(yaw, pitch, 0.0f);

    Mat44 worldToCamera = orientation.GetAsMatrix_IFwd_JLeft_KUp();
    worldToCamera.SetTranslation3D(lightPos);
    worldToCamera = worldToCamera.GetOrthonormalInverse();

    Mat44 cameraToRender;
    cameraToRender.SetIJK3D(Vec3(0.f, 0.f, 1.f), Vec3(-1.f, 0.f, 0.f), Vec3(0.f, 1.f, 0.f));

    Mat44 renderToClip = Mat44::MakeOrthoProjection(
        -sceneRadius, sceneRadius,
        -sceneRadius, sceneRadius,
        0.1f, sceneRadius * 4.0f
    );

    // ============ Shadow Map Stabilization ============
    // 把shadow map原点snap到texel网格，防止相机移动时抖动
    {
        Mat44 shadowMatrix = renderToClip * cameraToRender * worldToCamera;

        // NDC空间中一个texel的大小（NDC范围是-1到1，总共2.0）
        float texelSizeNDC = 2.0f / (float)SHADOW_MAP_SIZE;

        // 把原点变换到shadow space (NDC)
        Vec4 origin = shadowMatrix.TransformHomogeneous3D(Vec4(0.f, 0.f, 0.f, 1.f));

        // 透视除法（对于正交投影w应该是1，但保险起见还是做一下）
        if (origin.w != 0.f)
        {
            origin.x /= origin.w;
            origin.y /= origin.w;
        }

        // Snap到texel边界（在NDC空间）
        float snappedX = floorf(origin.x / texelSizeNDC) * texelSizeNDC;
        float snappedY = floorf(origin.y / texelSizeNDC) * texelSizeNDC;

        // 计算偏移量（NDC空间）
        float offsetX = snappedX - origin.x;
        float offsetY = snappedY - origin.y;

        // 应用偏移到投影矩阵
        Mat44 snapOffset;
        snapOffset.SetTranslation3D(Vec3(offsetX, offsetY, 0.f));
        renderToClip = snapOffset * renderToClip;
    }
    // ==================================================

	m_cachedLightWorldToCamera = worldToCamera;
	m_cachedLightCameraToRender = cameraToRender;
	m_cachedLightRenderToClip = renderToClip;
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMapTexture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &m_shadowDsvHandle);
    m_commandList->ClearDepthStencilView(
        m_shadowDsvHandle, 
        D3D12_CLEAR_FLAG_DEPTH, 
        1.0f, 0, 
        0, nullptr
    );

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (float)SHADOW_MAP_SIZE;
    viewport.Height = (float)SHADOW_MAP_SIZE;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = (LONG)SHADOW_MAP_SIZE;
    scissor.bottom = (LONG)SHADOW_MAP_SIZE;
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    m_commandList->SetPipelineState(m_shadowMapPSO);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ShadowConstants shadowConsts = {};
	shadowConsts.LightWorldToCamera = worldToCamera;   
	shadowConsts.LightCameraToRender = cameraToRender;
	shadowConsts.LightRenderToClip = renderToClip;
    shadowConsts.ShadowMapSize = (float)SHADOW_MAP_SIZE;
    shadowConsts.ShadowBias = 0.05f;  // Increased from 0.002
	shadowConsts.SoftnessFactor = 1.0f;
    shadowConsts.LightSize = 3.f;
    m_constantBuffers[k_shadowConstantsSlot]->AppendData(&shadowConsts, sizeof(ShadowConstants), 0);
    BindConstantBuffer(k_shadowConstantsSlot, m_constantBuffers[k_shadowConstantsSlot]);

    const std::vector<MeshObject*>& allObjects = m_giSystem->m_scene->m_meshObjects;
    for (MeshObject* meshObj : allObjects)
    {
        if (!meshObj) continue;
    	ModelConstants modelConstants;
    	modelConstants.ModelToWorldTransform = meshObj->m_cachedWorldMatrix;
    	meshObj->m_color.GetAsFloats(modelConstants.ModelColor);
    	m_constantBuffers[k_modelConstantsSlot]->AppendData(&modelConstants, sizeof(ModelConstants), m_currentDrawIndex); 
    	BindConstantBuffer(k_modelConstantsSlot, m_constantBuffers[k_modelConstantsSlot]); //改 TODO
        
        StaticMesh* mesh = meshObj->GetMesh();
    	BindVertexBuffer(mesh->m_vertexBuffer);
    	BindIndexBuffer(mesh->m_indexBuffer);
        m_commandList->DrawIndexedInstanced((UINT)mesh->m_indices.size(), 1, 0, 0, 0);
        m_currentDrawIndex++;
    }
    // 3.8 转换回 SRV 状态供后续采样
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMapTexture,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    m_commandList->ResourceBarrier(1, &barrier);
}

void DX12Renderer::ShutdownShadowMapPass()
{
	DX_SAFE_RELEASE(m_shadowMapTexture)
	DX_SAFE_RELEASE(m_shadowMapPSO)
}

void DX12Renderer::CreateGlobalSDFPassResources()
{
	const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = m_scuDescriptorSize;
	//CreateGlobalSDFTexture
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	texDesc.Width = GLOBAL_SDF_RESOLUTION;
	texDesc.Height = GLOBAL_SDF_RESOLUTION;
	texDesc.DepthOrArraySize = GLOBAL_SDF_RESOLUTION;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32_FLOAT;  // R = 距离, G = 实例索引
	texDesc.SampleDesc.Count = 1;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HRESULT hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_globalSDFTexture)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Global SDF texture!");
	m_globalSDFTexture->SetName(L"VoxelScene_GlobalSDF");
	
	// CreateVoxelVisibilityBuffer: 6 方向 × 每个 Voxel
	uint32_t totalVoxels = GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION;
	//uint32_t bufferSize = sizeof(VoxelVisibilityGPU) * totalVoxels * 3;
	uint32_t bufferSize = sizeof(uint32_t) * totalVoxels * 3; //Typed buffer
	//D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
		bufferSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_voxelVisibilityBuffer)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Voxel Visibility buffer!");
	m_voxelVisibilityBuffer->SetName(L"VoxelScene_VoxelVisibility");
	
	//CreateVoxelLightingTexture
	D3D12_RESOURCE_DESC voxelLightingTexDesc = {};
	voxelLightingTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	voxelLightingTexDesc.Width = GLOBAL_SDF_RESOLUTION;
	voxelLightingTexDesc.Height = GLOBAL_SDF_RESOLUTION;
	voxelLightingTexDesc.DepthOrArraySize = GLOBAL_SDF_RESOLUTION;
	voxelLightingTexDesc.MipLevels = 1;
	voxelLightingTexDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	voxelLightingTexDesc.SampleDesc.Count = 1;
	voxelLightingTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	//heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&voxelLightingTexDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_voxelLightingTexture)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Voxel Lighting texture!");
	m_voxelLightingTexture->SetName(L"VoxelScene_VoxelLighting");

	//CreateInstanceInfoBuffer
	uint32_t meshSDFBufferSize = sizeof(MeshSDFInfoGPU) * MAX_INSTANCES;
	D3D12_RESOURCE_DESC meshSDFBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(meshSDFBufferSize);
	hr = m_device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&meshSDFBufferDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_instanceInfoBuffer)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Instance Info buffer!");
	m_instanceInfoBuffer->SetName(L"VoxelScene_InstanceInfo");
    
	// Upload Buffer
	D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	hr = m_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&meshSDFBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_instanceInfoUploadBuffer)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "[VoxelScene] Failed to create Instance Info upload buffer!");
	m_instanceInfoUploadBuffer->SetName(L"VoxelScene_InstanceInfoUpload");

	D3D12_UNORDERED_ACCESS_VIEW_DESC globalSDFUavDesc = {};
    globalSDFUavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;  // float2
    globalSDFUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    globalSDFUavDesc.Texture3D.MipSlice = 0;
    globalSDFUavDesc.Texture3D.FirstWSlice = 0;
    globalSDFUavDesc.Texture3D.WSize = GLOBAL_SDF_RESOLUTION;
    
    m_device->CreateUnorderedAccessView(
        m_globalSDFTexture,
        nullptr,
        &globalSDFUavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, GLOBAL_SDF_UAV, inc));
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC lightingUavDesc = {};
    lightingUavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // float4
    lightingUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    lightingUavDesc.Texture3D.MipSlice = 0;
    lightingUavDesc.Texture3D.FirstWSlice = 0;
    lightingUavDesc.Texture3D.WSize = GLOBAL_SDF_RESOLUTION; 
    m_device->CreateUnorderedAccessView(
        m_voxelLightingTexture,
        nullptr,
        &lightingUavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, VOXEL_LIGHTING_UAV, inc));
	
	D3D12_UNORDERED_ACCESS_VIEW_DESC visibilityUavDesc = {};
	visibilityUavDesc.Format = DXGI_FORMAT_R32_UINT;
	visibilityUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	visibilityUavDesc.Buffer.FirstElement = 0;
	visibilityUavDesc.Buffer.NumElements = 
		GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION * 3;
	//visibilityUavDesc.Buffer.StructureByteStride = sizeof(VoxelVisibilityGPU);
	visibilityUavDesc.Buffer.StructureByteStride = 0;  // ← Typed buffer无stride
	visibilityUavDesc.Buffer.CounterOffsetInBytes = 0;
	visibilityUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	m_device->CreateUnorderedAccessView(
		m_voxelVisibilityBuffer,
		nullptr,
		&visibilityUavDesc,
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, VISIBILITY_UAV, inc));
    
    // SRV Desc
    D3D12_SHADER_RESOURCE_VIEW_DESC globalSDFSrvDesc = {};
    globalSDFSrvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
    globalSDFSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    globalSDFSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    globalSDFSrvDesc.Texture3D.MipLevels = 1;
    globalSDFSrvDesc.Texture3D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(
        m_globalSDFTexture,
        &globalSDFSrvDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, GLOBAL_SDF_SRV, inc));
	
	D3D12_SHADER_RESOURCE_VIEW_DESC voxelLightingSrvDesc = {};
	voxelLightingSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  
	voxelLightingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	voxelLightingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	voxelLightingSrvDesc.Texture3D.MipLevels = 1;
	voxelLightingSrvDesc.Texture3D.MostDetailedMip = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE voxelLightingSrvHandle = cpuStart;
	voxelLightingSrvHandle.ptr += VOXEL_LIGHTING_SRV * m_scuDescriptorSize;

	m_device->CreateShaderResourceView(
		m_voxelLightingTexture,  // 你的 VoxelLighting 资源
		&voxelLightingSrvDesc,
		voxelLightingSrvHandle
	);
    D3D12_SHADER_RESOURCE_VIEW_DESC visibilitySrvDesc = {};
    visibilitySrvDesc.Format = DXGI_FORMAT_R32_UINT;
    visibilitySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    visibilitySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    visibilitySrvDesc.Buffer.FirstElement = 0;
    visibilitySrvDesc.Buffer.NumElements =
        GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION * GLOBAL_SDF_RESOLUTION * 3;
    visibilitySrvDesc.Buffer.StructureByteStride = 0;
    m_device->CreateShaderResourceView(
        m_voxelVisibilityBuffer,
        &visibilitySrvDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, VISIBILITY_SRV, inc));

	D3D12_SHADER_RESOURCE_VIEW_DESC atlasSrvDesc = {};
	atlasSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	atlasSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;  // 改这里！
	atlasSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	atlasSrvDesc.Texture2DArray.MipLevels = 1;
	atlasSrvDesc.Texture2DArray.MostDetailedMip = 0;
	atlasSrvDesc.Texture2DArray.FirstArraySlice = 0;
	atlasSrvDesc.Texture2DArray.ArraySize = SURFACE_CACHE_LAYER_COUNT; 
	atlasSrvDesc.Texture2DArray.PlaneSlice = 0;
    m_device->CreateShaderResourceView(
        m_surfaceCache.m_atlasTexture,
        &atlasSrvDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, SURFACE_ATLAS_SRV, inc));
	D3D12_SHADER_RESOURCE_VIEW_DESC instanceInfoSrvDesc = {};
	instanceInfoSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instanceInfoSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instanceInfoSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instanceInfoSrvDesc.Buffer.FirstElement = 0;
	instanceInfoSrvDesc.Buffer.NumElements = MAX_INSTANCES;
	instanceInfoSrvDesc.Buffer.StructureByteStride = sizeof(MeshSDFInfoGPU);
	m_device->CreateShaderResourceView(
		m_instanceInfoBuffer,
		&instanceInfoSrvDesc,
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, INSTANCE_INFO_SRV, inc));

	D3D12_SHADER_RESOURCE_VIEW_DESC cardMetaSrvDesc = {};
	cardMetaSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	cardMetaSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	cardMetaSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	cardMetaSrvDesc.Buffer.FirstElement = 0;
	cardMetaSrvDesc.Buffer.NumElements = 64*64;  // TODO：其实应该从cache获取
	cardMetaSrvDesc.Buffer.StructureByteStride = sizeof(SurfaceCardMetadata);
	m_device->CreateShaderResourceView(
		m_surfaceCache.m_cardMetadataBuffer,
		&cardMetaSrvDesc,
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, CARD_METADATA_SRV, inc));

	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
	nullSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
	nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullSrvDesc.Texture3D.MipLevels = 1;
	for (UINT i = 0; i < MAX_SDF_TEXTURE_COUNT; ++i)
	{
		m_device->CreateShaderResourceView(nullptr, &nullSrvDesc, 
			CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, (UINT)SDF_TEXTURE_SRV_BASE + i, inc));
	}
	
	CD3DX12_ROOT_PARAMETER1 rootParams[5]; 
    CD3DX12_DESCRIPTOR_RANGE1 ranges[6];
    // [0] Constants (b0)
    rootParams[0].InitAsConstants(sizeof(VoxelSceneConstants)/4, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    // [1] UAV Table (u0-u2): GlobalSDF, VoxelLighting, Visibility
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
    rootParams[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    // [2] SRV Table (t0-t3, space0): InstanceInfo, SurfaceAtlas, CardMetadata, Visibility
    //     ↑ 移除GlobalSDF，从t1开始！
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0,  // t1: InstanceInfo
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0,  // t2: SurfaceAtlas
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 1);
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0,  // t3: CardMetadata
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 2);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0,  // t4: Visibility
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 3);
    rootParams[2].InitAsDescriptorTable(4, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    // [3] GlobalSDF SRV (t0, space0) - 单独的Table，只在Pass 2/3用
    ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,  // t0
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
    rootParams[3].InitAsDescriptorTable(1, &ranges[5], D3D12_SHADER_VISIBILITY_ALL);
    // [4] Bindless SDF Textures (t0, space1)
    CD3DX12_DESCRIPTOR_RANGE1 bindlessRange;
    bindlessRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_SDF_TEXTURE_COUNT, 0, 1,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
    rootParams[4].InitAsDescriptorTable(1, &bindlessRange, D3D12_SHADER_VISIBILITY_ALL);
    
	// Static Sampler
	CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1];
	staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
	rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 1, staticSamplers,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ID3DBlob* serialized = nullptr;
    ID3DBlob* error = nullptr;
    hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &serialized, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[VoxelScene] Root signature serialization error: %s\n",
                          (char*)error->GetBufferPointer());
            error->Release();
        }
        ERROR_AND_DIE("Failed to serialize VoxelScene root signature!");
		}
		hr = m_device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&m_globalSDFRootSignature)
    );
    serialized->Release();
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create VoxelScene root signature!");
    m_globalSDFRootSignature->SetName(L"VoxelScene_RootSignature");
	//Create PSOs
    {
		std::string buildGlobalSDFSource;
		int result = FileReadToString(buildGlobalSDFSource,"Data/Shaders/BuildGlobalSDF.hlsl" );
		if (result < 0)
		{
			DebuggerPrintf("[VoxelScene] Failed to load BuildGlobalSDF.hlsl\n");
		}
		ID3DBlob* csBlob = nullptr;
		bool success = CompileShaderToByteCode(&csBlob, "BuildGlobalSDF", buildGlobalSDFSource.c_str(),
			"CSMain", "cs_5_1");
		if (!success || !csBlob)
			DebuggerPrintf("[DX12Renderer] Failed to compile BuildGlobalSDF.hlsl\n");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_globalSDFRootSignature;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
        
        hr = m_device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(&m_buildGlobalSDFPSO)
        );
        csBlob->Release();
        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create BuildGlobalSDF PSO!");
        m_buildGlobalSDFPSO->SetName(L"VoxelScene_BuildGlobalSDF_PSO");
    }
    {
		std::string buildVoxelVisibilitySource;
		int result = FileReadToString(buildVoxelVisibilitySource,"Data/Shaders/BuildVoxelVisibility.hlsl" );
		if (result < 0)
		{
			DebuggerPrintf("[VoxelScene] Failed to load BuildVoxelVisibility.hlsl\n");
		}
		ID3DBlob* csBlob = nullptr;
		bool success = CompileShaderToByteCode(&csBlob, "BuildVoxelVisibility", buildVoxelVisibilitySource.c_str(),
			"CSMain", "cs_5_1");
		if (!success || !csBlob)
			DebuggerPrintf("[DX12Renderer] Failed to compile BuildVoxelVisibility.hlsl\n");
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_globalSDFRootSignature;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
		
		hr = m_device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(&m_buildVisibilityPSO)
        );
        csBlob->Release();
        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create BuildVisibility PSO!");
        m_buildVisibilityPSO->SetName(L"VoxelScene_BuildVisibility_PSO");
    }
    {
		std::string injectLightingSource;
		int result = FileReadToString(injectLightingSource,"Data/Shaders/InjectVoxelLighting.hlsl" );
		if (result < 0)
		{
			DebuggerPrintf("[VoxelScene] Failed to load InjectVoxelLighting.hlsl\n");
		}
        ID3DBlob* csBlob = nullptr;
		bool success = CompileShaderToByteCode(&csBlob, "InjectVoxelLighting", injectLightingSource.c_str(),
			"CSMain", "cs_5_1");
		if (!success || !csBlob)
			DebuggerPrintf("[DX12Renderer] Failed to compile InjectVoxelLighting.hlsl\n");
		
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_globalSDFRootSignature;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
        hr = m_device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(&m_injectLightingPSO)
        );
        csBlob->Release();
        GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create InjectLighting PSO!");
        m_injectLightingPSO->SetName(L"VoxelScene_InjectLighting_PSO");
    }
    DebuggerPrintf("[VoxelScene] All PSOs created\n");
}

void DX12Renderer::RenderingGlobalSDFPass()
{
	VoxelSceneConstants constants;
	constants.SceneBoundsMax = m_giSystem->m_scene->m_sceneBounds.m_maxs;
	constants.SceneBoundsMin = m_giSystem->m_scene->m_sceneBounds.m_mins;
	constants.VoxelResolution = GLOBAL_SDF_RESOLUTION;
	Vec3 sceneSize = m_giSystem->m_scene->m_sceneBounds.GetBoundsSize();
	constants.VoxelSize.x = sceneSize.x / (float)GLOBAL_SDF_RESOLUTION;
	constants.VoxelSize.y = sceneSize.y / (float)GLOBAL_SDF_RESOLUTION;
	constants.VoxelSize.z = sceneSize.z / (float)GLOBAL_SDF_RESOLUTION;
	constants.MaxTraceSteps = 64;
	constants.InstanceCount = (uint32_t)m_giSystem->m_scene->m_meshObjects.size();
	constants.SDFThreshold = constants.VoxelSize.x * 0.5f;
	constants.CardCount = m_giSystem->m_scene->m_nextCardID;
	constants.MaxTraceDistance = sceneSize.GetLength();
	constants.AtlasWidth = (float)m_giSystem->m_config.m_primaryAtlasSize;
	constants.AtlasHeight = (float)m_giSystem->m_config.m_primaryAtlasSize;
	
	D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
	UINT inc = m_scuDescriptorSize;
	constexpr uint32_t NUMTHREADS = 8;
	uint32_t groups = (constants.VoxelResolution + NUMTHREADS - 1) / NUMTHREADS;

	m_commandList->SetDescriptorHeaps(1, &m_cbvSrvDescHeap);
	m_commandList->SetComputeRootSignature(m_globalSDFRootSignature);
	
	if (m_giSystem->m_scene->m_needsRebuildGlobalLighting)
	{
		WaitForComputeQueue(); //确认一开始注册的meshSDF都生成了
		DebuggerPrintf("[GlobalSDF] All mesh SDFs ready\n");
		//UploadInstanceInfos(gpu)
		std::vector<MeshSDFInfoGPU> meshInfos = m_giSystem->m_scene->m_meshInfos;
		void* mappedData = nullptr;
		HRESULT hr = m_instanceInfoUploadBuffer->Map(0, nullptr, &mappedData);
		if (SUCCEEDED(hr))
		{
			memcpy(mappedData, meshInfos.data(), sizeof(MeshSDFInfoGPU) * meshInfos.size());
			m_instanceInfoUploadBuffer->Unmap(0, nullptr);
		}
		TransitionResource(m_instanceInfoBuffer,D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		m_commandList->CopyBufferRegion(
		   m_instanceInfoBuffer, 0,
		   m_instanceInfoUploadBuffer, 0,
		   sizeof(MeshSDFInfoGPU) * meshInfos.size()
		);
		TransitionResource(m_instanceInfoBuffer,D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		DebuggerPrintf("[GlobalSDF] Uploaded %zu instance infos\n", meshInfos.size());
	
    	TransitionResource(m_globalSDFTexture, 
    	    D3D12_RESOURCE_STATE_COMMON, 
    	    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    	TransitionResource(m_voxelLightingTexture,
    	    D3D12_RESOURCE_STATE_COMMON,
    	    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    	TransitionResource(m_voxelVisibilityBuffer,
    	    D3D12_RESOURCE_STATE_COMMON,
    	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    	TransitionResource(m_surfaceCache.m_cardMetadataBuffer,
    	    D3D12_RESOURCE_STATE_COMMON,
    	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    	TransitionResource(m_surfaceCache.m_atlasTexture,
			//D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    	
    	// Pass 1: BuildGlobalSDF
    	m_commandList->SetPipelineState(m_buildGlobalSDFPSO);
    	m_commandList->SetComputeRoot32BitConstants(0, sizeof(VoxelSceneConstants)/4, &constants, 0);
    	// [1] UAV Table
    	m_commandList->SetComputeRootDescriptorTable(1,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, GLOBAL_SDF_UAV, inc));
    	// [2] SRV Table (不包含GlobalSDF，只有InstanceInfo等)
    	m_commandList->SetComputeRootDescriptorTable(2,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, INSTANCE_INFO_SRV, inc));  // 从385开始
    	// [3] GlobalSDF SRV Table - Pass 1不用，不绑定或绑定到null
    	// 可以不调用SetComputeRootDescriptorTable(3, ...)
    	// [4] Bindless
    	m_commandList->SetComputeRootDescriptorTable(4,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, SDF_TEXTURE_SRV_BASE, inc));
    	m_commandList->Dispatch(groups, groups, groups);
    	
    	// Pass 2: BuildVoxelVisibility
    	TransitionResource(m_globalSDFTexture,
    	    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    	TransitionResource(m_voxelVisibilityBuffer,
    	    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    	    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    	m_commandList->SetPipelineState(m_buildVisibilityPSO);
    	m_commandList->SetComputeRoot32BitConstants(0, (UINT)sizeof(VoxelSceneConstants)/4, &constants, 0);
    	// [1] UAV Table
    	m_commandList->SetComputeRootDescriptorTable(1,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, VISIBILITY_UAV, inc));
    	// [2] SRV Table
    	m_commandList->SetComputeRootDescriptorTable(2,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, INSTANCE_INFO_SRV, inc));
    	// [3] GlobalSDF SRV Table 
    	m_commandList->SetComputeRootDescriptorTable(3,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, GLOBAL_SDF_SRV, inc));  // 382
    	// [4] Bindless
    	m_commandList->SetComputeRootDescriptorTable(4,
    	    CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, SDF_TEXTURE_SRV_BASE, inc));
    	m_commandList->Dispatch(groups, groups, groups);

		//turn back
		TransitionResource(m_voxelVisibilityBuffer,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON);
		TransitionResource(m_globalSDFTexture, 
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
			D3D12_RESOURCE_STATE_COMMON);
		TransitionResource(m_surfaceCache.m_atlasTexture,
			//D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);
		TransitionResource(m_surfaceCache.m_cardMetadataBuffer,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COMMON);
		TransitionResource(m_voxelLightingTexture,
	D3D12_RESOURCE_STATE_UNORDERED_ACCESS,  
	D3D12_RESOURCE_STATE_COMMON);

		m_giSystem->m_scene->m_needsRebuildGlobalLighting = false;
		DebuggerPrintf("[GlobalSDF] Global SDF pass completed {including Phase 1&2&3}.\n");
	}
    // Pass 3: InjectVoxelLighting
    TransitionResource(m_voxelVisibilityBuffer,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	TransitionResource(m_voxelLightingTexture,
	D3D12_RESOURCE_STATE_COMMON,
	D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionResource(m_globalSDFTexture,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	TransitionResource(m_surfaceCache.m_cardMetadataBuffer,
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	TransitionResource(m_surfaceCache.m_atlasTexture,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);  //TODO: srv和nonsrv是一个状态，似乎不会冲突
    m_commandList->SetPipelineState(m_injectLightingPSO);
    m_commandList->SetComputeRoot32BitConstants(0, sizeof(VoxelSceneConstants)/4, &constants, 0);
    // [1] UAV Table
    m_commandList->SetComputeRootDescriptorTable(1,
        CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, GLOBAL_SDF_UAV, inc));
    // [2] SRV Table
    m_commandList->SetComputeRootDescriptorTable(2,
        CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, INSTANCE_INFO_SRV, inc));
    // [3] GlobalSDF SRV Table
    m_commandList->SetComputeRootDescriptorTable(3,
        CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, GLOBAL_SDF_SRV, inc));
    // [4] Bindless
    m_commandList->SetComputeRootDescriptorTable(4,
        CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, SDF_TEXTURE_SRV_BASE, inc));
    m_commandList->Dispatch(groups, groups, groups);
    
    TransitionResource(m_voxelLightingTexture,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COMMON);
    TransitionResource(m_globalSDFTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    TransitionResource(m_voxelVisibilityBuffer,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    TransitionResource(m_surfaceCache.m_atlasTexture,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    TransitionResource(m_surfaceCache.m_cardMetadataBuffer,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
}

void DX12Renderer::CreateGraphicsRootSignature()
{
	HRESULT hr;
	// Create a root signature. 
	{
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

		//共 15 个 Root Parameter（14个CBV + 1个SRV Descriptor Table） ->Thesis extension: 32 parameters
		CD3DX12_ROOT_PARAMETER rootParameters[32] = {};

		// [0~13] = Root CBVs -> b0 ~ b13
		for (UINT i = 0; i < 14; ++i)
		{
			rootParameters[i].InitAsConstantBufferView(i, 0, D3D12_SHADER_VISIBILITY_ALL); // register(b#), space 0
		}
		// [14] = SRV Descriptor Table for t0~
		CD3DX12_DESCRIPTOR_RANGE textureSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_TEXTURE_COUNT, 0); // up to t0~t199
		rootParameters[14].InitAsDescriptorTable(1, &textureSRV, D3D12_SHADER_VISIBILITY_PIXEL);
		// [15] = GBuffer SRVs
		CD3DX12_DESCRIPTOR_RANGE gbufferSRVs(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBUFFER_COUNT + DEPTH_SRV_COUNT, GBUFFER_SRV_START); 
		rootParameters[15].InitAsDescriptorTable(1, &gbufferSRVs, D3D12_SHADER_VISIBILITY_ALL);
		// [16] = Surface Cache SRVs
		static CD3DX12_DESCRIPTOR_RANGE surfaceCacheSRVs;
		surfaceCacheSRVs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		                        (DESCRIPTORS_PER_SURFACE_BUFFER/2), SURFACE_CACHE_SRV, 0);
		rootParameters[16].InitAsDescriptorTable(1, &surfaceCacheSRVs, D3D12_SHADER_VISIBILITY_ALL);

		// [17] = Surface Cache UAVs (u0 + u1)
		static CD3DX12_DESCRIPTOR_RANGE surfaceCacheUAVs;
		surfaceCacheUAVs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		                       (DESCRIPTORS_PER_SURFACE_BUFFER/2), 0, 0);  // u0, u1
		rootParameters[17].InitAsDescriptorTable(1, &surfaceCacheUAVs, D3D12_SHADER_VISIBILITY_ALL);
		// u0, u1, u2, u3, u4, u5（如果包含 Buffer 1 的 UAVs） 那就是6 ->应该是不可能包含的
		
		// [18-19] = Radiance Cache 
		rootParameters[18].InitAsShaderResourceView(RADIANCE_CACHE_SRV, 0); 
		rootParameters[19].InitAsUnorderedAccessView(5, 0);  // u5
		// [20-21] = Probe Grid 
		rootParameters[20].InitAsShaderResourceView(PROBE_GRID_SRV_INDEX, 0); 
		rootParameters[21].InitAsConstantBufferView(16, 0);  // b16 - Probe settings TODO:改
		// [22-23] = SSR
		rootParameters[22].InitAsShaderResourceView(SSR_SRV_INDEX, 0); 
		rootParameters[23].InitAsConstantBufferView(17, 0);  // b17 - SSR settings TODO:改
		//[24-25] = Temporal
		rootParameters[24].InitAsShaderResourceView(TEMPORAL_PREV_SRV_INDEX, 0); 
		rootParameters[25].InitAsShaderResourceView(TEMPORAL_MOTION_SRV_INDEX, 0);
		//[26]
		CD3DX12_DESCRIPTOR_RANGE shadowMapRange;
		shadowMapRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, SHADOW_MAP_SRV_INDEX, 0);  // t240
		rootParameters[26].InitAsDescriptorTable(1, &shadowMapRange, D3D12_SHADER_VISIBILITY_PIXEL);
 
		//[27] Screen Indirect Lighting SRV
		static CD3DX12_DESCRIPTOR_RANGE screenIndirectRange;
		screenIndirectRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, SCREEN_INDIRECT_LIGHTING_SRV_REGISTER, 0);  // t241
		rootParameters[27].InitAsDescriptorTable(1, &screenIndirectRange, D3D12_SHADER_VISIBILITY_PIXEL);
		// [28-31] = reserved
		for (UINT i = 28; i < 32; ++i)
		{
			rootParameters[i].InitAsConstantBufferView(18 + (i-27), 0);
		}
        
		D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
		// Sampler 0: Point Sampler (s0) - 用于 GBuffer、精确采样
		samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // 改为 CLAMP，避免边界问题
		samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].MipLODBias = 0;
		samplers[0].MaxAnisotropy = 0;
		samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplers[0].MinLOD = 0.0f;
		samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[0].ShaderRegister = 0;  // s0
		samplers[0].RegisterSpace = 0;
		samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
		// Sampler 1: Linear Sampler (s1) - 用于 Surface Cache、Radiance Cache
		samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].MipLODBias = 0;
		samplers[1].MaxAnisotropy = 0;
		samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		samplers[1].MinLOD = 0.0f;
		samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[1].ShaderRegister = 1;  
		samplers[1].RegisterSpace = 0;
		samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		// Sampler 2: Shadow Comparison Sampler (s2) - 用于PCF阴影
		samplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplers[2].MipLODBias = 0;
		samplers[2].MaxAnisotropy = 1;
		samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;  // 边界外=无阴影
		samplers[2].MinLOD = 0.0f;
		samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[2].ShaderRegister = 2;  // s2
		samplers[2].RegisterSpace = 0;
		samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
		CD3DX12_ROOT_SIGNATURE_DESC rsigDesc = {};
		rsigDesc.Init(
			_countof(rootParameters),
			rootParameters,
			3,  // 3 samplers
			samplers, 
			rootSignatureFlags
		);
		
		ID3DBlob* signature;
		ID3DBlob* error;
		hr = D3D12SerializeRootSignature(&rsigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
		if (!SUCCEEDED(hr))
		{
			if (error != NULL)
			{
				DebuggerPrintf((char*)error->GetBufferPointer());
			}
		}
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Serialize Root Signature!");

		hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
		                                   IID_PPV_ARGS(&m_graphicsRootSignature));
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Create Root Signature!");
		m_graphicsRootSignature->SetName(L"OnlyRS");

		if (signature) signature->Release();
		if (error) error->Release();
	}
}

void DX12Renderer::CreateComputeRootSignature() //暂时也没在用了
{
    static CD3DX12_DESCRIPTOR_RANGE descriptorRanges[11];

    // [0] GBuffer SRVs (t0-t4)
    descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBUFFER_COUNT + DEPTH_SRV_COUNT, 0, 0); // t0-t4

    // [1] Surface Cache Atlas UAV (u0-u1) - PRIMARY + GI ->u0, primary
    descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0); // u0-u1->改为只有一种

    // [2] Surface Cache Metadata UAV (u2-u3) ->u1, primary
    descriptorRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0); // u2-u3->改为只有一种

    // [3] Radiance Cache Probe UAV (u4) ->(u2-u3) 
    descriptorRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1 * 2, 0); 

    // [4] Additional UAVs (u5-u6) - 预留
    descriptorRanges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1 * 2 + 1, 0);

    // [5] Previous Surface Cache Atlas SRV (t5) 预留->TODO:应该是没用了
    descriptorRanges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1,
        GBUFFER_COUNT + DEPTH_SRV_COUNT, 0); // t5

    // [6] Card Metadata SRV (t6)
    descriptorRanges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1,
        GBUFFER_COUNT + DEPTH_SRV_COUNT + 1, 0); // t6

    // [7] SDF Volume SRV (t10)
    descriptorRanges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 0); // t10

    // [8] Surface Cache Atlas SRV for Radiance Cache (t8-t9)
    // 这是给 Radiance Cache Update 读取 Surface Cache 用的
    descriptorRanges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0); 

    // [9] Radiance Cache Probe SRV (Previous frame, t7)
    descriptorRanges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7, 0);

    // [10] Card BVH SRVs (t11 - t12)
    descriptorRanges[10].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 11, 0); // t240: BVH Nodes, t241: BVH Indices

    CD3DX12_ROOT_PARAMETER computeRootParameters[15] = {};

    // [0] = GBuffer SRVs descriptor table
    computeRootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_ALL);

    // [1] = Surface Cache Atlas UAV descriptor table
    computeRootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_ALL);

    // [2] = Surface Cache Metadata UAV descriptor table
    computeRootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[2], D3D12_SHADER_VISIBILITY_ALL);

    // [3] = Surface Cache Constants CBV (b0)
    computeRootParameters[3].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [4] = Light Constants CBV (b1)
    computeRootParameters[4].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [5] = Previous Surface Cache SRV (t5)
    computeRootParameters[5].InitAsDescriptorTable(1, &descriptorRanges[5], D3D12_SHADER_VISIBILITY_ALL);

    // [6] = Card Metadata SRV (t6)
    computeRootParameters[6].InitAsDescriptorTable(1, &descriptorRanges[6], D3D12_SHADER_VISIBILITY_ALL);

    // [7] = SDF Volume SRV descriptor table (t10)
    computeRootParameters[7].InitAsDescriptorTable(1, &descriptorRanges[7], D3D12_SHADER_VISIBILITY_ALL);

    // [8] = Additional UAVs descriptor table (u5-u6)
    computeRootParameters[8].InitAsDescriptorTable(1, &descriptorRanges[4], D3D12_SHADER_VISIBILITY_ALL);

    // [9] = Radiance Cache Probe UAV (u4)
    computeRootParameters[9].InitAsDescriptorTable(1, &descriptorRanges[3], D3D12_SHADER_VISIBILITY_ALL);

    // [10] = Surface Cache Atlas SRV for Radiance Cache (t200-t201)
    computeRootParameters[10].InitAsDescriptorTable(1, &descriptorRanges[8], D3D12_SHADER_VISIBILITY_ALL);

    // [11] = Radiance Cache Probe SRV (Previous, t208)
    computeRootParameters[11].InitAsDescriptorTable(1, &descriptorRanges[9], D3D12_SHADER_VISIBILITY_ALL);

    // [12] = Card BVH Nodes SRV + Indices SRV (t240-t241)
    computeRootParameters[12].InitAsDescriptorTable(1, &descriptorRanges[10], D3D12_SHADER_VISIBILITY_ALL);

    // [13] = Radiance Cache Constants CBV (b13)
    computeRootParameters[13].InitAsConstantBufferView(13, 0, D3D12_SHADER_VISIBILITY_ALL);

    // [14] = Surface Cache Metadata SRV for Radiance Cache (t202-t203)
    // 用于 Radiance Cache Update 读取 Card Metadata
    static CD3DX12_DESCRIPTOR_RANGE surfCacheMetaSrvRange;
    surfCacheMetaSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 202, 0); // t202-t203 TODO:显然不对
    computeRootParameters[14].InitAsDescriptorTable(1, &surfCacheMetaSrvRange, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MipLODBias = 0;
    samplers[0].MaxAnisotropy = 0;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Linear sampler (s1) - Surface Cache 采样
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MipLODBias = 0;
    samplers[1].MaxAnisotropy = 0;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_SIGNATURE_DESC computeRootSigDesc = {};
    computeRootSigDesc.Init(
        _countof(computeRootParameters),
        computeRootParameters,
        _countof(samplers),
        samplers,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );
    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &computeRootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error
    );

    if (FAILED(hr))
    {
        if (error != nullptr)
        {
            DebuggerPrintf("Root signature serialization error: %s\n", (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to serialize compute root signature!");
    }

    hr = m_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_computeRootSignature)
    );

    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create compute root signature!");
    m_computeRootSignature->SetName(L"ComputeRootSignature");

    if (signature) signature->Release();
    if (error) error->Release();

    DebuggerPrintf("[Renderer] Created Compute Root Signature with Radiance Cache support\n");
}

void DX12Renderer::CreateSurfaceCacheDescriptorsAndTransitionStates(SurfaceCache* cache)
{
	if (cache == nullptr)
		return;
	const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
	const UINT inc = m_scuDescriptorSize;
	int idxSrvAtlas = 0;
	int idxSrvMeta = 0;
	int idxUavRadiance = 0; 
	int idxUavMeta = 0;
		idxSrvAtlas = PrimaryAtlasSrvIndex(0);
		idxSrvMeta = PrimaryMetaSrvIndex(0);
		idxUavRadiance = PrimaryAtlasUavIndex(0);  
		idxUavMeta = PrimaryMetaUavIndex(0);
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = SURFACE_CACHE_LAYER_COUNT;  // SRV可以访问所有layers

		m_device->CreateShaderResourceView(
			cache->m_atlasTexture,
			&srvDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, idxSrvAtlas, inc)
		);
	}
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		//uavDesc.Texture2DArray.FirstArraySlice = SURFACE_CACHE_LAYER_RADIANCE;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		//uavDesc.Texture2DArray.ArraySize = 1; 
		uavDesc.Texture2DArray.ArraySize = SURFACE_CACHE_LAYER_COUNT;

		m_device->CreateUnorderedAccessView(
			cache->m_atlasTexture,
			nullptr,
			&uavDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, idxUavRadiance, inc)
		);
	}
	// Metadata SRV
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.NumElements = cache->m_maxCards;
		srvDesc.Buffer.StructureByteStride = sizeof(SurfaceCardMetadata);

		m_device->CreateShaderResourceView(
			cache->m_cardMetadataBuffer,
			&srvDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, idxSrvMeta, inc)
		);
	}
	// Metadata UAV ->UAV其实用处不大
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = cache->m_maxCards;
		uavDesc.Buffer.StructureByteStride = sizeof(SurfaceCardMetadata);

		m_device->CreateUnorderedAccessView(
			cache->m_cardMetadataBuffer,
			nullptr,
			&uavDesc,
			CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, idxUavMeta, inc)
		);
	}
	// CD3DX12_RESOURCE_BARRIER barriers[] = {
	// 	CD3DX12_RESOURCE_BARRIER::Transition(
	// 		cache->m_atlasTexture[bufferIndex],
	// 		D3D12_RESOURCE_STATE_COMMON,
	// 		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	// 	),
	// 	CD3DX12_RESOURCE_BARRIER::Transition(
	// 		cache->m_cardMetadataBuffer[bufferIndex],
	// 		D3D12_RESOURCE_STATE_COMMON,
	// 		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	// 	)
	// };
	// m_commandList->ResourceBarrier(2, barriers);
}

void DX12Renderer::RenderingCompositePass()
{
    if (!m_giSystem || !m_gBufferPassActive)
        return;
	
	CD3DX12_RESOURCE_BARRIER preCompositeBarrier = 
		CD3DX12_RESOURCE_BARRIER::Transition(
			m_screenProbeFinalGather->GetIndirectLightingTexture(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
	m_commandList->ResourceBarrier(1, &preCompositeBarrier);
	
    BeginForwardPass();
    if (!m_hasBackBufferCleared)
    {
        ClearForwardPassRTV(Rgba8::BLACK);
    }
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        m_frameIndex,
        m_rtvDescriptorSize
    );
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    
    // CD3DX12_RESOURCE_BARRIER barriers[GBUFFER_COUNT + DEPTH_SRV_COUNT];
    // for (int i = 0; i < GBUFFER_COUNT; ++i)
    // {
    //     barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
    //         m_gBuffer.GetResource(i),
    //         D3D12_RESOURCE_STATE_RENDER_TARGET,
    //         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    //     );
    // }
    // barriers[GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
    //     m_depthStencilBuffer,
    //     D3D12_RESOURCE_STATE_DEPTH_WRITE,
    //     D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    // );
    //m_commandList->ResourceBarrier(GBUFFER_COUNT + DEPTH_SRV_COUNT, barriers);
    
    m_commandList->SetGraphicsRootSignature(m_graphicsRootSignature);
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvDescHeap };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    
    CompositeConstants constants = {};
    constants.ClipToRenderTransform = m_currentCam.RenderToClipTransform.GetInverse();
    constants.RenderToCameraTransform = m_currentCam.CameraToRenderTransform.GetInverse();
    constants.CameraToWorldTransform = m_currentCam.WorldToCameraTransform.GetInverse();
    
    IntVec2 screenDims = m_config.m_window->GetClientDimensions();
    constants.ScreenWidth = (float)screenDims.x;
    constants.ScreenHeight = (float)screenDims.y;
    constants.IndirectIntensity = 1.0f;
    constants.DirectIntensity = 1.0f;
    if (m_giSystem && m_giSystem->m_scene)
    {
    	Rgba8 color = m_giSystem->m_scene->m_sunColor;
    	float sunColorAsFloats[4];
    	color.GetAsFloats(sunColorAsFloats);
    	constants.SunColor[0] = sunColorAsFloats[0];
    	constants.SunColor[1] = sunColorAsFloats[1];
    	constants.SunColor[2] = sunColorAsFloats[2];
    	constants.SunColor[3] = sunColorAsFloats[3];
        constants.SunNormal[0] = m_giSystem->m_scene->m_sunDirection.GetNormalized().x;
        constants.SunNormal[1] = m_giSystem->m_scene->m_sunDirection.GetNormalized().y;
        constants.SunNormal[2] = m_giSystem->m_scene->m_sunDirection.GetNormalized().z;
    }
    else
    {
    	Vec3 normal = Vec3(0.5f, -0.8f, -0.3f).GetNormalized();
    	constants.SunColor[0] = 1.f;
    	constants.SunColor[1] = 1.f;
    	constants.SunColor[2] = 1.f;
    	constants.SunColor[3] = 1.f;
        constants.SunNormal[0] = normal.x;
        constants.SunNormal[1] = normal.y;
        constants.SunNormal[2] = normal.z;
    }
	
    constants.AmbientIntensity = 0.0f;  // 设为0，ambient通过GI间接光来实现
    constants.AmbientColor = Vec3(0.0f, 0.0f, 0.0f);
    constants.ShadowBias = 0.05f;  // Increased from 0.002 - needs to be proportional to scene scale
    
    constants.LightWorldToCamera = m_cachedLightWorldToCamera;
    constants.LightCameraToRender = m_cachedLightCameraToRender;
    constants.LightRenderToClip = m_cachedLightRenderToClip;
    
    constants.ShadowMapSize = (float)SHADOW_MAP_SIZE;
    constants.AOStrength = 0.5f;
	constants.LightSize = 3.f;
	constants.SoftnessFactor = 1.f;
    
    m_constantBuffers[k_compositeConstantsSlot]->AppendData(&constants, sizeof(CompositeConstants), 0);
    
    // [1] Camera Constants (b1)
    D3D12_GPU_VIRTUAL_ADDRESS cameraCBAddress =
        m_constantBuffers[k_cameraConstantsSlot]->m_dx12ConstantBuffer->GetGPUVirtualAddress()
        + m_constantBuffers[k_cameraConstantsSlot]->m_offset;
    m_commandList->SetGraphicsRootConstantBufferView(k_cameraConstantsSlot, cameraCBAddress);
    // [4] GeneralLight Constants (b4) - 点光源/聚光灯
    D3D12_GPU_VIRTUAL_ADDRESS lightCBAddress =
        m_constantBuffers[k_generalLightConstantsSlot]->m_dx12ConstantBuffer->GetGPUVirtualAddress()
        + m_constantBuffers[k_generalLightConstantsSlot]->m_offset;
    m_commandList->SetGraphicsRootConstantBufferView(k_generalLightConstantsSlot, lightCBAddress);
    // [12] Composite Constants (b12)
    D3D12_GPU_VIRTUAL_ADDRESS compositeCBAddress = 
        m_constantBuffers[k_compositeConstantsSlot]->m_dx12ConstantBuffer->GetGPUVirtualAddress()
        + m_constantBuffers[k_compositeConstantsSlot]->m_offset;
    m_commandList->SetGraphicsRootConstantBufferView(k_compositeConstantsSlot, compositeCBAddress);
    // [15] GBuffer SRVs (t200-t204)
    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufferHandle(
        m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
        GBUFFER_SRV_START_SLOT,  // 214
        m_scuDescriptorSize
    );
    m_commandList->SetGraphicsRootDescriptorTable(15, gbufferHandle);
    
    // // [16] Surface Cache SRVs (如果 Shader 需要)
    // BindSurfaceCacheForGraphics(&m_surfaceCache);
    // // [18] Radiance Cache SRV (如果 Shader 需要)
    // BindRadianceCacheForGraphics(&m_radianceCache);
    // [26] Shadow Map SRV
    CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapHandle(
        m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
        SHADOW_MAP_SRV,  // Shadow Map 在描述符堆中的位置
        m_scuDescriptorSize
    );
    m_commandList->SetGraphicsRootDescriptorTable(26, shadowMapHandle);
	// [27] Screen Indirect Lighting SRV
	CD3DX12_GPU_DESCRIPTOR_HANDLE indirectLightHandle(
		m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		SCREEN_INDIRECT_LIGHTING_SRV, //430
		m_scuDescriptorSize
	);
	m_commandList->SetGraphicsRootDescriptorTable(27, indirectLightHandle);
    m_commandList->SetPipelineState(m_compositePSO);
    
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    m_commandList->DrawInstanced(3, 1, 0, 0);
    //DebuggerPrintf("[Composite] Pass completed\n");

	
	RenderingGIVisualizationPass(constants);
}

void DX12Renderer::ShutdownCompositePass()
{
	DX_SAFE_RELEASE(m_compositePSO);
	DX_SAFE_RELEASE(m_graphicsRootSignature);
}

void DX12Renderer::BeginCardCapturePass()
{
	if (m_currentActivePass == ActivePass::CARDCAPTURE)
    {
        DebuggerPrintf("[CardCapture] Warning: Already in Card Capture Pass\n");
        return;
    }
	const std::vector<uint32_t>& dirtyCards = m_giSystem->GetDirtyCards();  
	if (dirtyCards.empty())
		return;
	
	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapHandle(
		m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		SHADOW_MAP_SRV,  // 387
		m_scuDescriptorSize
	);
	m_commandList->SetGraphicsRootDescriptorTable(26, shadowMapHandle);

	m_currentCaptureIndex = 0;
	
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_surfaceCache.GetAtlasTexture(),
		//D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	m_commandList->ResourceBarrier(1, &barrier);
	
    m_currentActivePass = ActivePass::CARDCAPTURE;

	UploadCardMetadataToGPU();
	
	RenderingShadowMapPass();
	
	CaptureDirtySurfaceCards(s_maxCardsPerBatch);
	//CaptureDirtySurfaceCards(1);
}

void DX12Renderer::EndCardCapturePass()
{
	if (m_currentActivePass != ActivePass::CARDCAPTURE)
	{
		return;
	}
	// CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
	// 	m_surfaceCaches[SURFACE_CACHE_TYPE_PRIMARY].GetAtlasTexture(),
	// 	D3D12_RESOURCE_STATE_COPY_DEST,
	// 	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	// );
	// m_commandList->ResourceBarrier(1, &barrier);
	
		// CardMetadata: PIXEL_SHADER_RESOURCE → COMMON
		TransitionResource(
			m_surfaceCache.m_cardMetadataBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON);
		// Atlas Texture: PIXEL_SHADER_RESOURCE → COMMON
		TransitionResource(
			m_surfaceCache.m_atlasTexture,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_COMMON);
	
	DebuggerPrintf("[CardCapture] END CardCapture Pass\n");
	m_currentActivePass = ActivePass::UNKNOWN;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Renderer::GetCardCaptureRTVHandle(int layer)
{
	const int baseIndex = GBUFFER_COUNT + FRAME_BUFFER_COUNT;

	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
		m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		baseIndex + layer,
		m_rtvDescriptorSize);

	return handle;
}

void DX12Renderer::UploadCardMetadataToGPU()
{
	if (m_giSystem->m_cardMetadataCPU.empty())
		return;
	//for (int type = 0; type < SURFACE_CACHE_TYPE_COUNT; type++)
	//{
		SurfaceCache cache = m_surfaceCache;
		if (!cache.m_initialized)
			return;
        
		ID3D12Resource* uploadBuffer = cache.GetMetadataUploadBuffer();
		if (!uploadBuffer)
			return;;
        
		void* mappedData = nullptr;
		HRESULT hr = uploadBuffer->Map(0, nullptr, &mappedData);
        
		if (SUCCEEDED(hr))
		{
			size_t dataSize = m_giSystem->m_cardMetadataCPU.size() * 
							 sizeof(SurfaceCardMetadata);
			memcpy(mappedData, m_giSystem->m_cardMetadataCPU.data(), dataSize);
			uploadBuffer->Unmap(0, nullptr);
            
			ID3D12Resource* defaultBuffer = cache.GetMetadataBuffer();
			if (defaultBuffer)
			{
                TransitionResource(uploadBuffer, 
					D3D12_RESOURCE_STATE_GENERIC_READ, 
					D3D12_RESOURCE_STATE_COPY_SOURCE);
                TransitionResource(defaultBuffer, 
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 
					D3D12_RESOURCE_STATE_COPY_DEST);
                
				m_commandList->CopyResource(defaultBuffer, uploadBuffer);
                
				TransitionResource(defaultBuffer, 
					D3D12_RESOURCE_STATE_COPY_DEST, 
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				TransitionResource(uploadBuffer, 
				D3D12_RESOURCE_STATE_COPY_SOURCE, 
				D3D12_RESOURCE_STATE_GENERIC_READ);//新增的
			}
		}
	//}
}

void DX12Renderer::CaptureDirtySurfaceCards(uint32_t maxCardsPerFrame)
{
	std::vector<uint32_t> cardsToUpdate = m_giSystem->BuildUpdateList(maxCardsPerFrame);

	for (uint32_t cardID : cardsToUpdate)
	{
		SurfaceCard* card = m_giSystem->m_scene->GetSurfaceCardByID(cardID);
		if (!card)
			continue;
                
		MeshObject* obj = static_cast<MeshObject*>(m_giSystem->m_scene->GetSceneObject(card->m_meshObjectID));
		if (!obj)
			continue;
                
		CardInstanceData* instance = obj->GetCardInstance(card->m_templateIndex);
		if (!instance)
			continue;
                
		const SurfaceCardTemplate& templ = obj->GetMesh()->m_cardTemplates[card->m_templateIndex];
                
		CaptureSingleCard(obj, card, instance, templ);
                
		card->m_pendingUpdate = false;
		card->m_lastTouchedFrame = m_frameIndex;
		instance->m_isDirty = false;

		if (card->m_pendingRealloc)
		{
			FinalizeCardCapture(card);
		}
	}
	m_giSystem->RemoveProcessedDirtyCards(cardsToUpdate.size());
}

void DX12Renderer::CaptureSingleCard(MeshObject* object, SurfaceCard* card, CardInstanceData* instance, const SurfaceCardTemplate& templ)
{
	uint32_t width = card->m_pixelResolution.x;   
	uint32_t height = card->m_pixelResolution.y;  
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;       
	viewport.TopLeftY = 0.0f;     
	viewport.Width = (float)width;  
	viewport.Height = (float)height; 
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_commandList->RSSetViewports(1, &viewport);
	D3D12_RECT scissorRect = {};
	scissorRect.left = 0;            
	scissorRect.top = 0;            
	scissorRect.right = (LONG)width; 
	scissorRect.bottom = (LONG)height;
	m_commandList->RSSetScissorRects(1, &scissorRect);

	// m_commandList->OMSetRenderTargets(
	// 	SURFACE_CACHE_LAYER_COUNT,
	// 	rtvHandles,
	// 	FALSE,
	// 	nullptr  // 可选：如果需要 depth，传入 DSV handle
	// );
	m_commandList->OMSetRenderTargets(SURFACE_CACHE_LAYER_CAPTURE_COUNT,
		m_surfaceCache.m_tempCapture.m_tempRtvs, FALSE, nullptr);
	float clearColor[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < SURFACE_CACHE_LAYER_CAPTURE_COUNT; i++)
	{
		m_commandList->ClearRenderTargetView(m_surfaceCache.m_tempCapture.m_tempRtvs[i],
		                                     clearColor, 0, nullptr);
	}
	//SetupCardCaptureCamera(card, instance, templ);
	CardCaptureConstants captureConsts = {};
	captureConsts.CardOrigin = instance->m_worldOrigin;
	captureConsts.CardAxisX = instance->m_worldAxisX;
	captureConsts.CardAxisY = instance->m_worldAxisY;
	captureConsts.CardNormal = instance->m_worldNormal;
    
	captureConsts.CardSize = instance->m_worldSize;
	captureConsts.CaptureDirection = templ.m_direction;
	captureConsts.Resolution = card->m_pixelResolution.x;
	captureConsts.CaptureDepth = object->CalculateCaptureDepth(instance, templ.m_direction);
	memcpy(captureConsts.LightMask, instance->m_lightMask, sizeof(captureConsts.LightMask));
    
	m_constantBuffers[k_cardCaptureConstantsSlot]->AppendData(&captureConsts, sizeof(CardCaptureConstants), m_currentCaptureIndex);
	BindConstantBuffer(k_cardCaptureConstantsSlot, m_constantBuffers[k_cardCaptureConstantsSlot]);

	uint64_t psoKey = ((uint64_t)width << 32) | height;
	auto it = m_cardCapturePSOConfiguration.find(psoKey);
	ID3D12PipelineState* pso = (it != m_cardCapturePSOConfiguration.end()) ? it->second : m_cardCapturePSO;
	m_commandList->SetPipelineState(pso);
	
	DrawObjectsForCardCapture(card);

	for (int i = 0; i < SURFACE_CACHE_LAYER_CAPTURE_COUNT; i++)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_surfaceCache.m_tempCapture.m_tempTextures[i],
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_COPY_SOURCE
		);
		m_commandList->ResourceBarrier(1, &barrier);
	}
	// 8. 复制到 Atlas 的不同层
	for (int layer = 0; layer < SURFACE_CACHE_LAYER_CAPTURE_COUNT; layer++)
	{
		CopyCardToAtlasLayer(
			m_surfaceCache.m_tempCapture.m_tempTextures[layer],
			m_surfaceCache.GetAtlasTexture(),
			layer,
			card->m_atlasPixelCoord.x,
			card->m_atlasPixelCoord.y,
			card->m_pixelResolution.x,  //实际分辨率，不是最大分辨率
			card->m_pixelResolution.y
		);
	}
    
	// 9. 转换状态：恢复temp textures-> RTV
	for (int i = 0; i < SURFACE_CACHE_LAYER_CAPTURE_COUNT; i++)
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			m_surfaceCache.m_tempCapture.m_tempTextures[i],
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		m_commandList->ResourceBarrier(1, &barrier);
	}
	
	m_currentCaptureIndex ++;

	DebuggerPrintf("[Renderer] Captured card %u at atlas (%u,%u) size %ux%u\n",
				   card->m_globalCardID, card->m_atlasPixelCoord.x, card->m_atlasPixelCoord.y, width, height);
}

void DX12Renderer::DrawObjectsForCardCapture(SurfaceCard* card)
{
	MeshObject* obj = static_cast<MeshObject*>(m_giSystem->m_scene->GetSceneObject(card->m_meshObjectID));
	if (!obj || !obj->GetMesh())
		return;
	StaticMesh* mesh = obj->GetMesh();
	ModelConstants modelConstants;
	modelConstants.ModelToWorldTransform = obj->m_cachedWorldMatrix;
	obj->m_color.GetAsFloats(modelConstants.ModelColor);
	m_constantBuffers[k_cardCaptureModelConstantsSlot]->AppendData(&modelConstants, sizeof(ModelConstants), m_currentCaptureIndex); 
	BindConstantBuffer(k_cardCaptureModelConstantsSlot, m_constantBuffers[k_cardCaptureModelConstantsSlot]);
	ConstantBuffer* cb = m_constantBuffers[k_cardCaptureMaterialConstantsSlot];
	MaterialConstants materialConstant = {};
	if (mesh->m_diffuseTexture)
		materialConstant.DiffuseId = mesh->m_diffuseTexture->m_textureDescIndex;
	else
		materialConstant.DiffuseId = 0;
	if (mesh->m_normalTexture)
		materialConstant.NormalId = mesh->m_normalTexture->m_textureDescIndex;
	else
		materialConstant.NormalId = 1;
	if (mesh->m_specularTexture)
		materialConstant.SpecularId = mesh->m_specularTexture->m_textureDescIndex;
	else
		materialConstant.SpecularId = 2;
	cb->AppendData(&materialConstant, sizeof(MaterialConstants), m_currentCaptureIndex); 
	BindConstantBuffer(k_cardCaptureMaterialConstantsSlot, cb);
	
	BindVertexBuffer(mesh->m_vertexBuffer);
	BindIndexBuffer(mesh->m_indexBuffer);
	m_commandList->DrawIndexedInstanced((UINT)mesh->m_indices.size(), 1, 0, 0, 0);
}

void DX12Renderer::FinalizeCardCapture(SurfaceCard* card)
{
	if (card->m_oldAtlasCoord.x >= 0 && card->m_oldAtlasCoord.y >= 0)
	{
		m_giSystem->FreeCardSpace(card->m_oldAtlasCoord, card->m_oldTileSpan);
    
		DebuggerPrintf("[Renderer] Released old tile for card %u at (%d,%d)\n",
					   card->m_globalCardID, card->m_oldAtlasCoord.x, card->m_oldAtlasCoord.y);
    
		card->m_oldAtlasCoord = IntVec2(-1, -1);
		card->m_oldTileSpan = IntVec2(0, 0);
	}

	card->m_resident = true;
	card->m_pendingRealloc = false;
}

void DX12Renderer::CopyCardToAtlasLayer(ID3D12Resource* srcTexture, ID3D12Resource* dstAtlasArray, uint32_t dstLayer,
	uint32_t dstX, uint32_t dstY, uint32_t width, uint32_t height)
{
	D3D12_BOX srcBox = {};
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = width;
	srcBox.bottom = height;
	srcBox.back = 1;
    
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = dstAtlasArray;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = dstLayer;  // Layer index
    
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = srcTexture;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;
	
	m_commandList->CopyTextureRegion(
		&dst,
		dstX, dstY, 0,  // 目标位置
		&src,
		&srcBox         // 源区域
	);
}

void DX12Renderer::CreateCardCapturePSO(const IntVec2& resolution)
{
	std::string shaderSource;
    int result = FileReadToString(shaderSource, "Data/Shaders/CardCapture.hlsl");
    if (result < 0)
    {
        DebuggerPrintf("[CardCapture] Failed to load CardCapture.hlsl\n");
        return;
    }

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;

    CompileShaderToByteCode(&vs, "CardCaptureVS",
        shaderSource.c_str(), "CardCaptureVS", "vs_5_0");
    CompileShaderToByteCode(&ps, "CardCapturePS",
        shaderSource.c_str(), "CardCapturePS", "ps_5_1");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",     0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // PSO description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_graphicsRootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = true;

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.IndependentBlendEnable = false;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 4;  
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; 
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; 
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT; 
    psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16B16A16_FLOAT;  // DirectLight

    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = m_device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pso)
    );

    if (FAILED(hr))
    {
        DebuggerPrintf("[CardCapture] Failed to create PSO for %dx%d\n",
            resolution.x, resolution.y);
        DX_SAFE_RELEASE(pso);
        vs->Release();
        ps->Release();
        return;
    }

    std::wstring name = L"CardCapturePSO_" + std::to_wstring(resolution.x) +
        L"x" + std::to_wstring(resolution.y);
    pso->SetName(name.c_str());

    vs->Release();
    ps->Release();

    uint64_t key = ((uint64_t)resolution.x << 32) | (uint64_t)resolution.y;
    
    auto it = m_cardCapturePSOConfiguration.find(key);
    if (it != m_cardCapturePSOConfiguration.end() && it->second != nullptr)
    {
        it->second->Release();
    }
    
    m_cardCapturePSOConfiguration[key] = pso;
    
    if (m_cardCapturePSO == nullptr)
    {
        m_cardCapturePSO = pso;
    }
    
    DebuggerPrintf("[CardCapture] Successfully created PSO for %dx%d\n",
        resolution.x, resolution.y);
}

void DX12Renderer::InitializeSurfaceCaches()
{
	m_surfaceCache.Initialize(
		m_device,
		m_giSystem->m_config.m_primaryAtlasSize,
		m_giSystem->m_config.m_primaryTileSize
	);
	
	CreateSurfaceCacheDescriptorsAndTransitionStates(&m_surfaceCache);
}

void DX12Renderer::CreateCardCapturePassResources()
{
	InitializeSurfaceCaches();

	for (int layer = 0; layer < SURFACE_CACHE_LAYER_CAPTURE_COUNT; ++layer)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());
		//ID3D12Resource* atlasTexture = m_surfaceCaches[SURFACE_CACHE_TYPE_PRIMARY].GetCurrentAtlasTexture();
        
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		//rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2DArray.MipSlice = 0;
		//rtvDesc.Texture2DArray.FirstArraySlice = layer;
		//rtvDesc.Texture2DArray.ArraySize = 1;

		rtvHandle.Offset(GBUFFER_COUNT+FRAME_BUFFER_COUNT + layer, m_rtvDescriptorSize);
		m_device->CreateRenderTargetView(m_surfaceCache.m_tempCapture.m_tempTextures[layer],
		                                 &rtvDesc, rtvHandle);
		m_surfaceCache.m_tempCapture.m_tempRtvs[layer] = rtvHandle;
	}

	CreateCardCapturePipelineStates();
}

void DX12Renderer::CreateCardCapturePipelineStates()
{
	std::vector<IntVec2> resolutions = {
		IntVec2(32, 32),
		IntVec2(64, 64),
		IntVec2(128, 128),
		IntVec2(512, 512)
	};
	for (const IntVec2& res : resolutions)
	{
		CreateCardCapturePSO(res); 
	}
}

void DX12Renderer::CreateCompositeResources()
{
	ID3DBlob* error = nullptr;
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #ifdef NDEBUG
    compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    #endif
    HRESULT hr = D3DCompileFromFile(
        L"Data/Shaders/CompositeShader.hlsl", 
        nullptr, 
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "CompositeVS", "vs_5_1", 
        compileFlags, 0, 
        &vsBlob, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[Composite] VS compile error: %s\n", 
                (char*)error->GetBufferPointer());
            error->Release();
        }
        return;
    }
    hr = D3DCompileFromFile(
        L"Data/Shaders/CompositeShader.hlsl", 
        nullptr, 
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "CompositePS", "ps_5_1", 
        compileFlags, 0, 
        &psBlob, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[Composite] PS compile error: %s\n", 
                (char*)error->GetBufferPointer());
            error->Release();
        }
        vsBlob->Release();
    	ERROR_AND_DIE("Failed to compile CompositeShader and create CompositePSO!");
        return;
    }
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_graphicsRootSignature;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;  // Back Buffer 格式
    psoDesc.SampleDesc.Count = 1;
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_compositePSO));
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr))
    {
        DebuggerPrintf("[Composite] Failed to create PSO: 0x%08X\n", hr);
        return;
    }
    m_compositePSO->SetName(L"Composite_PSO");
    
    DebuggerPrintf("[Composite] Resources created successfully\n");
}

void DX12Renderer::BindSurfaceCacheForCompute(SurfaceCache* cache) //暂时用不到咧 <-还是要复用起来
{
	if (!cache) return;
	// 转换Surface Cache资源到UAV状态
	CD3DX12_RESOURCE_BARRIER barriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			cache->GetAtlasTexture(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			cache->GetMetadataBuffer(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		)
	};
	m_commandList->ResourceBarrier(2, barriers);
    
	// 确保使用正确的描述符堆
	ID3D12DescriptorHeap* heaps[] = { m_cbvSrvDescHeap };
	m_commandList->SetDescriptorHeaps(1, heaps);

	// 绑定 Surface Cache UAVs（Atlas + Metadata）到参数 1
	// 注意：这里绑定从 Atlas UAV 开始的连续 descriptor table
	//CD3DX12_GPU_DESCRIPTOR_HANDLE surfaceCacheUAVHandle(
	//	m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
	//	SURFACE_CACHE_PRIMARY_ATLAS_UAV_INDEX,  // 从 Atlas UAV 开始，包含 Atlas + Metadata
	//	m_scuDescriptorSize
	//);
	//m_commandList->SetComputeRootDescriptorTable(1, surfaceCacheUAVHandle);
}

void DX12Renderer::BindSurfaceCacheForGraphics(SurfaceCache* cache)
{
	if (!cache) 
		return;
	// 转换Surface Cache到SRV状态（从UAV转换）
	CD3DX12_RESOURCE_BARRIER barriers[] = {
		// CD3DX12_RESOURCE_BARRIER::Transition(
		// 	cache->GetAtlasTexture(),
		// 	D3D12_RESOURCE_STATE_COMMON,
		// 	//D3D12_RESOURCE_STATE_UNORDERED_ACCESS,  // 从UAV
		// 	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE  // 到SRV
		// ),
		CD3DX12_RESOURCE_BARRIER::Transition(
			cache->GetMetadataBuffer(),
			D3D12_RESOURCE_STATE_COMMON,
			//D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, //暂时是这样，之后有了radiance cache它俩会统一~
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	};
	m_commandList->ResourceBarrier(1, barriers);

	int baseSlot = PrimaryAtlasSrvIndex(0);

	// 绑定 Surface Cache SRVs（Atlas + Metadata）到 16
	CD3DX12_GPU_DESCRIPTOR_HANDLE surfaceCacheSRVHandle(
		m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart(),
		baseSlot,  // 从 Atlas SRV 开始，包含 Atlas + Metadata
		m_scuDescriptorSize
	);
	m_commandList->SetGraphicsRootDescriptorTable(16, surfaceCacheSRVHandle);
}

void DX12Renderer::VisualizeSurfaceCache()
{
}

void DX12Renderer::InitializeRadianceCache()
{
	if (!m_device)
	{
		ERROR_AND_DIE("[DX12Renderer] Cannot initialize Radiance Cache: device is null!");
	}
    
	// 配置参数
	uint32_t maxProbes = 4096;      // 最多 4096 个 Probes
	uint32_t raysPerProbe = 32;     // 每个 Probe 发射 32 条射线
    
	m_radianceCache.Initialize(m_device, maxProbes, raysPerProbe);
    
	DebuggerPrintf("[DX12Renderer] Radiance Cache initialized: %u max probes, %u rays/probe\n",
				   maxProbes, raysPerProbe);
}

void DX12Renderer::CreateRadianceCacheResources()
{
	InitializeRadianceCache();
	
	for (int i = 0; i < RADIANCE_CACHE_BUFFER_COUNT; i++)
    {
        ID3D12Resource* probeBuffer = m_radianceCache.m_probeBuffer[i];
        if (!probeBuffer)
            continue;
        
        // === SRV ===
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_radianceCache.GetMaxProbes();
        srvDesc.Buffer.StructureByteStride = sizeof(RadianceProbeGPU);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        int srvIndex = RadianceCacheProbeSrvIndex(i);
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = GetCPUDescriptorHandle(
            m_cbvSrvDescHeap,
            srvIndex
        );
        m_device->CreateShaderResourceView(probeBuffer, &srvDesc, srvHandle);
        
        // === UAV ===
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = m_radianceCache.GetMaxProbes();
        uavDesc.Buffer.StructureByteStride = sizeof(RadianceProbeGPU);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        
        int uavIndex = RadianceCacheProbeUavIndex(i);
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = GetCPUDescriptorHandle(
            m_cbvSrvDescHeap,
            uavIndex
        );
        m_device->CreateUnorderedAccessView(probeBuffer, nullptr, &uavDesc, uavHandle);
    }
    
    // ========== Update List SRV ==========
    {
        ID3D12Resource* updateListBuffer = m_radianceCache.GetUpdateListBuffer();
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = 256;  // 最多 256 个更新索引
        srvDesc.Buffer.StructureByteStride = 0;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        int srvIndex = RadianceCacheUpdateListSrvIndex();
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = GetCPUDescriptorHandle(
            m_cbvSrvDescHeap,
            srvIndex
        );
        m_device->CreateShaderResourceView(updateListBuffer, &srvDesc, srvHandle);
    }
    
    DebuggerPrintf("[DX12Renderer] Created Radiance Cache descriptors\n");
}

void DX12Renderer::CreateRadianceCacheUpdatePSO()
{
	const char* shaderName = "Data/Shaders/RadianceCacheUpdate";
	Shader* test = GetShaderByName(shaderName);
	if (test)
	{
		return;
	}
	std::string shaderHLSLName = std::string(shaderName) + ".hlsl";
	std::string shaderSource;
	int result = FileReadToString(shaderSource, shaderHLSLName);
	if (result < 0)
	{
		ERROR_AND_DIE("Fail to create shader from this shaderName");
	}
	ID3DBlob* computeShaderBlob = nullptr;
	bool success = CompileShaderToByteCode(
		&computeShaderBlob,
		"RadianceCacheUpdate",
		shaderSource.c_str(),
		"CSMain",
		"cs_5_1"
	);
	if (!success || !computeShaderBlob)
	{
		ERROR_AND_DIE("[DX12Renderer] Failed to compile RadianceCacheUpdate.hlsl!");
	}
    
	// 创建 Compute PSO
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_computeRootSignature;
	psoDesc.CS = {
		computeShaderBlob->GetBufferPointer(),
		computeShaderBlob->GetBufferSize()
	};
	psoDesc.NodeMask = 0;
	psoDesc.CachedPSO = {};
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    
	HRESULT hr = m_device->CreateComputePipelineState(
		&psoDesc,
		IID_PPV_ARGS(&m_radianceCacheUpdatePSO)
	);
    
	if (FAILED(hr))
	{
		ERROR_AND_DIE("[DX12Renderer] Failed to create Radiance Cache Update PSO!");
	}
    
	DX_SAFE_RELEASE(computeShaderBlob);
    
	DebuggerPrintf("[DX12Renderer] Created Radiance Cache Update PSO\n");
}

void DX12Renderer::BeginRadianceCachePass()
{
	RadianceCacheManager* manager = m_giSystem->GetRadianceCacheManager();
    if (!manager)
        return;
    
    manager->PlaceProbesScreenSpace(m_camera, m_gBuffer);
    
    manager->BuildPriorityQueue(m_camera, m_frameIndex);
    
    m_radianceCache.BuildUpdateQueue(s_maxProbesPerUpdate); //256
    
    uint32_t updateCount = m_radianceCache.GetUpdateProbeCount();
    if (updateCount == 0)
    {
        return;
    }
    
    // ========== 4. 上传数据到 GPU ==========
    m_radianceCache.UploadProbeData();
    
    RadianceCacheConstants constants = {};
    constants.MaxProbes = m_radianceCache.GetMaxProbes();
    constants.ActiveProbeCount = m_radianceCache.GetActiveProbeCount();
    constants.UpdateProbeCount = updateCount;
    constants.RaysPerProbe = m_radianceCache.GetRaysPerProbe();
    
    Vec3 camPos = m_currentCam.CameraWorldPosition;
    constants.CameraPositionX = camPos.x;
    constants.CameraPositionY = camPos.y;
    constants.CameraPositionZ = camPos.z;
    
    constants.ViewProj = m_camera.GetProjectionMatrix(); //肯定有问题
    constants.ViewProjInverse = constants.ViewProj.GetOrthonormalInverse();
    
    constants.ScreenWidth = (float)m_config.m_window->GetClientDimensions().x;
    constants.ScreenHeight = (float)m_config.m_window->GetClientDimensions().y;
    constants.CurrentFrame = m_frameIndex;
    constants.TemporalBlend = 0.9f;  // 90% 保留旧数据
    
    constants.MaxTraceDistance = 50.0f;
    constants.ProbeSpacing = 2.0f;
    
    constants.ActiveCardCount = (uint32_t)m_giSystem->GetCurrentSurfaceCardMetadataCPU().size();
    constants.AtlasWidth = m_surfaceCache.m_atlasSize;
    constants.AtlasHeight = m_surfaceCache.m_atlasSize;
    constants.TileSize = m_surfaceCache.m_tileSize;
    constants.BVHNodeCount = m_cardBVHNodeCount;

	m_constantBuffers[k_radianceCacheConstantsSlot]->AppendData(&constants, sizeof(RadianceCacheConstants), m_currentDrawIndex); 
	BindConstantBuffer(k_radianceCacheConstantsSlot,
		m_constantBuffers[k_radianceCacheConstantsSlot]);
	 
    // Surface Cache Atlas → SRV
    TransitionResource(
        m_surfaceCache.GetAtlasTexture(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    
    // Previous Probe Buffer → SRV
    TransitionResource(
        m_radianceCache.GetPreviousProbeBuffer(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    // Current Probe Buffer → UAV
    TransitionResource(
        m_radianceCache.GetCurrentProbeBuffer(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    
    // Card BVH → SRV
    if (m_cardBVHNodeBuffer)
    {
        TransitionResource(
            m_cardBVHNodeBuffer,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
    }
    
    if (m_cardBVHIndexBuffer)
    {
        TransitionResource(
            m_cardBVHIndexBuffer,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
    }
    
    // ========== 6. 绑定资源 ==========
    BindRadianceCacheForCompute(&m_radianceCache);
    
    // ========== 7. 设置 PSO ==========
    m_commandList->SetPipelineState(m_radianceCacheUpdatePSO);
    m_commandList->SetComputeRootSignature(m_computeRootSignature);
    
    // ========== 8. Dispatch ==========
    uint32_t groupCount = (updateCount + 63) / 64;  // 每个 thread group 64 个线程
    m_commandList->Dispatch(groupCount, 1, 1);
    
    
    DebuggerPrintf("[DX12Renderer] Updated %u radiance probes\n", updateCount);
}

void DX12Renderer::EndRadianceCachePass()
{
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = m_radianceCache.GetCurrentProbeBuffer();
	m_commandList->ResourceBarrier(1, &uavBarrier);
    
	TransitionResource(
		m_surfaceCache.GetAtlasTexture(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	TransitionResource(
		m_radianceCache.GetCurrentProbeBuffer(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON
	);
}

void DX12Renderer::BindRadianceCacheForCompute(RadianceCache* cache)
{
	if (!cache || !cache->m_initialized)
    {
        DebuggerPrintf("[Renderer] Warning: Cannot bind uninitialized Radiance Cache\n");
        return;
    }
    
    m_commandList->SetComputeRootSignature(m_computeRootSignature);
    
    DebuggerPrintf("[Renderer] Binding Radiance Cache for Compute...\n");
    
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = m_cbvSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    UINT inc = m_scuDescriptorSize;
    
    // ========================================
    // Root Parameter [9]: Radiance Cache Probe UAV (Current, u4)
    // ========================================
    {
        int currentIndex = cache->GetCurrentBufferIndex();
        int uavIndex = RadianceCacheProbeUavIndex(currentIndex);  
        
        D3D12_GPU_DESCRIPTOR_HANDLE probeUavHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            gpuStart,
            uavIndex,
            inc
        );
        
        m_commandList->SetComputeRootDescriptorTable(9, probeUavHandle);
        
        DebuggerPrintf("  [9] Probe UAV (Current): descriptor index %d\n", uavIndex);
    }
    
    // ========================================
    // Root Parameter [11]: Radiance Cache Probe SRV (Previous, t208)
    // ========================================
    {
        int previousIndex = 1 - cache->GetCurrentBufferIndex();
        int srvIndex = RadianceCacheProbeSrvIndex(previousIndex);
        
        D3D12_GPU_DESCRIPTOR_HANDLE probeSrvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            gpuStart,
            srvIndex,
            inc
        );
        
        m_commandList->SetComputeRootDescriptorTable(11, probeSrvHandle);
        
        DebuggerPrintf("  [11] Probe SRV (Previous): descriptor index %d\n", srvIndex);
    }
    
    // Root Parameter [10]: Surface Cache Atlas SRV 
    {
        // 绑定 PRIMARY Surface Cache Atlas（用于采样直接光照）
        //SurfaceCache* primaryCache = &m_surfaceCaches[SURFACE_CACHE_TYPE_PRIMARY];
        //int currentIndex = primaryCache->GetCurrentBufferIndex();
        int atlasSrvIndex = PrimaryAtlasSrvIndex(0);  // 例如 219 TODO
        
        D3D12_GPU_DESCRIPTOR_HANDLE atlasSrvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            gpuStart,
            atlasSrvIndex,
            inc
        );
        
        m_commandList->SetComputeRootDescriptorTable(10, atlasSrvHandle);
        
        DebuggerPrintf("  [10] Surface Cache Atlas SRV: descriptor index %d\n", atlasSrvIndex);
    }
    // Root Parameter [6]: Card Metadata SRV (t6)
    {
        //SurfaceCache* primaryCache = &m_surfaceCache;
        //int currentIndex = primaryCache->GetCurrentBufferIndex();
        int metaSrvIndex = PrimaryMetaSrvIndex(0); 
        D3D12_GPU_DESCRIPTOR_HANDLE metaSrvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            gpuStart,
            metaSrvIndex,
            inc
        );
        
        m_commandList->SetComputeRootDescriptorTable(6, metaSrvHandle);
        
        DebuggerPrintf("  [6] Card Metadata SRV: descriptor index %d\n", metaSrvIndex);
    }
    
    // Root Parameter [12]: Card BVH SRVs
    {
        // 注意：这里绑定的是 BVH Nodes 的起始位置
        // Descriptor Range 会自动覆盖 t240 (Nodes) 和 t241 (Indices)
        int bvhNodeSrvIndex = CARD_BVH_NODE_SRV;  //228
        
        D3D12_GPU_DESCRIPTOR_HANDLE bvhHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            gpuStart,
            bvhNodeSrvIndex,
            inc
        );
        
        m_commandList->SetComputeRootDescriptorTable(12, bvhHandle);
        
        DebuggerPrintf("  [12] Card BVH: descriptor index %d (Nodes) + %d (Indices)\n", 
            bvhNodeSrvIndex, bvhNodeSrvIndex + 1);
    }
    
    // Root Parameter [13]: Radiance Cache Constants (b13)
    {
        RadianceCacheConstants rcConstants = {};
        
        rcConstants.MaxProbes = cache->GetMaxProbes();
        rcConstants.ActiveProbeCount = cache->GetActiveProbeCount();
        rcConstants.UpdateProbeCount = cache->GetUpdateProbeCount();
        rcConstants.RaysPerProbe = cache->GetRaysPerProbe();
        
        rcConstants.CameraPositionX = m_camera.GetPosition().x;
        rcConstants.CameraPositionY = m_camera.GetPosition().y;
        rcConstants.CameraPositionZ = m_camera.GetPosition().z;
        
        Mat44 viewProj = m_camera.GetProjectionMatrix(); //TO CHECK
        viewProj.Append(m_camera.GetCameraToRenderTransform());
        rcConstants.ViewProj = viewProj;
        
        Mat44 viewProjInverse = viewProj;
        rcConstants.ViewProjInverse = viewProjInverse.GetOrthonormalInverse();
        
        IntVec2 screenSize = m_config.m_window->GetClientDimensions();
        rcConstants.ScreenWidth = (float)screenSize.x;
        rcConstants.ScreenHeight = (float)screenSize.y;
        
        rcConstants.CurrentFrame = m_frameIndex;
        rcConstants.TemporalBlend = 0.9f;  // Lumen 默认值
        
        rcConstants.MaxTraceDistance = 100.0f;  // 可配置
        rcConstants.ProbeSpacing = 2.0f;        // 可配置
        
        SurfaceCache* primaryCache = &m_surfaceCache;
        rcConstants.ActiveCardCount = (uint32_t)m_giSystem->GetCurrentSurfaceCardMetadataCPU().size();
        rcConstants.AtlasWidth = primaryCache->m_atlasSize;
        rcConstants.AtlasHeight = primaryCache->m_atlasSize;
        rcConstants.TileSize = primaryCache->m_tileSize;
        
        rcConstants.BVHNodeCount = m_cardBVHNodeCount;
        
        m_constantBuffers[k_radianceCacheConstantsSlot]->AppendData(
            &rcConstants,
            sizeof(RadianceCacheConstants),
            m_currentDrawIndex
        );
        D3D12_GPU_VIRTUAL_ADDRESS cbvAddress = 
            m_constantBuffers[k_radianceCacheConstantsSlot]->m_dx12ConstantBuffer->GetGPUVirtualAddress();
        m_commandList->SetComputeRootConstantBufferView(0, cbvAddress);
        
        DebuggerPrintf("  [13] Radiance Cache Constants: %u probes, %u active, %u updating\n",
            rcConstants.MaxProbes, rcConstants.ActiveProbeCount, rcConstants.UpdateProbeCount);
    }
    
    // Root Parameter [1]: Update List Buffer (已在其他地方处理)
    // Update List 在 cache->UploadProbeData() 中上传
    DebuggerPrintf("[Renderer] Radiance Cache bound for Compute\n");
}

void DX12Renderer::BindRadianceCacheForGraphics(RadianceCache* cache)
{
	if (!cache || !cache->m_initialized)
	{
		DebuggerPrintf("[Renderer] Warning: Cannot bind uninitialized Radiance Cache\n");
		return;
	}
	ID3D12Resource* currentProbeBuffer = cache->GetCurrentProbeBuffer();
    
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		currentProbeBuffer,
		D3D12_RESOURCE_STATE_COMMON,  // 或 D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);
	m_commandList->ResourceBarrier(1, &barrier);
    
	D3D12_GPU_VIRTUAL_ADDRESS probeBufferAddress = currentProbeBuffer->GetGPUVirtualAddress();
	m_commandList->SetGraphicsRootShaderResourceView(18, probeBufferAddress);
	// DebuggerPrintf("[Renderer] Bound Radiance Cache SRV at GPU address 0x%llx\n", 
	// 			   probeBufferAddress);
}

void DX12Renderer::UploadBufferData(ID3D12Resource* dstBuffer, const void* srcData, uint32_t dataSize) //TODO
{
	if (!dstBuffer || !srcData || dataSize == 0)
		return;
	// 创建 Upload Heap
	D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);
    
	ID3D12Resource* uploadBuffer = nullptr;
	HRESULT hr = m_device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer)
	);
	if (FAILED(hr))
	{
		ERROR_AND_DIE("[DX12Renderer] Failed to create upload buffer!");
	}
	// Map and copy
	void* mappedData = nullptr;
	hr = uploadBuffer->Map(0, nullptr, &mappedData);
	if (SUCCEEDED(hr))
	{
		memcpy(mappedData, srcData, dataSize);
		uploadBuffer->Unmap(0, nullptr);
	}
	// Transition dst to COPY_DEST
	TransitionResource(dstBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	// Copy
	m_commandList->CopyResource(dstBuffer, uploadBuffer);
	// Transition back to SRV
	TransitionResource(dstBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    
	// 暂时保存，在帧结束后释放
	m_currentFrameTempResources.push_back(uploadBuffer);
}

void DX12Renderer::CreateCardBVHBuffers(const std::vector<GPUCardBVHNode>& nodes,
                                        const std::vector<uint32_t>& cardIndices)
{
	if (nodes.empty() || cardIndices.empty())
    {
        DebuggerPrintf("[DX12Renderer] Warning: Empty BVH data\n");
        return;
    }
    // 创建 BVH Node Buffer 
    {
        uint32_t bufferSize = static_cast<uint32_t>(nodes.size() * sizeof(GPUCardBVHNode));
        DX_SAFE_RELEASE(m_cardBVHNodeBuffer);
		
        // 创建 DEFAULT heap
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        HRESULT hr = m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_cardBVHNodeBuffer)
        );
        
        if (FAILED(hr))
        {
            ERROR_AND_DIE("[DX12Renderer] Failed to create Card BVH node buffer!");
        }
        
        m_cardBVHNodeBuffer->SetName(L"CardBVH_NodeBuffer");
        m_cardBVHNodeCount = static_cast<uint32_t>(nodes.size());
        
        UploadBufferData(m_cardBVHNodeBuffer, nodes.data(), bufferSize);
    }
    // 创建 BVH Index Buffer
    {
        uint32_t bufferSize = static_cast<uint32_t>(cardIndices.size() * sizeof(uint32_t));
        DX_SAFE_RELEASE(m_cardBVHIndexBuffer);
        
        // 创建 DEFAULT heap
        D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        HRESULT hr = m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_cardBVHIndexBuffer)
        );
        if (FAILED(hr))
        {
            ERROR_AND_DIE("[DX12Renderer] Failed to create Card BVH index buffer!");
        }
        m_cardBVHIndexBuffer->SetName(L"CardBVH_IndexBuffer");
        m_cardBVHIndexCount = static_cast<uint32_t>(cardIndices.size());
        UploadBufferData(m_cardBVHIndexBuffer, cardIndices.data(), bufferSize);
    }
    // Node Buffer SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_cardBVHNodeCount;
        srvDesc.Buffer.StructureByteStride = sizeof(GPUCardBVHNode);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptorHandle(
            m_cbvSrvDescHeap,
            CARD_BVH_NODE_SRV
        );
        m_device->CreateShaderResourceView(m_cardBVHNodeBuffer, &srvDesc, handle);
    }
    // Index Buffer SRV
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_cardBVHIndexCount;
        srvDesc.Buffer.StructureByteStride = 0;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        
        D3D12_CPU_DESCRIPTOR_HANDLE handle = GetCPUDescriptorHandle(
            m_cbvSrvDescHeap,
            CARD_BVH_INDEX_SRV
        );
        m_device->CreateShaderResourceView(m_cardBVHIndexBuffer, &srvDesc, handle);
    }
    DebuggerPrintf("[DX12Renderer] Created Card BVH buffers: %u nodes, %u indices\n",
                   m_cardBVHNodeCount, m_cardBVHIndexCount);
}

void DX12Renderer::TransitionResource(ID3D12Resource* resource,
	D3D12_RESOURCE_STATES beforeState,
	D3D12_RESOURCE_STATES afterState)
{
	if (beforeState == afterState)
	{
		DebuggerPrintf("Before and after states are the same, no transition needed.\n");
		return;
	}
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
	m_commandList->ResourceBarrier(1, &barrier);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Renderer::GetCPUDescriptorHandle(ID3D12DescriptorHeap* heap, int index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * m_scuDescriptorSize;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12Renderer::GetGPUDescriptorHandle(ID3D12DescriptorHeap* heap, int index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += index * m_scuDescriptorSize;
	return handle;
}

void DX12Renderer::CreateCombineSurfaceCacheResources()
{
	m_combineSurfaceCache = new CombineSurfaceCache();
	m_combineSurfaceCache->Initialize(m_device,
		m_cbvSrvDescHeap);
}

void DX12Renderer::RenderingCombineSurfaceCachePass()
{
	m_combineSurfaceCache->Execute(m_commandList, &m_surfaceCache, m_giSystem->m_scene->m_nextCardID);
}

void DX12Renderer::CreateSurfaceCacheRadiosityResources()
{
	m_surfaceRadiosity = new SurfaceRadiosity();
	m_surfaceRadiosity->Initialize(
		m_device,
		m_cbvSrvDescHeap,
		m_giSystem->m_config.m_primaryAtlasSize,  // Atlas Width
		m_giSystem->m_config.m_primaryAtlasSize   // Atlas Height
	);
}

void DX12Renderer::RenderingSurfaceCacheRadiosityPass()
{
	if (!m_surfaceRadiosity) 
        return;
	CD3DX12_RESOURCE_BARRIER preRadiosityBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(m_globalSDFTexture,
			D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_voxelLightingTexture,
			D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_surfaceCache.m_atlasTexture,D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES),
		CD3DX12_RESOURCE_BARRIER::Transition(m_surfaceCache.m_cardMetadataBuffer,
			D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
	};
	m_commandList->ResourceBarrier(_countof(preRadiosityBarriers), preRadiosityBarriers);
    
    SurfaceRadiosityConstants constants = {};
    constants.AtlasWidth = m_giSystem->m_config.m_primaryAtlasSize;        // 4096
    constants.AtlasHeight = m_giSystem->m_config.m_primaryAtlasSize;      // 4096
    constants.ProbeGridWidth = constants.AtlasWidth/4;   // 4096 / 4
    constants.ProbeGridHeight =  constants.AtlasWidth/4;  // 4096 / 4
    // Radiosity 追踪配置
    constants.RaysPerProbe = RadiosityConfig::RAYS_PER_PROBE;           // 16
    constants.ProbeSpacing = (float)RadiosityConfig::PROBE_SPACING;     // 4.0
    constants.TraceMaxDistance = RadiosityConfig::TRACE_DISTANCE;       // 200.0
    constants.TraceMaxSteps = RadiosityConfig::TRACE_MAX_STEPS;         // 64
    // 追踪参数
    constants.TraceHitThreshold = 0.02f;
    constants.RayBias = 0.5f;
    constants.TemporalBlendFactor = RadiosityConfig::TEMPORAL_BLEND;    // 0.05
    constants.SkyIntensity = 0.3f;
    // Global SDF 信息
    AABB3 sceneBounds = m_giSystem->m_scene->m_sceneBounds;
    Vec3 sceneCenter = (sceneBounds.m_maxs + sceneBounds.m_mins) * 0.5f;
    float sceneRadius = sceneBounds.GetBoundsSize().GetLength() * 0.5f;
    constants.GlobalSDFCenter[0] = sceneCenter.x;
    constants.GlobalSDFCenter[1] = sceneCenter.y;
    constants.GlobalSDFCenter[2] = sceneCenter.z;
    constants.GlobalSDFExtent = sceneRadius;
    
    Vec3 invExtent = Vec3(1.0f / sceneRadius, 1.0f / sceneRadius, 1.0f / sceneRadius);
    constants.GlobalSDFInvExtent[0] = invExtent.x;
    constants.GlobalSDFInvExtent[1] = invExtent.y;
    constants.GlobalSDFInvExtent[2] = invExtent.z;
    constants.GlobalSDFResolution = GLOBAL_SDF_RESOLUTION;  // 根据你的实际 SDF 分辨率

    // Voxel Lighting 场景边界 (与 InjectVoxelLighting 一致)
    constants.SceneBoundsMin[0] = sceneBounds.m_mins.x;
    constants.SceneBoundsMin[1] = sceneBounds.m_mins.y;
    constants.SceneBoundsMin[2] = sceneBounds.m_mins.z;
    constants.SceneBoundsMax[0] = sceneBounds.m_maxs.x;
    constants.SceneBoundsMax[1] = sceneBounds.m_maxs.y;
    constants.SceneBoundsMax[2] = sceneBounds.m_maxs.z;

    constants.DepthWeightScale = 10.0f;
    constants.NormalWeightScale = 4.0f;
    constants.FilterRadius = 1;
    constants.IndirectIntensity = 1.0f;
    constants.FrameIndex = m_frameIndex;
    constants.ActiveCardCount = m_giSystem->m_scene->m_nextCardID;
    
    ConstantBuffer* radiosityCB = m_constantBuffers[k_surfaceRadiosityConstantsSlot];
    radiosityCB->AppendData(&constants, sizeof(SurfaceRadiosityConstants), 0);
    m_surfaceRadiosity->Execute(m_commandList, radiosityCB, constants, &m_surfaceCache);
}

void DX12Renderer::CreateScreenProbeGatherResources()
{
	m_screenProbeFinalGather = new ScreenProbeFinalGather();
	m_screenProbeFinalGather->Initialize(
		m_device,
		m_cbvSrvDescHeap,
		m_config.m_window->GetClientDimensions().x,  
		m_config.m_window->GetClientDimensions().y   
	);
}

void DX12Renderer::RenderingScreenProbeGatherPass()
{
	if (!m_screenProbeFinalGather)
        return;
    ScreenProbeConstants constants = {};
    IntVec2 screenDims = m_config.m_window->GetClientDimensions();
    constants.ScreenWidth = screenDims.x;
    constants.ScreenHeight = screenDims.y;
    constants.ProbeGridWidth = (screenDims.x + 7) / 8;   // 8 像素间距
    constants.ProbeGridHeight = (screenDims.y + 7) / 8;
    // Probe 配置
    constants.ProbeSpacing = SCREEN_PROBE_SPACING;           // 8
    constants.RaysPerProbe = SCREEN_PROBE_RAYS;              // 64
    constants.RaysTexWidth = constants.ProbeGridWidth * 8;
    constants.RaysTexHeight = constants.ProbeGridHeight * 8;
    // 追踪参数
    constants.TraceMaxDistance = 100.0f;        // Mesh SDF
    constants.TraceMaxSteps = 64;
    constants.TraceHitThreshold = 0.02f;
    constants.RayBias = 0.5f;
    constants.MeshSDFTraceDistance = MESH_SDF_TRACE_DISTANCE;   // 100.0
    constants.VoxelTraceDistance = VOXEL_TRACE_DISTANCE;        // 500.0
    constants.TemporalBlendFactor = 0.05f;
    constants.SkyIntensity = 0.3f;

    constants.CurrentFrame = m_frameIndex;
    constants.OctahedronSize = OCTAHEDRON_SIZE;                 // 8
    constants.OctahedronBorder = OCTAHEDRON_BORDER;             // 1
    constants.BorderedOctSize = BORDERED_OCTAHEDRON_SIZE;       // 10
    
    AABB3 sceneBounds = m_giSystem->m_scene->m_sceneBounds;
    Vec3 sceneCenter = (sceneBounds.m_maxs + sceneBounds.m_mins) * 0.5f;
    float sceneRadius = sceneBounds.GetBoundsSize().GetLength() * 0.5f;
    constants.GlobalSDFCenter = sceneCenter;
    constants.GlobalSDFExtent = sceneRadius;
    
    Vec3 invExtent = Vec3(1.0f / sceneRadius, 1.0f / sceneRadius, 1.0f / sceneRadius);
    constants.GlobalSDFInvExtent = invExtent;
    constants.GlobalSDFResolution = 128;
    
    // if (m_voxelScene.IsValid())
    // {
	Vec3 sceneSize = sceneBounds.GetBoundsSize();
	float maxDim = max(sceneSize.x, max(sceneSize.y, sceneSize.z));
	constants.VoxelSize = maxDim / (float)GLOBAL_SDF_RESOLUTION; 
	constants.VoxelResolution = GLOBAL_SDF_RESOLUTION;

	constants.DepthThreshold = 0.1f;
	constants.PlaneDepthWeight = 10.0f;
	constants.BRDFWeight = 0.5f;
	constants.LightingWeight = 0.5f;
	constants.FilterRadius = 1;
	constants.DepthWeightScale = 10.0f;
	constants.NormalWeightScale = 4.0f;
	constants.AOStrength = 0.5f;
	constants.OctTexWidth = constants.ProbeGridWidth * BORDERED_OCTAHEDRON_SIZE;
	constants.OctTexHeight = constants.ProbeGridHeight * BORDERED_OCTAHEDRON_SIZE;
	constants.Padding3 = 0;
	constants.Padding4 = 0;
    
    constants.AtlasWidth = m_giSystem->m_config.m_primaryAtlasSize;
    constants.AtlasHeight = m_giSystem->m_config.m_primaryAtlasSize;
    constants.TileSize = m_giSystem->m_config.m_primaryTileSize;
    constants.ActiveCardCount = m_giSystem->m_scene->m_nextCardID;
	constants.CameraPosition = m_currentCam.CameraWorldPosition;
	
	constants.WorldToCamera = m_currentCam.WorldToCameraTransform; 
	constants.CameraToRender = m_currentCam.CameraToRenderTransform; 
	constants.RenderToClip = m_currentCam.RenderToClipTransform;     
	constants.CameraToWorld = m_currentCam.WorldToCameraTransform.GetInverse(); 
	constants.RenderToCamera = m_currentCam.CameraToRenderTransform.GetInverse(); 
	constants.ClipToRender = m_currentCam.RenderToClipTransform.GetInverse();  

	constants.PrevWorldToCamera = m_previousCam.WorldToCameraTransform;
	constants.PrevCameraToRender = m_previousCam.CameraToRenderTransform;
	constants.PrevRenderToClip = m_previousCam.RenderToClipTransform;

	constants.IndirectIntensity = 1.0f;
	constants.UseHistoryBufferB = m_screenProbeFinalGather->GetUseHistoryB() ? 1 : 0;

	constants.CameraNear = 0.1f;
	constants.CameraFar = 300.f;
	m_screenProbeConstants = constants;
    
	ConstantBuffer* screenProbeCB = m_constantBuffers[k_screenProbeFinalGatherConstantsSlot];
	screenProbeCB->AppendData(&constants, sizeof(ScreenProbeConstants), 0);

	CD3DX12_RESOURCE_BARRIER barriers[GBUFFER_COUNT + DEPTH_SRV_COUNT];
	for (int i = 0; i < GBUFFER_COUNT; ++i)
	{
		barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_gBuffer.GetResource(i),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
	}
	barriers[GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
		m_depthStencilBuffer,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE  
	);
	m_commandList->ResourceBarrier(GBUFFER_COUNT + DEPTH_SRV_COUNT, barriers);
	
	D3D12_GPU_DESCRIPTOR_HANDLE sdfSrvHandle = GetGPUDescriptorHandle(
		m_cbvSrvDescHeap,
		SDF_TEXTURE_SRV_BASE
	);
	m_screenProbeFinalGather->Execute(m_commandList, screenProbeCB, constants, sdfSrvHandle, &m_gBuffer, &m_surfaceCache);

	CD3DX12_RESOURCE_BARRIER restoreBarriers[GBUFFER_COUNT + DEPTH_SRV_COUNT];
	for (int i = 0; i < GBUFFER_COUNT; ++i)
	{
		restoreBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
			m_gBuffer.GetResource(i),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE  // 恢复给后续 Pixel Shader 使用
		);
	}
	restoreBarriers[GBUFFER_COUNT] = CD3DX12_RESOURCE_BARRIER::Transition(
	m_depthStencilBuffer,
	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
	D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
);
	m_commandList->ResourceBarrier(GBUFFER_COUNT + DEPTH_SRV_COUNT, restoreBarriers);
}

void DX12Renderer::CreateGIVisualizationResources()
{
	m_giVisualization = new GIVisualization();
	m_giVisualization->Initialize(
		m_device,
		m_cbvSrvDescHeap,
		m_config.m_window->GetClientDimensions().x,  
		m_config.m_window->GetClientDimensions().y   
	);
}

void DX12Renderer::RenderingGIVisualizationPass(const CompositeConstants& compositeConsts)
{
    if (!m_vizEnabled)
        return;
    
    if (!m_giVisualization->IsInitialized())
        return;
    
    ID3D12DescriptorHeap* heaps[] = { m_cbvSrvDescHeap };
    m_commandList->SetDescriptorHeaps(1, heaps);
    
    GIVisualizationMode mode = m_vizParams.Mode;
    
    if (IsSurfaceCacheMode(mode))
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_frameIndex * m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();

        //float clearColor[4] = { 0.2f, 0.2f, 0.3f, 1.0f };  // 深蓝灰色
        float clearColor[4] = { 0.f, 0.f, 0.f, 0.0f }; 
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

        float width = (float)m_config.m_window->GetClientDimensions().x;
        float height = (float)m_config.m_window->GetClientDimensions().y;

        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
        D3D12_RECT scissorRect = { 0, 0, (LONG)width, (LONG)height };
        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissorRect);
    }

    m_giVisualization->Execute(
        m_commandList,
        m_constantBuffers[k_giVisualizationConstantsSlot],
        m_vizParams,
        compositeConsts,
        m_screenProbeConstants,
        m_currentCam,
        &m_surfaceCache,
        m_giSystem->m_scene->m_nextCardID
    );

    if (GIVisualization::NeedsCopyToBackBuffer(mode))
    {
        m_giVisualization->CopyOutputToTarget(m_commandList, m_renderTargets[m_frameIndex]);
    }
}

void DX12Renderer::SetSamplerMode(SamplerMode samplerMode)
{
	m_desiredSamplerMode = samplerMode;
}

void DX12Renderer::SetRasterizerMode(RasterizerMode rasterizerMode)
{
	m_desiredRasterizerMode = rasterizerMode;
}

void DX12Renderer::SetDepthMode(DepthMode depthMode)
{
	m_desiredDepthMode = depthMode;
}

void DX12Renderer::BeginRenderPass(RenderMode renderMode, Rgba8 const& backBufferClearColor)
{
	if (renderMode == m_currentRenderMode)
		return;

	switch (m_currentRenderMode)
	{
	case RenderMode::FORWARD:
		if (m_forwardPassActive)
		{
			EndForwardPass();
		}
		break;
	case RenderMode::GI:
		if (m_gBufferPassActive)
		{
			EndGBufferPass();
			UnbindGBufferPassRT();
		}
		break;
	}
	m_desiredRenderMode = renderMode;
	switch (m_desiredRenderMode)
	{
	case RenderMode::FORWARD:
		BeginForwardPass();
		if (!m_hasBackBufferCleared)
		{
			ClearForwardPassRTV(backBufferClearColor);
		}
		break;
	case RenderMode::GI:
		BeginGBufferPass();
		ClearGBufferPassRTV();
		break;
	}
        
	m_currentRenderMode = renderMode;
	CHECK_DEVICE(); 
}

Texture* DX12Renderer::CreateTextureFromFile(char const* filePath)
{
	Image image = Image(filePath);
	Texture* newTexture = CreateTextureFromImage(image);
	newTexture->m_name = filePath;
	
	m_loadedTextures.push_back(newTexture);
	
	return newTexture;
}

void DX12Renderer::BindConstantBuffer(int slot, ConstantBuffer* cbo)
{
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = cbo->m_dx12ConstantBuffer->GetGPUVirtualAddress()
										+ cbo->m_offset;
	m_commandList->SetGraphicsRootConstantBufferView(slot, cbAddress);
}

IndexBuffer* DX12Renderer::CreateIndexBuffer(unsigned int size, unsigned int stride)
{
	IndexBuffer* indexBuffer = new IndexBuffer(size, stride);

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	HRESULT hr = m_device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer->m_dx12IndexBuffer));
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "D3D12 Cannot Create Index Buffer!");

	std::wstring name = L"IndexUpload_" + std::to_wstring(reinterpret_cast<uintptr_t>(indexBuffer));
	indexBuffer->m_dx12IndexBuffer->SetName(name.c_str());

	return indexBuffer;
}

void DX12Renderer::BindVertexBuffer(VertexBuffer* vbo)
{
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &vbo->m_vertexBufferView);
}

void DX12Renderer::WaitForPreviousFrame()              
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. More advanced samples 
	// illustrate how to use fences for efficient resource usage.

	// Signal and increment the fence value.

	// swap the current rtv buffer index so we draw on the correct buffer
	//m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	const UINT64 fenceValueToSignal = m_fenceValue[m_frameIndex];
	m_commandQueue->Signal(m_fence[m_frameIndex], fenceValueToSignal);
	//其实没啥用
	if (m_fence[m_frameIndex]->GetCompletedValue() < fenceValueToSignal)
	{
		m_fence[m_frameIndex]->SetEventOnCompletion(fenceValueToSignal, m_fenceEvent);
    
		DWORD result = WaitForSingleObject(m_fenceEvent, 5000); // 5秒
		if (result == WAIT_TIMEOUT) {
			OutputDebugStringA("[TDR] GPU TIMEOUT!\n");
			DebugBreak();
		}
	}
	// for (auto& res : m_previousFrameTempResources)
	// {
	// 	DX_SAFE_RELEASE(res);
	// }
	// m_previousFrameTempResources.clear();
	
	// increment fenceValue for next frame
	m_fenceValue[m_frameIndex]++;
}

void DX12Renderer::WaitForComputeQueue()
{
	if (m_computeFence->GetCompletedValue() < m_computeFenceValue)
	{
		char buf[128];
		sprintf_s(buf, "[DX12] Waiting for Compute Queue (fence=%llu)...\n", m_computeFenceValue);
		OutputDebugStringA(buf);
        
		HRESULT hr = m_computeFence->SetEventOnCompletion(m_computeFenceValue, m_computeFenceEvent);
		GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to set Compute Fence event");
        
		DWORD waitResult = WaitForSingleObject(m_computeFenceEvent, INFINITE);
		GUARANTEE_OR_DIE(waitResult == WAIT_OBJECT_0, "Compute Fence wait failed");
        
		OutputDebugStringA("[DX12] Compute Queue completed\n");
	}
	else
	{
		OutputDebugStringA("[DX12] Compute Queue already completed\n");
	}
}

void DX12Renderer::StartupComputeQueue()
{
	DebuggerPrintf("[DX12] Creating Async Compute Queue...\n");
    // 1. 创建Compute Queue
    D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
    computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    computeQueueDesc.NodeMask = 0;
    HRESULT hr = m_device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(&m_computeQueue));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Compute Queue");
    m_computeQueue->SetName(L"Async Compute Queue");
    // 2. 创建Compute Allocator
    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, 
                                          IID_PPV_ARGS(&m_computeAllocator));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Compute Allocator");
    m_computeAllocator->SetName(L"Compute Allocator");
    // 3. 创建Compute CommandList
    hr = m_device->CreateCommandList(0, 
                                      D3D12_COMMAND_LIST_TYPE_COMPUTE, 
                                      m_computeAllocator, 
                                      nullptr, 
                                      IID_PPV_ARGS(&m_computeCommandList));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Compute CommandList");
    m_computeCommandList->SetName(L"Compute CommandList");
    // 必须Close一次才能后续Reset
    m_computeCommandList->Close();
    // 4. 创建Compute Fence
    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Compute Fence");
    m_computeFence->SetName(L"Compute Fence");
    // 5. 创建Fence Event
    m_computeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    GUARANTEE_OR_DIE(m_computeFenceEvent != nullptr, "Failed to create Compute Fence Event");
	size_t alignedSize = AlignUp(sizeof(SDFGenerationConstants), 256);
	//size_t totalBufferSize = alignedSize * 256;
	m_computeConstantBuffer = new ConstantBuffer(alignedSize, sizeof(SDFGenerationConstants));
	D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(alignedSize);
	D3D12_HEAP_PROPERTIES properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	hr = m_device->CreateCommittedResource(
		&properties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_computeConstantBuffer->m_dx12ConstantBuffer)
	);
	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create compute constant buffer!");
	CD3DX12_RANGE range(0, 0);
	hr = m_computeConstantBuffer->m_dx12ConstantBuffer->Map(0, &range,
		reinterpret_cast<void**>(&m_computeConstantBuffer->m_mappedPtr));
	m_computeConstantBuffer->m_dx12ConstantBuffer->SetName(L"ComputeConstantBuffer");
    m_computeFenceValue = 0;
    DebuggerPrintf("[DX12] Async Compute Queue created successfully\n");
}

void DX12Renderer::ShutdownComputeQueue()
{
	if (m_computeFenceEvent)
	{
		CloseHandle(m_computeFenceEvent);
		m_computeFenceEvent = nullptr;
	}
	DX_SAFE_RELEASE(m_computeFence);
	DX_SAFE_RELEASE(m_computeCommandList);
	DX_SAFE_RELEASE(m_computeAllocator);
	DX_SAFE_RELEASE(m_computeQueue);
	delete m_computeConstantBuffer;
	m_computeConstantBuffer = nullptr;
	DebuggerPrintf("[DX12] Async Compute Queue released\n");
}

void DX12Renderer::ImGuiStartUp()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 
	ImGui::StyleColorsDark();  // 或 Light/Classic

	D3D12_DESCRIPTOR_HEAP_DESC imGuiSRVDescHeap = {};
	imGuiSRVDescHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imGuiSRVDescHeap.NumDescriptors = FRAME_BUFFER_COUNT;
	imGuiSRVDescHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	imGuiSRVDescHeap.NodeMask = 0;

	HRESULT hr = m_device->CreateDescriptorHeap(&imGuiSRVDescHeap, IID_PPV_ARGS(&m_imguiSrvDescHeap));

	GUARANTEE_OR_DIE(SUCCEEDED(hr), "Could not create D3D12 ImGui SRV desc heap!"); 

	m_imguiSrvDescHeap->SetName(L"ImGuiHeap");

	ImGui_ImplWin32_Init(g_theWindow->GetHwnd());
	ImGui_ImplDX12_Init(m_device, FRAME_BUFFER_COUNT, DXGI_FORMAT_R8G8B8A8_UNORM, m_imguiSrvDescHeap,
	                    m_imguiSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
	                    m_imguiSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	ImGui_ImplDX12_NewFrame();
}
void DX12Renderer::ImGuiBeginFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	// GI Visualization 的控制已移至游戏代码，通过公共接口调用
}
void DX12Renderer::ImGuiEndFrame()
{
	ID3D12DescriptorHeap* heaps[] = { m_imguiSrvDescHeap };
	m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList);
}
void DX12Renderer::ImGuiShutDown()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();
}
size_t DX12Renderer::GetConstantBufferSize(int cbSlot)
{
	if (cbSlot == k_modelConstantsSlot)
		return sizeof(ModelConstants);
	if (cbSlot == k_cameraConstantsSlot)
		return sizeof(CameraConstants);
	if (cbSlot == k_perFrameConstantsSlot)
		return sizeof(PerFrameConstants);
	if (cbSlot == k_materialConstantsSlot)
		return sizeof(MaterialConstants);
	if (cbSlot == k_generalLightConstantsSlot)
		return sizeof(GeneralLightConstants);
	// if (cbSlot == k_surfaceCacheConstantsSlot)
	// 	return sizeof(SurfaceCacheConstants);
	// if (cbSlot == k_passConstantsSlot)
	// 	return sizeof(PassModeConstants);
	return 64;
}

bool DX12Renderer::RenderGIVisualizationImGuiPanel()
{
	if (!m_config.m_enableGI)
		return false;

	if (!m_vizEnabled)
		return false;

	if (!m_giVisualization || !m_giVisualization->IsInitialized())
		return false;

	return m_giVisualization->RenderImGuiPanel(m_vizParams);
}
#endif
