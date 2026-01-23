#pragma once
#include <array>
#include <wrl/client.h>

#include "GI/GBufferData.h"
#include "Cache/CacheCommon.h"

class CombineSurfaceCache;
class ScreenProbeFinalGather;
class SurfaceRadiosity;
class GIVisualization;
class Window;
class MeshObject;
struct GPUCardBVHNode;
struct SurfaceCardTemplate;
struct CardInstanceData;
class BVH;
class SDFTexture3D;

using Microsoft::WRL::ComPtr;

#include "Engine/Renderer/Camera.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/RenderCommon.h"

#ifdef ENGINE_DX12_RENDERER

// #define DX_SAFE_RELEASE(dxObject)\
// {\
// if ((dxObject) != nullptr)\
// {\
// (dxObject)->Release();\
// (dxObject) = nullptr;\
// }\
// }\

// #if defined(OPAQUE)
// #undef OPAQUE
// #endif

#if defined(ENGINE_DEBUG_RENDER)
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

#if defined(_DEBUG)
#define ENGINE_DEBUG_RENDER
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN    // Exclude rarely-used stuff from Windows headers.
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "ThirdParty/d3dx12/d3dx12.h"
#include <D3Dcompiler.h>
#include <dxgidebug.h>
#include <d3dcommon.h>
#include <DirectXMath.h>
#include <map>
#include <string>

#include "GI/GISystem.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")


struct ID3D12Device;
struct IDXGISwapChain3;
struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;
struct ID3D12Resource;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12Fence;

class DX12Renderer
{
	friend class GISystem;
	friend class Scene;
public:
	DX12Renderer(RendererConfig config);
	~DX12Renderer();
	void Startup();
	void ShutDown();
	void BeginFrame();
	void EndFrame();

	//Camera & screen
	void ClearScreen(const Rgba8& clearColor);
	void ClearScreen();
	void BeginCamera(const Camera& camera);
	void EndCamera(const Camera& camera);
	void BeginRenderPass(RenderMode renderMode, Rgba8 const& backBufferClearColor = Rgba8::TEAL);
	
	void SetViewport(const AABB2& normalizedViewport);
	void SetModelConstants( Mat44 const& modelMatrix = Mat44(), Rgba8 const& modelColor = Rgba8::WHITE );
	void SetLightConstants( Vec3 const& lightPosition, float ambient, Mat44 const& lightViewMatrix, Mat44 const& lightProjectionMatrix );
	void SetPerFrameConstants(const float time, const int debugInt, const float debugFloat);
	void SetGeneralLightConstants(Rgba8 sunColor, const Vec3& sunNormal, int numLights, std::vector<Rgba8>colors,
		std::vector<Vec3>worldPositions, std::vector<Vec3>spotForwards, std::vector<float>ambiences,
		std::vector<float>innerRadii, std::vector<float>outerRadii,
		std::vector<float>innerDotThresholds, std::vector<float>outerDotThresholds);
	void SetMaterialConstants(const Texture* diffuseTex, const Texture* normalTex, const Texture* specTex);
	//void SetComputeSurfaceCacheConstants(SurfaceCacheType type, size_t batchStart, int bindComputeSlot);
	
	//Font & Texture & Image
	BitmapFont*	CreateOrGetBitmapFont( const char* bitmapFontFilePathWithNoExtension );
	Texture* CreateOrGetTextureFromFile(char const* imageFilePath);
	Texture* CreateTextureFromFile(char const* imageFilePath);
	Texture* CreateTextureFromImage(Image const& image);
	SDFTexture3D* CreateSDFTextureFromData(const std::vector<Vertex_PCUTBN>& vertices,const std::vector<uint32_t>& indices,
	   const BVH& bvh,const AABB3& bounds,int resolution);
	SDFTexture3D* CreateSDFTextureFromData(
		const std::vector<float>& data,int resolution);
	std::vector<float> ReadbackSDF3DData(const SDFTexture3D* sdf);
	void PushBackNewTextureManually(Texture* const tex);
	//Texture* CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel, uint8_t* texelData);
	Texture* GetTextureByFileName(char const* imageFilePath);
	Image* CreateImageFromFile(char const* imageFilePath);
	void BindTexture(const Texture* texture = nullptr, int slot=0);
	
	//Shader
	Shader* CreateShader( char const* shaderName, VertexType type = VertexType::VERTEX_PCU);
	Shader* CreateShader( char const* shaderName, char const* shaderSource, VertexType type = VertexType::VERTEX_PCU );
	bool CompileShaderToByteCode( ID3DBlob** shaderByteCode, char const* name, char const* source, char const* entryPoint, char const* target );
	void BindShader( Shader* shader );
	Shader* GetShaderByName( char const* shaderName );

	//Buffers
	VertexBuffer* CreateVertexBuffer(unsigned int size, unsigned int stride);
	IndexBuffer* CreateIndexBuffer(unsigned int size, unsigned int stride);

	void BindVertexBuffer( VertexBuffer* vbo );
	void BindConstantBuffer(int slot, ConstantBuffer* cbo); //Graphics Constant Buffer
	void BindIndexBuffer( IndexBuffer* ibo );

	void DrawVertexBuffer( VertexBuffer* vbo, int vertexCount, int vertexOffset = 0 );
	
	void CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer* ibo);
	void CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo);
	void CopyCPUToGPU(const void* data, unsigned int size, ID3D12Resource* buffer);
	void CopyUploadHeapToDefaultHeap(ID3D12Resource* defaultHeap, ID3D12Resource* uploadHeap);

	//Draw
	void DrawVertexArray(const std::vector<Vertex_PCU>& verts);
	void DrawVertexArray(int numVerts, const Vertex_PCU* vert);
	void DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts);
	void DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts);
	void DrawVertexIndexArray(const std::vector<Vertex_PCU>& verts, const std::vector<unsigned int>& indices);
	void DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices);
	void DrawIndexedVertexBuffer( VertexBuffer* vbo, IndexBuffer* ibo, int indexCount, int indexOffset = 0 );

	// States
	void SetBlendMode(BlendMode blendMode);
	void SetRasterizerMode(RasterizerMode rasterizerMode);
	void SetSamplerMode(SamplerMode samplerMode);
	void SetDepthMode(DepthMode depthMode);
	
	//Setter
	void SetGISystem(GISystem* giSystem) { m_giSystem = giSystem; }

	// GI Visualization 公共接口
	void SetGIVisualizationEnabled(bool enabled) { m_vizEnabled = enabled; }
	bool IsGIVisualizationEnabled() const { return m_vizEnabled; }
	void ToggleGIVisualization() { m_vizEnabled = !m_vizEnabled; }
	GIVisualizationParams& GetGIVisualizationParams() { return m_vizParams; }
	bool RenderGIVisualizationImGuiPanel();  // 返回是否有变化

private:
	void WaitForPreviousFrame(); //wait until gpu is finished with command list
	void WaitForComputeQueue();
	void StartupComputeQueue();
	void ShutdownComputeQueue();

	//ImGui
	void	ImGuiStartUp();
	void	ImGuiBeginFrame();
	void	ImGuiEndFrame();
	void	ImGuiShutDown();

	size_t GetConstantBufferSize(int cbSlot);

	//Pass
	//GeneralRender Pass
	void SetGraphicsStatesIfChanged();

	//Forward Pass
	void BeginForwardPass();
	void ClearForwardPassRTV(Rgba8 const& clearColor);
	void EndForwardPass();
	void CreateGraphicsRootSignature();

	//GBuffer Pass
	void CreateDepthSRV();
	void CreateGBufferPassResources();
	void BeginGBufferPass();
	void ClearGBufferPassRTV();
	void EndGBufferPass();
	void UnbindGBufferPassRT();

	//SDF
	void CreateSDFGenerationRootSignature();
	void CreateSDFGenerationPSO();
	ID3D12Resource* CreateStructuredBuffer(
		SDFTexture3D* sdfOwner,
		const void* data,
		size_t numElements,
		size_t elementSize,
		const wchar_t* debugName, ID3D12GraphicsCommandList* commandList);
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle(uint32_t index);
	
	//Shadow map passs
	void CreateShadowMapResources();
	void RenderingShadowMapPass(); //暂时只有SunLight
	void ShutdownShadowMapPass();
	
	//GlobalSDF Pass
	void CreateGlobalSDFPassResources();
	void RenderingGlobalSDFPass();
	
	void CreateComputeRootSignature();
	
	void CreateSurfaceCacheDescriptorsAndTransitionStates(SurfaceCache* cache);
	
	//Card CapturePass
	void BeginCardCapturePass();
	void EndCardCapturePass();
	D3D12_CPU_DESCRIPTOR_HANDLE GetCardCaptureRTVHandle(int layer);
	void UploadCardMetadataToGPU();
	void CaptureDirtySurfaceCards(uint32_t maxCardsPerFrame);
	void CaptureSingleCard(MeshObject* object, SurfaceCard* card, CardInstanceData* instance, const SurfaceCardTemplate& templ);
	void DrawObjectsForCardCapture(SurfaceCard* card);
	void FinalizeCardCapture(SurfaceCard* card);
	void CopyCardToAtlasLayer(ID3D12Resource* srcTexture,      // 临时纹理
				ID3D12Resource* dstAtlasArray,   // Atlas (Texture2DArray)
				uint32_t dstLayer,               // 目标层 (0=Albedo, 1=Normal, 2=Material, 3=DirectLight)
				uint32_t dstX,                   // Atlas 中的 X 坐标
				uint32_t dstY,                   // Atlas 中的 Y 坐标
				uint32_t width,                  // Card 宽度
				uint32_t height);                 // Card 高度

	
	void CreateCardCapturePassResources();
	void CreateCardCapturePipelineStates();
	void CreateCardCapturePSO(const IntVec2& resolution);

	//SurfaceCache
	void InitializeSurfaceCaches();

	// GI Composite
	void CreateCompositeResources();
	void RenderingCompositePass();
	void ShutdownCompositePass();
	
	//Surface 
	void BindSurfaceCacheForCompute(SurfaceCache* cache);
	void BindSurfaceCacheForGraphics(SurfaceCache* cache);
	void VisualizeSurfaceCache();

	//Radiance Cache
	void InitializeRadianceCache();
	RadianceCache* GetRadianceCache() { return &m_radianceCache; }
	const RadianceCache* GetRadianceCache() const { return &m_radianceCache; }
	void CreateRadianceCacheResources();
	void CreateRadianceCacheUpdatePSO();
	void BeginRadianceCachePass();
	void EndRadianceCachePass();
	void BindRadianceCacheForCompute(RadianceCache* cache);
	void BindRadianceCacheForGraphics(RadianceCache* cache);

	void UploadBufferData(ID3D12Resource* dstBuffer,const void* srcData,uint32_t dataSize);

	// Card BVH
	void CreateCardBVHBuffers(
		const std::vector<GPUCardBVHNode>& nodes,
		const std::vector<uint32_t>& cardIndices
	);
	ID3D12Resource* GetCardBVHNodeBuffer() const { return m_cardBVHNodeBuffer; }
	ID3D12Resource* GetCardBVHIndexBuffer() const { return m_cardBVHIndexBuffer; }
	uint32_t GetCardBVHNodeCount() const { return m_cardBVHNodeCount; }
	uint32_t GetCardBVHIndexCount() const { return m_cardBVHIndexCount; }
	void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* heap, int index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* heap,int index);

	//Combine SurfaceCache
	void CreateCombineSurfaceCacheResources();
	void RenderingCombineSurfaceCachePass();
	
	//SurfaceCacheRadiosity
	void CreateSurfaceCacheRadiosityResources();
	void RenderingSurfaceCacheRadiosityPass();

	//ScreenProbeGather
	void CreateScreenProbeGatherResources();
	void RenderingScreenProbeGatherPass();

	//GI Visuliazation
	void CreateGIVisualizationResources();
	void RenderingGIVisualizationPass(const CompositeConstants& compositeConsts);
	
protected:
	RendererConfig m_config;

	ID3D12Device5* m_device; // direct3d device
	IDXGISwapChain3* m_swapChain; // swapchain used to switch between render targets

	ID3D12CommandQueue* m_commandQueue; // container for command lists
	
	ID3D12DescriptorHeap* m_rtvDescHeap; // a descriptor heap to hold resources like the render targets
	ID3D12DescriptorHeap* m_dsvDescHeap;
	ID3D12DescriptorHeap* m_cbvSrvDescHeap;
	ID3D12DescriptorHeap* m_imguiSrvDescHeap;
	ID3D12Resource* m_renderTargets[FRAME_BUFFER_COUNT]; // number of render targets equal to buffer count
	ID3D12Resource* m_depthStencilBuffer;
	unsigned int m_rtvDescriptorSize; // size of the rtv descriptor on the device (all front and back buffers will be the same size)
	unsigned int m_scuDescriptorSize; //the same with cbv descriptor size
	
	ID3D12CommandAllocator* m_commandAllocator[FRAME_BUFFER_COUNT]; // we want enough allocators for each buffer * number of threads (we only have one thread)
	ID3D12GraphicsCommandList* m_commandList; // a command list we can record commands into, then execute them to render the frame

	ID3D12Fence* m_fence[FRAME_BUFFER_COUNT];    // an object that is locked while our command list is being executed by the gpu. We need as many 
	//as we have allocators (more if we want to know when the gpu is finished with an asset)

	HANDLE m_fenceEvent; // a handle to an event when our fence is unlocked by the gpu
	UINT64 m_fenceValue[FRAME_BUFFER_COUNT]; // this value is incremented each frame. each fence will have its own value

	int m_frameIndex; // current rtv we are on

	// we will exit the program when this becomes false
	bool m_isRunning = true;

	ID3D12PipelineState* m_pipelineStateObject; // pso containing a pipeline state 实际应用中将拥有多个

	ID3D12RootSignature* m_graphicsRootSignature; // root signature defines data shaders will access

	D3D12_VIEWPORT m_viewport; // area that output from rasterizer will be stretched to.

	D3D12_RECT m_scissorRect; // the area to draw in. pixels outside that area will not be drawn onto
	
	//ID3D12Resource* m_immediateVertexBuffer; // a default buffer in GPU memory that we will load vertex data for our triangle into
	//D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView; // a structure containing a pointer to the vertex data in gpu memory
	// the total size of the buffer, and the size of each element (vertex) <-移动到VertexBuffer里 //二编：改ringbuffer

	//ConstantBuffer* m_constantBuffers[NUM_CONSTANT_BUFFERS];
	std::array<ConstantBuffer*, NUM_CONSTANT_BUFFERS> m_constantBuffers; //每个slot一个大buffer，包含多帧数据（更高效）
	GeneralLightConstants m_lightConstant;
	CameraConstants m_currentCam;
	CameraConstants m_previousCam;
	Camera m_camera;
	int m_currentDrawIndex = 0;
	int m_currentCaptureIndex = 0;
	//bool m_isFirstFrame = true;

	unsigned int VERTEX_RING_BUFFER_SIZE = 128 * 1024 * 1024; //128MB
	unsigned int INDEX_RING_BUFFER_SIZE = 128 * 1024 * 1024; //128MB
	std::array<VertexBuffer*, FRAME_BUFFER_COUNT> m_frameVertexBuffers;
	std::array<VertexBuffer*, FRAME_BUFFER_COUNT> m_frameVertexBuffersForTBN;
	std::array<IndexBuffer*, FRAME_BUFFER_COUNT> m_frameIndexBuffers;
	
	std::vector<Texture*> m_loadedTextures;
	std::vector<BitmapFont*> m_loadedFonts;
	Texture* m_defaultTexture = nullptr; //default diffuse map
	Texture* m_defaultNormalTexture = nullptr; //default normal map
	Texture* m_defaultSpecTexture = nullptr;
	const Texture* m_currentTexture = nullptr;

	std::vector<Shader*> m_loadedShaders;
	Shader* m_defaultShader = nullptr;
	Shader* m_currentShader = nullptr;
	Shader* m_desiredShader = nullptr;
	Shader* m_defaultGBufferShader = nullptr;

	bool m_forwardPassActive = false;
	std::map<int, ID3D12PipelineState*> m_pipelineStatesConfiguration; //Graphics PSO
	BlendMode m_currentBlendMode = BlendMode::COUNT;
	BlendMode m_desiredBlendMode = BlendMode::ALPHA;
	SamplerMode m_currentSamplerMode = SamplerMode::COUNT;
	SamplerMode m_desiredSamplerMode = SamplerMode::POINT_CLAMP;
	RasterizerMode m_currentRasterizerMode = RasterizerMode::COUNT;
	RasterizerMode m_desiredRasterizerMode = RasterizerMode::SOLID_CULL_BACK;
	DepthMode m_currentDepthMode = DepthMode::COUNT;
	DepthMode m_desiredDepthMode = DepthMode::READ_WRITE_LESS_EQUAL;

	//Pass
	RenderMode m_currentRenderMode = RenderMode::FORWARD;
	RenderMode m_desiredRenderMode = RenderMode::FORWARD;
	ActivePass m_currentActivePass = ActivePass::UNKNOWN;
	bool m_hasBackBufferCleared = false;
	//Shadow Map
	ID3D12Resource* m_shadowMapTexture = nullptr;
	ID3D12PipelineState* m_shadowMapPSO = nullptr;
	Mat44 m_cachedLightWorldToCamera;
	Mat44 m_cachedLightCameraToRender;
	Mat44 m_cachedLightRenderToClip;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_shadowDsvHandle;

	//SurfaceCaches
	SurfaceCache m_surfaceCache;
	//GI
	GISystem* m_giSystem = nullptr;
	//GBuffer
	GBufferData m_gBuffer;
	std::map<int, ID3D12PipelineState*> m_gBufferPipelineStatesConfiguration; 
	bool m_gBufferPassActive = false;

	//Composite pass
	ID3D12PipelineState* m_compositePSO = nullptr;
	//ID3D12RootSignature* m_compositeRootSignature = nullptr;
	
	//Surface Cache
	bool m_debugVisualizeSurfaceCache = false;
	
	// SDF
	ID3D12PipelineState* m_sdfGenerationPSO = nullptr;
	ID3D12RootSignature* m_sdfGenerationRootSignature = nullptr;
	std::vector<SDFTexture3D*> m_loadedSDFs; 
	int m_nextSDFDescriptorIndex = SDF_TEXTURE_SRV_BASE;
	int m_nextSDFTextureIndex = 0;
	ID3D12PipelineState* m_globalSDFGenerationPSO = nullptr;
	ID3D12RootSignature* m_globalSDFGenerationRootSignature = nullptr;
	// Global voxel scene sdf
	ID3D12Resource* m_globalSDFTexture = nullptr;        // RG32_FLOAT, 3D (距离 + 实例索引)
	ID3D12Resource* m_voxelVisibilityBuffer = nullptr;   // StructuredBuffer, 6方向
	ID3D12Resource* m_voxelLightingTexture = nullptr;    // RGBA16_FLOAT, 3D × 6方向
	ID3D12Resource* m_instanceInfoBuffer = nullptr;      // StructuredBuffer<MeshSDFInfoGPU>
	ID3D12Resource* m_instanceInfoUploadBuffer = nullptr;
	ID3D12PipelineState* m_buildGlobalSDFPSO = nullptr;
	ID3D12PipelineState* m_buildVisibilityPSO = nullptr;
	ID3D12PipelineState* m_injectLightingPSO = nullptr;
	ID3D12RootSignature* m_globalSDFRootSignature = nullptr;

	//Compute surface cache
	//ID3D12PipelineState* m_surfaceCacheExtractPSO = nullptr;
	ID3D12RootSignature* m_computeRootSignature = nullptr;

	//Card capture
	ID3D12PipelineState* m_cardCapturePSO = nullptr;
	std::map<uint64_t, ID3D12PipelineState*> m_cardCapturePSOConfiguration;
	int m_currentCardUpdateIndex = 0;

	//Radiance Cache
	ID3D12PipelineState* m_radianceCacheUpdatePSO = nullptr;
	RadianceCache m_radianceCache;
	ID3D12Resource* m_cardBVHNodeBuffer = nullptr;
	ID3D12Resource* m_cardBVHIndexBuffer = nullptr;
	uint32_t m_cardBVHNodeCount = 0;
	uint32_t m_cardBVHIndexCount = 0;

	std::vector<ID3D12Resource*> m_currentFrameTempResources;
	std::vector<ID3D12Resource*> m_previousFrameTempResources;

	//Async Compute Queue
	ID3D12CommandQueue* m_computeQueue = nullptr;
	ID3D12CommandAllocator* m_computeAllocator = nullptr;
	ID3D12GraphicsCommandList* m_computeCommandList = nullptr;
	ID3D12Fence* m_computeFence = nullptr;
	HANDLE m_computeFenceEvent = nullptr;
	ConstantBuffer* m_computeConstantBuffer = nullptr; //暂时只用于sdf gen
	uint64_t m_computeFenceValue = 0;

	CombineSurfaceCache* m_combineSurfaceCache = nullptr;
	SurfaceRadiosity* m_surfaceRadiosity = nullptr;
	ScreenProbeFinalGather* m_screenProbeFinalGather = nullptr;
	ScreenProbeConstants m_screenProbeConstants;
	
	GIVisualization* m_giVisualization = nullptr;
	GIVisualizationParams m_vizParams;
	bool m_vizEnabled = false;
};
#endif