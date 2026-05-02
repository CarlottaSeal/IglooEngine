#pragma once

#include "Engine/Math/AABB3.hpp"
#include "Engine/Renderer/SDFTexture3D.h"

// struct MeshSDFData
// {
//     SDFTexture3D* m_sdfTexture;
//     //AABB3 m_worldBounds;                      
//     //float m_maxDistance = 1.0f;               
//     uint32_t m_meshIndex = 0;                 
// };
constexpr int MAX_INSTANCES = 256; //in scene
constexpr uint32_t GLOBAL_SDF_RESOLUTION = 64;

struct VoxelSceneConstants
{
    Vec3 SceneBoundsMin;
    uint32_t VoxelResolution;
    Vec3 SceneBoundsMax;
    uint32_t InstanceCount;
    
    Vec3 VoxelSize;
    float SDFThreshold;
    
    uint32_t MaxTraceSteps;
    float MaxTraceDistance;
    uint32_t CardCount;
    
    uint32_t padding[1];

    float AtlasWidth;            // offset 64
    float AtlasHeight;           // offset 68
    float _padding2[2];  
};

struct VoxelVisibilityGPU
{
    uint32_t HitInstanceIndex;  // 0xFFFFFFFF = 未命中
    float HitDistance;
};

struct MeshSDFInfoGPU
{
    Mat44    WorldToLocal;
    Mat44    LocalToWorld;
    Vec3     LocalBoundsMin;
    float    LocalToWorldScale;
    Vec3     LocalBoundsMax;
    uint32_t SDFTextureIndex;
    Vec3     WorldBoundsMin;     // Pre-computed world AABB for fast ray reject
    uint32_t CardStartIndex;
    Vec3     WorldBoundsMax;
    uint32_t CardCount;
};