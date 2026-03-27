#pragma once
#include <d3d11.h>

#include "Game/EngineBuildPreferences.hpp"
#include "Engine/Renderer/RenderCommon.h"
#include "Engine/Renderer/Camera.hpp"
#include "Engine/Core/EngineConfig.h"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include "Engine/Core/Vertex_Font.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/IntVec2.hpp"
#include <vector>

class DX11Renderer;
class DX12Renderer;
#ifdef ENGINE_VULKAN_RENDERER
class VulkanRenderer;
#endif

class Window;
class BitmapFont;
class BMFont;
class Image;
class Texture;
class Shader;
class VertexBuffer;
class ConstantBuffer;
class IndexBuffer;

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11RasterizerState;
struct ID3D11BlendState;
struct ID3D11SamplerState;
struct ID3D11Texure2D;
struct ID3D11DepthStencilView;
struct ID3D11DepthStencilState;

#if defined(ENGINE_DEBUG_RENDER)
    extern void* m_dxgiDebug;
    extern void* m_dxgiDebugModule;
#endif

struct RendererConfig
{
    Window* m_window = nullptr;
	//RenderMode m_mode = RenderMode::FORWARD;
	bool m_fullscreen = false;
#ifdef ENGINE_DX12_RENDERER
    bool m_enableGI = false;
    //GIConfig m_giConfig;
#endif
};


class Renderer 
{
public:

    Renderer(RendererConfig config);

    //void InitializeWindow(void* hwnd);
    void Startup();  
  
    //Create functions called in Startup()
    void CreateDeviceAndSwapChain(unsigned int deviceFlags);
	void CreateBlendStates();
	void SetDefaultTexture();
	void CreateAndBindDefaultShader();
    void CreateSampleStates();
	void CreateRasterizerStates();
	void GetBackBufferTexture();
	void CreateDepthStencilTextureAndView();
	void CreateDepthStencilStates();

    virtual void ShutDown();
    
    //void CreateRenderingContext();
    virtual void BeginFrame();
    virtual void EndFrame(); 

	virtual void ClearScreen(const Rgba8& clearColor);
	virtual void ClearScreen();
	virtual void BeginCamera(const Camera& camera);
	virtual void EndCamera(const Camera& camera);
	//void SetViewport(const AABB2& viewport, const AABB2& viewportBase = AABB2(Vec2(0.f,0.f), Vec2(1600.f,800.f)));
	virtual void SetViewport(const AABB2& normalizedViewport);
	virtual void DrawVertexArray(const std::vector<Vertex_PCU>& verts);
	virtual void DrawVertexArray(int numVerts, const Vertex_PCU* verts);
	virtual void DrawVertexArray(const std::vector<Vertex_PCUTBN>& verts);
	virtual void DrawVertexArray(int numVerts, const Vertex_PCUTBN* verts);
	virtual void DrawVertexArray(const std::vector<Vertex_Font>& verts);
	virtual void DrawVertexArray(int numVerts, const Vertex_Font* verts);
	virtual void DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices);
	virtual void DrawVertexIndexArray(const std::vector<Vertex_PCUTBN>& verts, const std::vector<unsigned int>& indices, VertexBuffer* vbo, IndexBuffer* ibo);
	virtual void DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices);
	virtual void DrawVertexIndexArray(int numVerts, const Vertex_PCUTBN* verts, int numIndices, const unsigned int* indices, VertexBuffer* vbo, IndexBuffer* ibo);
    //static void DrawLine(Vec2, Vec2, Rgba8);

    void DrawAABB2(const AABB2& bounds, const Rgba8& color);

    Image* CreateImageFromFile(char const* imageFilePath);
    Texture* CreateTextureFromImage(const Image& image, bool usingMipmaps = false);
    Texture* CreateOrGetTextureFromFile(char const* imageFilePath, bool usingMipmaps = false);
    Texture* CreateTextureFromData(char const* name, IntVec2 dimensions, int bytesPerTexel, uint8_t* texelData, bool usingMipmaps = false);
    Texture* CreateTextureFromFile(char const* imageFilePath, bool usingMipmaps = false);
    Texture* GetTextureForFileName(const char* imageFilePath);
    void BindTexture(const Texture* texture, int slot = 0);
   
    BitmapFont* CreateOrGetBitmapFont(const char* bitmapFontFilePathWithNoExtension);
    BitmapFont* CreateOrGetProportionalFont(const char* bitmapFontFilePathWithNoExtension);
    BMFont* CreateOrGetBMFont(const char* fntFilePath);
    void SetFontConstants(float sdfThreshold = 0.5f, float sdfSmoothRange = 0.05f, float time = 0.f, float weight = 0.f);

    //Shader Related Functions
    Shader* CreateShader(char const* shaderName, char const* shaderSource, VertexType vertexType = VertexType::VERTEX_PCU);
	Shader* CreateShader(char const* shaderName, VertexType vertexType = VertexType::VERTEX_PCU);
    Shader* CreateOrGetShader(char const* shaderName, VertexType vertexType = VertexType::VERTEX_PCU);
    bool CompileShaderToByteCode(char const* name, char const* source, char const* entryPoint, char const* target,
    	std::vector<unsigned char>& outByteCode,
    	ID3DBlob** shaderByteCode);
    void BindShader(Shader* shader);

    //Functions about vertex buffer & index & constant buffer
    VertexBuffer* CreateVertexBuffer(const unsigned int size, unsigned int stride);
    void BindVertexBuffer(VertexBuffer* vbo);
    void DrawVertexBuffer(VertexBuffer* vbo, unsigned int vertexCount);
    void CopyCPUToGPU(const void* data, unsigned int size, VertexBuffer* vbo);

    IndexBuffer* CreateIndexBuffer(const unsigned int size, unsigned int stride);
    void BindIndexBuffer(IndexBuffer* ibo);
	void DrawIndexBuffer(VertexBuffer* vbo, IndexBuffer* ibo, unsigned int indexCount, PrimitiveTopology topology = PRIMITIVE_TRIANGLES);
    void CopyCPUToGPU(const void* data, unsigned int size, IndexBuffer*& ibo);
	
    ConstantBuffer* CreateConstantBuffer(const unsigned int size);
	void CopyCPUToGPU(const void* data, unsigned int size, ConstantBuffer* cbo);
	void BindConstantBuffer(int slot, ConstantBuffer* cbo);

    //Set blend states function
    void SetBlendMode(BlendMode blendMode);
    void SetBlendModeIfChanged();

    //RasterizerMode setting function
    void SetRasterizerMode(RasterizerMode rasterizerMode);
    void SetRasterizerModeIfChanged();

    //SamplerMode setting function
    void SetSamplerMode(SamplerMode samplerMode, int slot = 0);
    void SetSamplerModeIfChanged();
    void SetSamplerModeIfChanged(int slot);

    //Depth
    void SetDepthMode(DepthMode depthMode);
    void SetDepthModeIfChanged();

	//RenderMode
	void SetRenderMode(RenderMode renderMode);

	void SetGeneralLightConstants(Rgba8 sunColor, const Vec3& sunNormal, int numLights, std::vector<Rgba8>colors,
	                              std::vector<Vec3>worldPositions, std::vector<Vec3>spotForwards, std::vector<float>ambiences,
	                              std::vector<float>innerRadii, std::vector<float>outerRadii,
	                              std::vector<float>innerDotThresholds, std::vector<float>outerDotThresholds);
    //void SetLightConstants(const Vec3& sunDirection, const float sunIntensity, const Rgba8& ambientColor);
#ifdef ENGINE_PAST_VERSION_LIGHTS
	void SetLightConstants(const Vec3& sunDirection, const float sunIntensity, const float ambientIntensity, const Rgba8& ambientColor=Rgba8::WHITE);
    void SetPointLightConstants(const std::vector<Vec3>& pos, std::vector<float> intensity, const std::vector<Rgba8>& color, std::vector<float> range);
    void SetSpotLightConstants(const std::vector<Vec3>& pos, std::vector<float> cutOff, const std::vector<Vec3>& dir, const std::vector<Rgba8>& color);
#endif
	void SetModelConstants(const Mat44& modelToWorldTransform = Mat44(), const Rgba8& modelColor = Rgba8::WHITE);
	void SetMaterialConstants(const Texture* diffuseTex = nullptr, const Texture* normalTex = nullptr, const Texture* specularTex = nullptr);

	// Instanced drawing for GBuffer pass
	void ResetInstanceData();
	uint32_t AppendInstanceData(const Mat44& worldMatrix, const Rgba8& color);
	void DrawIndexedInstancedBatch(VertexBuffer* vbo, IndexBuffer* ibo,
		int indexCount, uint32_t startInstance, uint32_t instanceCount);
    void SetShadowConstants(const Mat44& lightViewProjectionMatrix = Mat44());
	void SetPerFrameConstants(const float time, const int debugInt, const float debugFloat);

    //Shadow map
    void CreateShadowMapResources();
    //Shadow map functions
    void BeginShadowPass();
    void EndShadowPass();
    void CreateShadowMapShader();
    void BindShadowMapTextureAndSampler();

    //Get
#ifdef ENGINE_DX11_RENDERER
        DX11Renderer* GetSubRenderer() 
        {
            return m_dx11Renderer;
        }
#endif
#ifdef ENGINE_DX12_RENDERER
		DX12Renderer* GetSubRenderer()
		{
			return m_dx12Renderer;
		}
		const DX12Renderer* GetSubRenderer() const
		{
			return m_dx12Renderer;
		}
#endif
#ifdef ENGINE_VULKAN_RENDERER
		VulkanRenderer* GetSubRenderer()
		{
			return m_vulkanRenderer;
		}
#endif

public:
		RendererConfig m_config;

protected:
#ifdef ENGINE_DX11_RENDERER
    DX11Renderer* m_dx11Renderer = nullptr;
#endif
#ifdef ENGINE_DX12_RENDERER
    DX12Renderer* m_dx12Renderer = nullptr;
#endif
#ifdef ENGINE_VULKAN_RENDERER
    VulkanRenderer* m_vulkanRenderer = nullptr;
#endif

private:

};
