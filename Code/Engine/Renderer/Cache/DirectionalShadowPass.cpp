#include "DirectionalShadowPass.h"

#ifdef ENGINE_DX12_RENDERER
#include "Engine/Renderer/DX12Renderer.hpp"
#include "Engine/Renderer/ConstantBuffer.hpp"
#include "Engine/Renderer/VertexBuffer.hpp"
#include "Engine/Renderer/IndexBuffer.hpp"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/Object/Mesh/MeshObject.h"
#include "Engine/Core/StaticMesh.h"
#include <d3dcompiler.h>
#include <algorithm>

DirectionalShadowPass::~DirectionalShadowPass()
{
    Shutdown();
}

void DirectionalShadowPass::Initialize(ID3D12Device5* device,
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

    CreateTexture();
    CreateDSV();
    CreateSRV();
    CreatePSO();

    m_initialized = true;
    DebuggerPrintf("[DirectionalShadowPass] Initialized\n");
}

void DirectionalShadowPass::CreateTexture()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = SHADOW_MAP_SIZE;
    desc.Height = SHADOW_MAP_SIZE;
    desc.DepthOrArraySize = 1;
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
        IID_PPV_ARGS(&m_shadowMapTexture)
    );
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create shadow map texture!");
    m_shadowMapTexture->SetName(L"ShadowMapTexture");
}

void DirectionalShadowPass::CreateDSV()
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    UINT dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // Index 1 in DSV heap (index 0 = main depth buffer)
    CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(
        m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        1,
        dsvDescriptorSize
    );
    m_device->CreateDepthStencilView(m_shadowMapTexture, &dsvDesc, shadowDsvHandle);
    m_shadowDsvHandle = shadowDsvHandle;
}

void DirectionalShadowPass::CreateSRV()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        m_cbvSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        SHADOW_MAP_SRV,
        m_descriptorSize
    );
    m_device->CreateShaderResourceView(m_shadowMapTexture, &srvDesc, srvHandle);
}

void DirectionalShadowPass::CreatePSO()
{
    std::string shaderSource;
    FileReadToString(shaderSource, "Data/Shaders/Shadow.hlsl");
    ID3DBlob* vs = nullptr;
    ID3DBlob* error = nullptr;
    HRESULT hr = D3DCompile(shaderSource.c_str(), shaderSource.size(), "Shadow.hlsl",
        nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VertexMain", "vs_5_1",
        0, 0, &vs, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            DebuggerPrintf("[DirectionalShadowPass] VS compile error: %s\n", (char*)error->GetBufferPointer());
            error->Release();
        }
        GUARANTEE_OR_DIE(false, "Failed to compile Shadow VS!");
    }

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
    psoDesc.PS = { nullptr, 0 };

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    psoDesc.RasterizerState.DepthBias = 5000;
    psoDesc.RasterizerState.DepthBiasClamp = 0.005f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;

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

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shadowMapPSO));
    GUARANTEE_OR_DIE(SUCCEEDED(hr), "Failed to create Shadow Map PSO!");
    m_shadowMapPSO->SetName(L"ShadowMapPSO");

    vs->Release();
    if (error) error->Release();
}

void DirectionalShadowPass::Execute(ID3D12GraphicsCommandList* cmdList,
                                     ConstantBuffer* shadowCB,
                                     DX12Renderer* renderer,
                                     Scene* scene)
{
    if (!m_initialized || !m_shadowMapTexture)
        return;

    Vec3 sunDir = scene->m_sunDirection.GetNormalized();
    AABB3 sceneBounds = scene->m_sceneBounds;
    Vec3 sceneCenter = (sceneBounds.m_maxs + sceneBounds.m_mins) * 0.5f;
    float sceneRadius = sceneBounds.GetBoundsSize().GetLength() * 0.5f;

    if (sceneRadius < 1.0f) sceneRadius = 100.0f;

    Vec3 lightPos = sceneCenter - sunDir * sceneRadius * 2.0f;

    Vec3 forward = (sceneCenter - lightPos).GetNormalized();
    float yaw = Atan2Degrees(forward.y, forward.x);
    float pitch = Atan2Degrees(-forward.z, sqrtf(forward.x * forward.x + forward.y * forward.y));
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

    // Snap to texel boundaries to prevent shadow flickering
    {
        Mat44 shadowMatrix = renderToClip * cameraToRender * worldToCamera;
        float texelSizeNDC = 2.0f / (float)SHADOW_MAP_SIZE;
        Vec4 origin = shadowMatrix.TransformHomogeneous3D(Vec4(0.f, 0.f, 0.f, 1.f));
        if (origin.w != 0.f)
        {
            origin.x /= origin.w;
            origin.y /= origin.w;
        }
        float snappedX = floorf(origin.x / texelSizeNDC) * texelSizeNDC;
        float snappedY = floorf(origin.y / texelSizeNDC) * texelSizeNDC;
        float offsetX = snappedX - origin.x;
        float offsetY = snappedY - origin.y;
        Mat44 snapOffset;
        snapOffset.SetTranslation3D(Vec3(offsetX, offsetY, 0.f));
        renderToClip = snapOffset * renderToClip;
    }

    m_cachedLightWorldToCamera = worldToCamera;
    m_cachedLightCameraToRender = cameraToRender;
    m_cachedLightRenderToClip = renderToClip;

    // Transition to DEPTH_WRITE
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMapTexture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->OMSetRenderTargets(0, nullptr, FALSE, &m_shadowDsvHandle);
    cmdList->ClearDepthStencilView(
        m_shadowDsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0,
        0, nullptr
    );

    D3D12_VIEWPORT viewport = {};
    viewport.Width = (float)SHADOW_MAP_SIZE;
    viewport.Height = (float)SHADOW_MAP_SIZE;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {};
    scissor.right = (LONG)SHADOW_MAP_SIZE;
    scissor.bottom = (LONG)SHADOW_MAP_SIZE;
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    cmdList->SetPipelineState(m_shadowMapPSO);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ShadowConstants shadowConsts = {};
    shadowConsts.LightWorldToCamera = worldToCamera;
    shadowConsts.LightCameraToRender = cameraToRender;
    shadowConsts.LightRenderToClip = renderToClip;
    shadowConsts.ShadowMapSize = (float)SHADOW_MAP_SIZE;
    shadowConsts.ShadowBias = 0.05f;
    shadowConsts.SoftnessFactor = 1.0f;
    shadowConsts.LightSize = 3.f;
    shadowConsts.FarPlane = 0.f;
    for (int i = 0; i < 4; i++)
    {
        shadowConsts.ShadowLightIndices[i] = -1;
        shadowConsts.ShadowFarPlanes[i] = 0.f;
    }
    shadowConsts.PointShadowBias = 0.005f;
    shadowConsts.PointShadowSoftness = 0.02f;
    shadowConsts.NumShadowCastingLights = 0;

    shadowCB->AppendData(&shadowConsts, sizeof(ShadowConstants), 0);

    D3D12_GPU_VIRTUAL_ADDRESS shadowCBAddr = shadowCB->m_dx12ConstantBuffer->GetGPUVirtualAddress() + shadowCB->m_offset;
    cmdList->SetGraphicsRootConstantBufferView(k_shadowConstantsSlot, shadowCBAddr);

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

    // Upload instance data (appends after any existing GBuffer instances)
    uint32_t startInstance = renderer->GetInstanceDataCount();
    for (MeshObject* obj : sortedObjects)
        renderer->AppendInstanceData(obj->m_cachedWorldMatrix, Rgba8::WHITE);

    // Bind instance buffer
    cmdList->SetGraphicsRootShaderResourceView(30, renderer->GetInstanceBufferGPUAddress());

    // Batched draw by StaticMesh*
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

    // Transition back to SRV
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_shadowMapTexture,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
}

void DirectionalShadowPass::Shutdown()
{
    if (!m_initialized)
        return;

    DX_SAFE_RELEASE(m_shadowMapTexture);
    DX_SAFE_RELEASE(m_shadowMapPSO);

    m_initialized = false;
    DebuggerPrintf("[DirectionalShadowPass] Shutdown\n");
}

#endif // ENGINE_DX12_RENDERER
