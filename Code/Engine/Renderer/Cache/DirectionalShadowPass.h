#pragma once
#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_DX12_RENDERER
#include <d3d12.h>
#include "ThirdParty/d3dx12/d3dx12.h"
#include "Engine/Renderer/RenderCommon.h"

class Scene;
class ConstantBuffer;
class DX12Renderer;

class DirectionalShadowPass
{
public:
    DirectionalShadowPass() = default;
    ~DirectionalShadowPass();

    void Initialize(ID3D12Device5* device,
                    ID3D12RootSignature* graphicsRootSignature,
                    ID3D12DescriptorHeap* cbvSrvDescHeap,
                    ID3D12DescriptorHeap* dsvDescHeap,
                    UINT descriptorSize);

    void Execute(ID3D12GraphicsCommandList* cmdList,
                 ConstantBuffer* shadowCB,
                 DX12Renderer* renderer,
                 Scene* scene);

    void Shutdown();

    ID3D12Resource* GetShadowMapTexture() const { return m_shadowMapTexture; }
    const Mat44& GetCachedLightWorldToCamera() const { return m_cachedLightWorldToCamera; }
    const Mat44& GetCachedLightCameraToRender() const { return m_cachedLightCameraToRender; }
    const Mat44& GetCachedLightRenderToClip() const { return m_cachedLightRenderToClip; }

private:
    void CreateTexture();
    void CreateDSV();
    void CreateSRV();
    void CreatePSO();

    ID3D12Device5* m_device = nullptr;
    ID3D12RootSignature* m_graphicsRootSignature = nullptr;
    ID3D12DescriptorHeap* m_cbvSrvDescHeap = nullptr;
    ID3D12DescriptorHeap* m_dsvDescHeap = nullptr;
    UINT m_descriptorSize = 0;

    ID3D12Resource* m_shadowMapTexture = nullptr;
    ID3D12PipelineState* m_shadowMapPSO = nullptr;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_shadowDsvHandle = {};

    Mat44 m_cachedLightWorldToCamera;
    Mat44 m_cachedLightCameraToRender;
    Mat44 m_cachedLightRenderToClip;

    bool m_initialized = false;
};

#endif // ENGINE_DX12_RENDERER
