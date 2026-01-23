#pragma once
#include <d3d12.h>
struct GBufferData
{
    ID3D12Resource* m_albedo;
    ID3D12Resource* m_normal;
    ID3D12Resource* m_material;  // R:roughness G:metallic B:AO A:ObjectID
    ID3D12Resource* m_worldPos; //改了！

    ID3D12Resource* m_depth;
        
    ID3D12Resource* GetResource(int index)
    {
        switch(index)
        {
        case 0: return m_albedo;
        case 1: return m_normal;
        case 2: return m_material;
        case 3: return m_worldPos;
        default: return nullptr;
        }
    }
    D3D12_CLEAR_VALUE m_gBufferClearValues[4];
};
