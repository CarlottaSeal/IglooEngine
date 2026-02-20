#include "PointLightShadowPass.h"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/VertexBuffer.hpp"
#include "Engine/Renderer/IndexBuffer.hpp"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Object/Light/LightObject.h"
#include "Engine/Scene/Object/Mesh/MeshObject.h"
#include "Engine/Core/StaticMesh.h"
#include <d3dcompiler.h>
#include <algorithm>

PointLightShadowPass::~PointLightShadowPass()
{
    Shutdown();
}

void PointLightShadowPass::Initialize(ID3D12Device5* device,
                                       ID3D12RootSignature* graphicsRootSignature,
                                       ID3D12DescriptorHeap* cbvSrvDescHeap,
                                       ID3D12DescriptorHeap* dsvDescHeap,
                                       UINT descriptorSize)
{
    if (m_initialized)
        return;

    m_device = device;
    m_graphicsRootSignature = graphicsRootSignature;
    m_cbvSrvDescHeap = cbvSrvDescHeap;
    m_dsvDescHeap = dsvDescHeap;
    m_descriptorSize = descriptorSize;

    CreateCubeArrayTexture();
    CreateDSVs();
    CreateSRV();
    CreatePSO();

    m_initialized = true;
    DebuggerPrintf("[PointLightShadowPass] Initialized\n");
}

void PointLightShadowPass::CreateCubeArrayTexture()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = POINT_LIGHT_SHADOW_MAP_SIZE;
    desc.Height = POINT_LIGHT_SHADOW_MAP_SIZE;
    desc.DepthOrArraySize = (UINT16)POINT_SHADOW_TOTAL_FACES; // 4 cubes * 6 faces = 24
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
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
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_cubeArrayTexture)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create point light cube shadow array texture!");
    m_cubeArrayTexture->SetName(L"PointLightCubeShadowArray");
}

void PointLightShadowPass::CreateDSVs()
{
    UINT dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // DSV heap layout: [0] = backbuffer depth, [1] = directional shadow, [2..25] = point shadow faces
    static const int POINT_SHADOW_DSV_START = 1 + SHADOW_MAP_DSV_COUNT; // = 2

    for (int i = 0; i < POINT_SHADOW_TOTAL_FACES; i++)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = (UINT)i;
        dsvDesc.Texture2DArray.ArraySize = 1;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(
            m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(),
            POINT_SHADOW_DSV_START + i,
            dsvDescriptorSize
        );

        m_device->CreateDepthStencilView(m_cubeArrayTexture, &dsvDesc, dsvHandle);
        m_cubeFaceDsvHandles[i] = dsvHandle;
    }
}

void PointLightShadowPass::CreateSRV()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCubeArray.MostDetailedMip = 0;
    srvDesc.TextureCubeArray.MipLevels = 1;
    srvDesc.TextureCubeArray.First2DArrayFace = 0;
    srvDesc.TextureCubeArray.NumCubes = MAX_SHADOW_CASTING_POINT_LIGHTS;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        POINT_SHADOW_CUBE_ARRAY_SRV,
        m_descriptorSize
    );
    m_device->CreateShaderResourceView(m_cubeArrayTexture, &srvDesc, srvHandle);
}

void PointLightShadowPass::CreatePSO()
{
    std::string shaderSource;
    FileReadToString(shaderSource, "Data/Shaders/PointLightShadow.hlsl");

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    ID3DBlob* error = nullptr;

    // Compile VS
    HRESULT hr = D3DCompile(shaderSource.c_str(), shaderSource.size(), "PointLightShadow.hlsl",
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VertexMain", "vs_5_1",
        0, 0, &vs, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[PointLightShadowPass] VS compile error: %s\n", (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to compile PointLightShadow VS!");
    }
    if (error) { error->Release(); error = nullptr; }

    // Compile PS
    hr = D3DCompile(shaderSource.c_str(), shaderSource.size(), "PointLightShadow.hlsl",
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PixelMain", "ps_5_1",
        0, 0, &ps, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[PointLightShadowPass] PS compile error: %s\n", (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to compile PointLightShadow PS!");
    }
    if (error) { error->Release(); error = nullptr; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_graphicsRootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;  // Back-face culling (cube face basis is left-handed, flipping winding vs directional pass)

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.SampleDesc.Count = 1;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pointShadowPSO));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Point Light Shadow PSO!");
    m_pointShadowPSO->SetName(L"PointLightShadowPSO");

    vs->Release();
    ps->Release();
}

void PointLightShadowPass::AssignShadowSlots(Scene* scene)
{
    m_numActiveShadowLights = 0;
    for (int i = 0; i < MAX_SHADOW_CASTING_POINT_LIGHTS; i++)
    {
        m_shadowLightIndices[i] = -1;
        m_shadowFarPlanes[i] = 0.f;
    }

    for (LightObject* light : scene->m_lightObjects)
    {
        if (!light) continue;
        if (light->GetLightType() != LIGHT_POINT) continue;
        if (m_numActiveShadowLights >= MAX_SHADOW_CASTING_POINT_LIGHTS) break;

        int lightID = light->GetGeneralLightID();
        if (lightID < 0) continue; // Skip lights not yet registered in LightsArray

        int slot = m_numActiveShadowLights;
        m_shadowLightIndices[slot] = lightID;
        m_shadowFarPlanes[slot] = light->GetOuterRadius();
        if (m_shadowFarPlanes[slot] <= 0.f)
            m_shadowFarPlanes[slot] = 25.f; // Default far plane
        m_numActiveShadowLights++;
    }
}

Mat44 PointLightShadowPass::GetCubeFaceViewMatrix(const Vec3& lightPos, int faceIndex)
{
    // Camera orientation for each cube face matching DX TextureCubeArray convention.
    // Camera convention: I = forward, J = left, K = up
    // These orientations ensure the rendered depth aligns with GPU cube map lookup
    // when sampling with world-space direction vectors.
    static const Vec3 faceForward[6] = {
        Vec3( 1.f, 0.f, 0.f),  // Face 0: +X
        Vec3(-1.f, 0.f, 0.f),  // Face 1: -X
        Vec3( 0.f, 1.f, 0.f),  // Face 2: +Y
        Vec3( 0.f,-1.f, 0.f),  // Face 3: -Y
        Vec3( 0.f, 0.f, 1.f),  // Face 4: +Z
        Vec3( 0.f, 0.f,-1.f),  // Face 5: -Z
    };
    static const Vec3 faceLeft[6] = {
        Vec3( 0.f, 0.f, 1.f),  // +X
        Vec3( 0.f, 0.f,-1.f),  // -X
        Vec3(-1.f, 0.f, 0.f),  // +Y
        Vec3(-1.f, 0.f, 0.f),  // -Y
        Vec3(-1.f, 0.f, 0.f),  // +Z
        Vec3( 1.f, 0.f, 0.f),  // -Z
    };
    static const Vec3 faceUp[6] = {
        Vec3( 0.f, 1.f, 0.f),  // +X: tc=-y, NDC.y=y/x
        Vec3( 0.f, 1.f, 0.f),  // -X: tc=-y, NDC.y=y/(-x)
        Vec3( 0.f, 0.f,-1.f),  // +Y: tc=+z, NDC.y=-z/y
        Vec3( 0.f, 0.f, 1.f),  // -Y: tc=-z, NDC.y=z/(-y)
        Vec3( 0.f, 1.f, 0.f),  // +Z: tc=-y, NDC.y=y/z
        Vec3( 0.f, 1.f, 0.f),  // -Z: tc=-y, NDC.y=y/(-z)
    };

    Mat44 cameraToWorld;
    cameraToWorld.SetIJKT3D(faceForward[faceIndex], faceLeft[faceIndex], faceUp[faceIndex], lightPos);
    return cameraToWorld.GetOrthonormalInverse();
}

Mat44 PointLightShadowPass::GetCubeFaceProjection(float nearPlane, float farPlane)
{
    // 90-degree FOV, 1:1 aspect ratio for cube face
    return Mat44::MakePerspectiveProjection(90.f, 1.f, nearPlane, farPlane);
}

void PointLightShadowPass::RenderCubeFace(ID3D12GraphicsCommandList* cmdList,
                                            int lightIndex, int faceIndex,
                                            const Vec3& lightPos, float farPlane,
                                            ConstantBuffer* shadowCB,
                                            const std::vector<MeshObject*>& sortedObjects,
                                            uint32_t startInstance)
{
    int sliceIndex = lightIndex * POINT_SHADOW_CUBE_FACES + faceIndex;

    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &m_cubeFaceDsvHandles[sliceIndex]);
    cmdList->ClearDepthStencilView(
        m_cubeFaceDsvHandles[sliceIndex],
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0,
        0, nullptr
    );

    Mat44 worldToCamera = GetCubeFaceViewMatrix(lightPos, faceIndex);
    Mat44 cameraToRender;
    cameraToRender.SetIJK3D(Vec3(0.f, 0.f, 1.f), Vec3(-1.f, 0.f, 0.f), Vec3(0.f, 1.f, 0.f));
    Mat44 renderToClip = GetCubeFaceProjection(0.1f, farPlane);

    // Upload shadow constants for this face
    ShadowConstants shadowConsts = {};
    shadowConsts.LightWorldToCamera = worldToCamera;
    shadowConsts.LightCameraToRender = cameraToRender;
    shadowConsts.LightRenderToClip = renderToClip;
    shadowConsts.ShadowMapSize = (float)POINT_LIGHT_SHADOW_MAP_SIZE;
    shadowConsts.LightPosition[0] = lightPos.x;
    shadowConsts.LightPosition[1] = lightPos.y;
    shadowConsts.LightPosition[2] = lightPos.z;
    shadowConsts.FarPlane = farPlane;

    shadowCB->AppendData(&shadowConsts, sizeof(ShadowConstants), m_shadowDrawCounter);
    m_shadowDrawCounter++;
    D3D12_GPU_VIRTUAL_ADDRESS shadowCBAddr = shadowCB->m_dx12ConstantBuffer->GetGPUVirtualAddress() + shadowCB->m_offset;
    cmdList->SetGraphicsRootConstantBufferView(k_shadowConstantsSlot, shadowCBAddr);

    // Batched draw by StaticMesh* (instance data already uploaded, shared across all faces)
    uint32_t i = 0;
    while (i < (uint32_t)sortedObjects.size())
    {
        StaticMesh* batchMesh = sortedObjects[i]->GetMesh();
        uint32_t batchEnd = i + 1;
        while (batchEnd < (uint32_t)sortedObjects.size() && sortedObjects[batchEnd]->GetMesh() == batchMesh)
            ++batchEnd;

        cmdList->IASetVertexBuffers(0, 1, &batchMesh->m_vertexBuffer->m_vertexBufferView);
        cmdList->IASetIndexBuffer(&batchMesh->m_indexBuffer->m_indexBufferView);
        cmdList->SetGraphicsRoot32BitConstant(31, startInstance + i, 0);
        cmdList->DrawIndexedInstanced((UINT)batchMesh->m_indices.size(), batchEnd - i, 0, 0, 0);
        i = batchEnd;
    }
}

void PointLightShadowPass::FillShadowConstants(ShadowConstants& outConsts) const
{
    for (int i = 0; i < MAX_SHADOW_CASTING_POINT_LIGHTS; i++)
    {
        outConsts.ShadowLightIndices[i] = m_shadowLightIndices[i];
        outConsts.ShadowFarPlanes[i] = m_shadowFarPlanes[i];
    }
    outConsts.PointShadowBias = 0.005f;
    outConsts.PointShadowSoftness = 0.02f;
    outConsts.NumShadowCastingLights = m_numActiveShadowLights;
}

void PointLightShadowPass::Execute(ID3D12GraphicsCommandList* cmdList,
                                    ConstantBuffer* shadowCB,
                                    DX12Renderer* renderer,
                                    Scene* scene)
{
    if (!m_initialized || !m_cubeArrayTexture)
        return;

    AssignShadowSlots(scene);

    if (m_numActiveShadowLights == 0)
        return;

    // Skip rendering if shadow maps are already up-to-date
    if (!m_needsRender)
        return;

    m_needsRender = false;

    // Reset shadow draw counter. Index 0 is reserved for combined constants upload.
    m_shadowDrawCounter = 1;

    // Collect and sort mesh objects by StaticMesh* for instanced batching
    std::vector<MeshObject*> sortedObjects;
    sortedObjects.reserve(scene->m_meshObjects.size());
    for (MeshObject* obj : scene->m_meshObjects)
    {
        if (obj) sortedObjects.push_back(obj);
    }
    std::sort(sortedObjects.begin(), sortedObjects.end(), [](MeshObject* a, MeshObject* b) {
        return a->GetMesh() < b->GetMesh();
    });

    // Upload instance data once (shared across all faces and lights)
    uint32_t startInstance = renderer->GetInstanceDataCount();
    for (MeshObject* obj : sortedObjects)
        renderer->AppendInstanceData(obj->m_cachedWorldMatrix, Rgba8::WHITE);

    // Transition entire cube array to DEPTH_WRITE
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_cubeArrayTexture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    cmdList->ResourceBarrier(1, &barrier);

    // Set viewport and scissor for all faces
    D3D12_VIEWPORT viewport = {};
    viewport.Width = (float)POINT_LIGHT_SHADOW_MAP_SIZE;
    viewport.Height = (float)POINT_LIGHT_SHADOW_MAP_SIZE;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {};
    scissor.right = (LONG)POINT_LIGHT_SHADOW_MAP_SIZE;
    scissor.bottom = (LONG)POINT_LIGHT_SHADOW_MAP_SIZE;
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(m_pointShadowPSO);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind instance buffer once for all faces
    cmdList->SetGraphicsRootShaderResourceView(30, renderer->GetInstanceBufferGPUAddress());

    // Render each active light's 6 cube faces
    for (int lightSlot = 0; lightSlot < m_numActiveShadowLights; lightSlot++)
    {
        int generalLightID = m_shadowLightIndices[lightSlot];
        if (generalLightID < 0) continue;
        if (generalLightID >= (int)scene->m_worldLightPositions.size()) continue;

        Vec3 lightPos = scene->m_worldLightPositions[generalLightID];
        float farPlane = m_shadowFarPlanes[lightSlot];

        for (int face = 0; face < POINT_SHADOW_CUBE_FACES; face++)
        {
            RenderCubeFace(cmdList, lightSlot, face, lightPos, farPlane,
                           shadowCB, sortedObjects, startInstance);
        }
    }

    // Transition back to SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_cubeArrayTexture,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
}

void PointLightShadowPass::Shutdown()
{
    if (!m_initialized)
        return;

    DX_SAFE_RELEASE(m_cubeArrayTexture);
    DX_SAFE_RELEASE(m_pointShadowPSO);

    m_initialized = false;
    DebuggerPrintf("[PointLightShadowPass] Shutdown\n");
}

#endif // ENGINE_DX12_RENDERER
