#pragma once
#include "Engine/Core/EngineCommon.hpp"

#ifdef ENGINE_DX12_RENDERER
#include <d3d12.h>
#include <vector>
#include "ThirdParty/d3dx12/d3dx12.h"
#include "Engine/Renderer/RenderCommon.h"

class Scene;
class ConstantBuffer;
class DX12Renderer;
class MeshObject;

class PointLightShadowPass
{
public:
    PointLightShadowPass() = default;
    ~PointLightShadowPass();

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

    ID3D12Resource* GetCubeArrayTexture() const { return m_cubeArrayTexture; }
    int GetNumActiveShadowLights() const { return m_numActiveShadowLights; }
    int GetShadowLightIndex(int slot) const { return (slot >= 0 && slot < MAX_SHADOW_CASTING_POINT_LIGHTS) ? m_shadowLightIndices[slot] : -1; }
    float GetShadowFarPlane(int slot) const { return (slot >= 0 && slot < MAX_SHADOW_CASTING_POINT_LIGHTS) ? m_shadowFarPlanes[slot] : 0.f; }

    // Fill the point light shadow fields into an existing ShadowConstants struct
    void FillShadowConstants(ShadowConstants& outConsts) const;

    void MarkDirty() { m_needsRender = true; }

private:
    void CreateCubeArrayTexture();
    void CreateDSVs();
    void CreateSRV();
    void CreatePSO();

    void AssignShadowSlots(Scene* scene);
    void RenderCubeFace(ID3D12GraphicsCommandList* cmdList,
                        int lightIndex, int faceIndex,
                        const Vec3& lightPos, float farPlane,
                        ConstantBuffer* shadowCB,
                        const std::vector<MeshObject*>& sortedObjects,
                        uint32_t startInstance);

    Mat44 GetCubeFaceViewMatrix(const Vec3& lightPos, int faceIndex);
    Mat44 GetCubeFaceProjection(float nearPlane, float farPlane);

    ID3D12Device5* m_device = nullptr;
    ID3D12RootSignature* m_graphicsRootSignature = nullptr;
    ID3D12DescriptorHeap* m_cbvSrvDescHeap = nullptr;
    ID3D12DescriptorHeap* m_dsvDescHeap = nullptr;
    UINT m_descriptorSize = 0;

    ID3D12Resource* m_cubeArrayTexture = nullptr;
    ID3D12PipelineState* m_pointShadowPSO = nullptr;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_cubeFaceDsvHandles[POINT_SHADOW_TOTAL_FACES] = {};

    // Shadow assignment
    int m_shadowLightIndices[MAX_SHADOW_CASTING_POINT_LIGHTS] = {-1, -1, -1, -1};
    float m_shadowFarPlanes[MAX_SHADOW_CASTING_POINT_LIGHTS] = {0.f, 0.f, 0.f, 0.f};
    int m_numActiveShadowLights = 0;

    // Each face render needs a unique constant buffer index to avoid GPU data race.
    // Index 0 is reserved for the combined shadow constants upload.
    int m_shadowDrawCounter = 1;

    bool m_initialized = false;
    bool m_needsRender = true; // Set true on init and when lights/geometry change
};

#endif // ENGINE_DX12_RENDERER
