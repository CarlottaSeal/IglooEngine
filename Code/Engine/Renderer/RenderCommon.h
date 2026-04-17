#pragma once
#include "Engine/Math/AABB3.hpp"
#include "Engine/Renderer/Cache/CacheCommon.h"

extern const char* m_shaderSource;

#define DX_SAFE_RELEASE(dxObject)\
{\
if ((dxObject) != nullptr)\
{\
(dxObject)->Release();\
(dxObject) = nullptr;\
}\
}\

#include "Engine/Window/Window.hpp"
#include "Engine/Math/IntVec4.h"

extern Window* g_theWindow;

 //#if defined(ENGINE_DEBUG_RENDER)
 //void* m_dxgiDebug = nullptr;
 //void* m_dxgiDebugModule = nullptr;
 //#endif

#if defined(OPAQUE)
#undef OPAQUE
#endif

enum class BlendMode
{
    ALPHA,
    ADDITIVE,
    OPAQUE,
    COUNT
};

enum class SamplerMode
{
    POINT_CLAMP,
    BILINEAR_WRAP,
    COUNT
};

enum class RasterizerMode
{
    SOLID_CULL_NONE,
    SOLID_CULL_BACK,
    WIREFRAME_CULL_NONE,
    WIREFRAME_CULL_BACK,
    COUNT
};

enum class DepthMode
{
    DISABLED,
    READ_ONLY_ALWAYS,
    READ_ONLY_LESS_EQUAL,
    READ_WRITE_LESS_EQUAL,
    COUNT
};

enum class VertexType
{
    VERTEX_PCU,
    VERTEX_PCUTBN,
    VERTEX_FONT,
    COUNT
};

struct FontConstants
{
    float SDFThreshold;		// alpha threshold for SDF edge
    float SDFSmoothRange;	// smoothstep range for anti-aliasing
    float Time;				// for animated effects
    float Weight;			// global weight adjustment
};

#ifdef ENGINE_DX11_RENDERER
static const int k_perFrameConstantsSlot = 1;
static const int k_cameraConstantsSlot = 2;
static const int k_modelConstantsSlot = 3;
static const int k_generalLightConstantsSlot = 4;
static const int k_shadowConstantsSlot = 6;
static const int k_fontConstantsSlot = 7;
#endif
#ifdef ENGINE_DX12_RENDERER
static const int k_perFrameConstantsSlot = 0;
static const int k_cameraConstantsSlot = 1;
static const int k_modelConstantsSlot = 2;
static const int k_materialConstantsSlot = 3;
static const int k_generalLightConstantsSlot = 4;
static const int k_shadowConstantsSlot = 5;
static const int k_surfaceRadiosityConstantsSlot = 6;       
static const int k_screenProbeFinalGatherConstantsSlot = 7;
//static const int k_sdfGenerationConstantsSlot = 11;
static const int k_giVisualizationConstantsSlot = 11;
static const int k_cardCaptureConstantsSlot = 10;
static const int k_cardCaptureModelConstantsSlot = 9;
static const int k_cardCaptureMaterialConstantsSlot = 8;
static const int k_compositeConstantsSlot = 12;
#endif

struct CameraConstants
{
    Mat44 WorldToCameraTransform;
    Mat44 CameraToRenderTransform;
    Mat44 RenderToClipTransform;

    Vec3 CameraWorldPosition;
    float padding;
};

struct ModelConstants
{
    Mat44 ModelToWorldTransform;
    float ModelColor[4];
};

struct InstanceData
{
    Mat44 ModelToWorldTransform;
    float ModelColor[4];
};

#ifdef ENGINE_PAST_VERSION_LIGHTS
struct LightConstants //DirectionalLight
{
    float SunDirection[3];
    float SunIntensity;
    //float AmbientIntensity[4];
    float AmbientIntensity;
    float AmbientLightColor[3];
};
static const int k_lightConstantsSlot = 7;

struct PointLightConstants
{
    float PointLightPosition[3];
    float LightIntensity = 0.f;
    float LightColor[3];
    float LightRange;
};
static const int k_pointLightConstantsSlot = 4;

struct SpotLightConstants
{
    Vec3 SpotLightPosition;
    float SpotLightCutOff; // cos(45°)，表示光锥半角
    Vec3 SpotLightDirection; // Normalized
    float pad0;
    float SpotLightColor[3];
    float pad1;
};
static const int k_spotLightConstantsSlot = 5;
#endif

static const int s_maxLights = 15;
struct GeneralLight
{
    float Color[4]          =  {0.f, 0.f, 0.f, 0.f};
    float WorldPosition[3]  = {0.f, 0.f, 0.f};
    int LightType = 0; //不放进Constant里

    float SpotForward[3]    = {0.f, 0.f, 0.f};
    float Ambience          = 0.f;
    float InnerRadius       = 0.f;
    float OuterRadius       = 0.f;
    float InnerDotThreshold = -1.f;
    float OuterDotThreshold = -1.f;
};
struct GeneralLightConstants
{
    float SunColor[4];
    float SunNormal[3];
    int NumLights;
    GeneralLight LightsArray[s_maxLights];
};

struct ShadowConstants
{
    // Directional shadow
    Mat44 LightWorldToCamera;
    Mat44 LightCameraToRender;
    Mat44 LightRenderToClip;
    float ShadowMapSize;
    float ShadowBias;
    float SoftnessFactor;
    float LightSize;

    // Point light cube shadow (per-face rendering)
    float LightPosition[3];
    float FarPlane;

    // Point light shadow sampling info
    int ShadowLightIndices[4];    // generalLightID per shadow slot, -1 = unused
    float ShadowFarPlanes[4];     // far plane per shadow slot
    float PointShadowBias;
    float PointShadowSoftness;
    int NumShadowCastingLights;
    float PointShadowPadding;
};

struct PerFrameConstants 
{
    float Time;
    int DebugInt;
    float DebugFloat;
    float EMPTY_PADDING;
};

struct MaterialConstants
{
    int DiffuseId = 0;
    int NormalId = 1;
    int SpecularId = 2;

    float EMPTY_PADDING;
};

enum PrimitiveTopology
{
    PRIMITIVE_TRIANGLES,
    PRIMITIVE_LINES,
    PRIMITIVE_LINE_STRIP,
    PRIMITIVE_POINTS
};

enum class RenderMode
{
    FORWARD,  
    GI,
    UNKNOWN
};

// struct PassModeConstants
// {
//     uint32_t RenderMode;
//     uint32_t ObjectID;
//     float padding[2];
// };

static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
static constexpr uint32_t POINT_LIGHT_SHADOW_MAP_SIZE = 512;
static constexpr int MAX_SHADOW_CASTING_POINT_LIGHTS = 4;
static constexpr int POINT_SHADOW_CUBE_FACES = 6;
static constexpr int POINT_SHADOW_TOTAL_FACES = MAX_SHADOW_CASTING_POINT_LIGHTS * POINT_SHADOW_CUBE_FACES; // 24

static const int FRAME_BUFFER_COUNT = 3;  // Swap Chain 三重缓冲
static const int MAX_TEXTURE_COUNT = 200;
static const unsigned int NUM_CONSTANT_BUFFERS = 14;
static constexpr int GBUFFER_COUNT = 4;
static constexpr int DEPTH_SRV_COUNT = 1;
static constexpr int SURFACE_CACHE_DESCRIPTOR_COUNT = 4;

// SurfaceCache 配置
//static constexpr int SURFACE_CACHE_BUFFER_COUNT = 1;        // 双缓冲 ->改为无缓冲！ 删掉这个
static constexpr int SURFACE_CACHE_LAYER_CAPTURE_COUNT = 4;
static constexpr int DESCRIPTORS_PER_SURFACE_BUFFER = 4;    // Atlas SRV, Meta SRV, Atlas UAV, Meta UAV

static constexpr int SHADOW_MAP_DSV_COUNT = 1;
static constexpr int POINT_SHADOW_DSV_COUNT = POINT_SHADOW_TOTAL_FACES; // 24
static constexpr int CARD_CAPTURE_RTV_COUNT = SURFACE_CACHE_LAYER_COUNT;
// SDF
static const int MAX_SDF_TEXTURE_COUNT = 128;

// Descriptor Heap Physical Layout
static constexpr int CBV_START = 0;                         // Physical slot 0
static constexpr int TEXTURE_SRV_START = NUM_CONSTANT_BUFFERS;  // Physical slot 14

static constexpr int GBUFFER_SRV_START_SLOT = TEXTURE_SRV_START + MAX_TEXTURE_COUNT;  // 214
static constexpr int DEPTH_SRV_START_SLOT = GBUFFER_SRV_START_SLOT + GBUFFER_COUNT;   // 218

// Section 4: SurfaceCache PRIMARY 
static constexpr int SURFCACHE_PRIMARY_BASE = DEPTH_SRV_START_SLOT + DEPTH_SRV_COUNT; // 219

static constexpr int SURFCACHE_PRIMARY_ATLAS_SRV = SURFCACHE_PRIMARY_BASE + 0;   // 219
static constexpr int SURFCACHE_PRIMARY_META_SRV  = SURFCACHE_PRIMARY_BASE + 1;   // 220
static constexpr int SURFCACHE_PRIMARY_ATLAS_UAV = SURFCACHE_PRIMARY_BASE + 2;   // 221
static constexpr int SURFCACHE_PRIMARY_META_UAV  = SURFCACHE_PRIMARY_BASE + 3;   // 222

// Section 7: Card BVH
static constexpr int CARD_BVH_BASE = SURFCACHE_PRIMARY_META_UAV + 1 + 5; // 228
static constexpr int CARD_BVH_NODE_SRV = CARD_BVH_BASE + 0;   
static constexpr int CARD_BVH_INDEX_SRV = CARD_BVH_BASE + 1;  

// Section 8: Per-Frame Resources 
static constexpr int PER_FRAME_BASE = CARD_BVH_INDEX_SRV + 1;  // 230

static constexpr int PROBE_GRID_SRV_BASE = PER_FRAME_BASE;                                  
static constexpr int SSR_SRV_BASE = PROBE_GRID_SRV_BASE + FRAME_BUFFER_COUNT;              
static constexpr int TEMPORAL_PREV_SRV_BASE = SSR_SRV_BASE + FRAME_BUFFER_COUNT;           
static constexpr int TEMPORAL_MOTION_SRV_BASE = TEMPORAL_PREV_SRV_BASE + FRAME_BUFFER_COUNT;

// Section 9: SDF Generation 
static constexpr int SDF_GEN_BASE = TEMPORAL_MOTION_SRV_BASE + FRAME_BUFFER_COUNT;  // 242
static constexpr int SDF_GEN_VERTEX_SRV = SDF_GEN_BASE + 0;   
static constexpr int SDF_GEN_INDEX_SRV = SDF_GEN_BASE + 1;    
static constexpr int SDF_GEN_BVH_NODE_SRV = SDF_GEN_BASE + 2; 
static constexpr int SDF_GEN_BVH_TRI_SRV = SDF_GEN_BASE + 3;  
static constexpr int SDF_GEN_OUTPUT_UAV = SDF_GEN_BASE + 4;   

// Section 10: SDF Textures
static constexpr int SDF_TEXTURE_SRV_BASE = SDF_GEN_OUTPUT_UAV + 1;  // 247

// Section 11: VoxelScene Pass Descriptors
static constexpr int VOXEL_SCENE_BASE = SDF_TEXTURE_SRV_BASE + MAX_SDF_TEXTURE_COUNT;  // 375
static constexpr int GLOBAL_SDF_UAV = VOXEL_SCENE_BASE + 0;       
static constexpr int VOXEL_LIGHTING_UAV = VOXEL_SCENE_BASE + 1;   
static constexpr int VISIBILITY_UAV = VOXEL_SCENE_BASE + 2;       

static constexpr int GLOBAL_SDF_SRV = VOXEL_SCENE_BASE + 3;        //378
static constexpr int VOXEL_LIGHTING_SRV = VOXEL_SCENE_BASE + 4;    // 379 
static constexpr int INSTANCE_INFO_SRV = VOXEL_SCENE_BASE + 5;     // 380 
static constexpr int SURFACE_ATLAS_SRV = VOXEL_SCENE_BASE + 6;     // 381 
static constexpr int CARD_METADATA_SRV = VOXEL_SCENE_BASE + 7;     // 382
static constexpr int VISIBILITY_SRV = VOXEL_SCENE_BASE + 8;        // 383
static constexpr int VOXEL_SCENE_DESCRIPTOR_COUNT = 9;         

static constexpr int SHADOW_MAP_SRV = VOXEL_SCENE_BASE + VOXEL_SCENE_DESCRIPTOR_COUNT;

//Surface Radiosity
static constexpr int RADIOSITY_BASE = SHADOW_MAP_SRV + 1;  // 385

// UAVs 连续排列 (385-392)
static constexpr int RADIOSITY_UAV_BASE = RADIOSITY_BASE;
static constexpr int RADIOSITY_TRACE_RESULT_UAV   = RADIOSITY_UAV_BASE + 0;   // 385
static constexpr int RADIOSITY_HISTORY_UAV        = RADIOSITY_UAV_BASE + 1;   // 385
static constexpr int RADIOSITY_FILTERED_UAV       = RADIOSITY_UAV_BASE + 2;   // 386
static constexpr int RADIOSITY_SH_R_UAV           = RADIOSITY_UAV_BASE + 3;   // 387
static constexpr int RADIOSITY_SH_G_UAV           = RADIOSITY_UAV_BASE + 4;   // 388
static constexpr int RADIOSITY_SH_B_UAV           = RADIOSITY_UAV_BASE + 5;   // 389
static constexpr int RADIOSITY_PROBE_DEPTH_UAV    = RADIOSITY_UAV_BASE + 6;   // 390
static constexpr int RADIOSITY_PROBE_NORMAL_UAV   = RADIOSITY_UAV_BASE + 7;   // 391
static constexpr int RADIOSITY_UAV_COUNT = 8;

// SRVs 连续排列 (393-400)
static constexpr int RADIOSITY_SRV_BASE = RADIOSITY_UAV_BASE + RADIOSITY_UAV_COUNT;  // 393 
static constexpr int RADIOSITY_TRACE_RESULT_SRV   = RADIOSITY_SRV_BASE + 0;   
static constexpr int RADIOSITY_HISTORY_SRV        = RADIOSITY_SRV_BASE + 1;   
static constexpr int RADIOSITY_FILTERED_SRV       = RADIOSITY_SRV_BASE + 2;   
static constexpr int RADIOSITY_SH_R_SRV           = RADIOSITY_SRV_BASE + 3;   
static constexpr int RADIOSITY_SH_G_SRV           = RADIOSITY_SRV_BASE + 4;   
static constexpr int RADIOSITY_SH_B_SRV           = RADIOSITY_SRV_BASE + 5;   
static constexpr int RADIOSITY_PROBE_DEPTH_SRV    = RADIOSITY_SRV_BASE + 6;   
static constexpr int RADIOSITY_PROBE_NORMAL_SRV   = RADIOSITY_SRV_BASE + 7;   
static constexpr int RADIOSITY_SRV_COUNT = 8;

static constexpr int RADIOSITY_DESCRIPTOR_COUNT = RADIOSITY_UAV_COUNT + RADIOSITY_SRV_COUNT;  // 16

// Screen Probe
static constexpr int SCREEN_PROBE_BASE = RADIOSITY_BASE + RADIOSITY_DESCRIPTOR_COUNT;  // 401

// UAVs 连续排列 (401-415)
static constexpr int SCREEN_PROBE_UAV_BASE = SCREEN_PROBE_BASE;
static constexpr int SCREEN_PROBE_BUFFER_UAV              = SCREEN_PROBE_UAV_BASE + 0;   
static constexpr int SCREEN_PROBE_BRDF_PDF_UAV            = SCREEN_PROBE_UAV_BASE + 1;   
static constexpr int SCREEN_PROBE_LIGHTING_PDF_UAV        = SCREEN_PROBE_UAV_BASE + 2;   
static constexpr int PREV_SCREEN_RADIANCE_UAV             = SCREEN_PROBE_UAV_BASE + 3;   
static constexpr int SCREEN_PROBE_SAMPLE_DIRECTIONS_UAV   = SCREEN_PROBE_UAV_BASE + 4;   
static constexpr int SCREEN_PROBE_MESH_TRACE_UAV          = SCREEN_PROBE_UAV_BASE + 5;   
static constexpr int SCREEN_PROBE_VOXEL_TRACE_UAV         = SCREEN_PROBE_UAV_BASE + 6;   
static constexpr int SCREEN_PROBE_RADIANCE_UAV            = SCREEN_PROBE_UAV_BASE + 7;   
static constexpr int SCREEN_PROBE_RADIANCE_HISTORY_UAV    = SCREEN_PROBE_UAV_BASE + 8;   
static constexpr int SCREEN_PROBE_RADIANCE_FILTERED_UAV   = SCREEN_PROBE_UAV_BASE + 9;   
static constexpr int SCREEN_PROBE_OCT_SH_R_UAV            = SCREEN_PROBE_UAV_BASE + 10;  
static constexpr int SCREEN_PROBE_OCT_SH_G_UAV            = SCREEN_PROBE_UAV_BASE + 11;  
static constexpr int SCREEN_PROBE_OCT_SH_B_UAV            = SCREEN_PROBE_UAV_BASE + 12;  
static constexpr int SCREEN_INDIRECT_LIGHTING_UAV         = SCREEN_PROBE_UAV_BASE + 13;
static constexpr int SCREEN_PROBE_RADIANCE_HISTORY_B_UAV  = SCREEN_PROBE_UAV_BASE + 14;
static constexpr int SCREEN_PROBE_UAV_COUNT = 15;

// SRVs 连续排列 (415-429)
static constexpr int SCREEN_PROBE_SRV_BASE = SCREEN_PROBE_UAV_BASE + SCREEN_PROBE_UAV_COUNT;  // 416
static constexpr int SCREEN_PROBE_BUFFER_SRV              = SCREEN_PROBE_SRV_BASE + 0;   
static constexpr int SCREEN_PROBE_BRDF_PDF_SRV            = SCREEN_PROBE_SRV_BASE + 1;   
static constexpr int SCREEN_PROBE_LIGHTING_PDF_SRV        = SCREEN_PROBE_SRV_BASE + 2;   
static constexpr int PREV_SCREEN_RADIANCE_SRV             = SCREEN_PROBE_SRV_BASE + 3;   
static constexpr int PREV_DEPTH_BUFFER_SRV                = SCREEN_PROBE_SRV_BASE + 4;   
static constexpr int SCREEN_PROBE_SAMPLE_DIRECTIONS_SRV   = SCREEN_PROBE_SRV_BASE + 5;   
static constexpr int SCREEN_PROBE_MESH_TRACE_SRV          = SCREEN_PROBE_SRV_BASE + 6;   
static constexpr int SCREEN_PROBE_VOXEL_TRACE_SRV         = SCREEN_PROBE_SRV_BASE + 7;   
static constexpr int SCREEN_PROBE_RADIANCE_SRV            = SCREEN_PROBE_SRV_BASE + 8;   
static constexpr int SCREEN_PROBE_RADIANCE_HISTORY_SRV    = SCREEN_PROBE_SRV_BASE + 9;   
static constexpr int SCREEN_PROBE_RADIANCE_FILTERED_SRV   = SCREEN_PROBE_SRV_BASE + 10;  
static constexpr int SCREEN_PROBE_OCT_SH_R_SRV            = SCREEN_PROBE_SRV_BASE + 11;  
static constexpr int SCREEN_PROBE_OCT_SH_G_SRV            = SCREEN_PROBE_SRV_BASE + 12;  
static constexpr int SCREEN_PROBE_OCT_SH_B_SRV            = SCREEN_PROBE_SRV_BASE + 13;  
static constexpr int SCREEN_INDIRECT_LIGHTING_SRV         = SCREEN_PROBE_SRV_BASE + 14;
static constexpr int SCREEN_PROBE_RADIANCE_HISTORY_B_SRV = SCREEN_PROBE_SRV_BASE + 15;  // 431
static constexpr int SCREEN_PROBE_SRV_COUNT = 16;

static constexpr int SCREEN_PROBE_DESCRIPTOR_COUNT = SCREEN_PROBE_UAV_COUNT + SCREEN_PROBE_SRV_COUNT;  // 31
static constexpr int VIZ_OUTPUT_UAV = SCREEN_PROBE_BASE + SCREEN_PROBE_DESCRIPTOR_COUNT;  // 432

// Screen Indirect Raw (FinalGather 原始输出，时间滤波前)
static constexpr int SCREEN_INDIRECT_RAW_UAV = VIZ_OUTPUT_UAV + 1;  // 433
static constexpr int SCREEN_INDIRECT_RAW_SRV = SCREEN_INDIRECT_RAW_UAV + 1;  // 434

// Point Light Cube Shadow Array SRV
static constexpr int POINT_SHADOW_CUBE_ARRAY_SRV = SCREEN_INDIRECT_RAW_SRV + 1;  // 435

// Card index lookup texture (tile-granularity atlas → card index map)
static constexpr int CARD_INDEX_LOOKUP_SRV = POINT_SHADOW_CUBE_ARRAY_SRV + 1;  // 436

// Instance data uses root SRV (no descriptor needed)
static constexpr int TOTAL_NUM_DESCRIPTORS = CARD_INDEX_LOOKUP_SRV + 1;  // 437

// Shader Register Bindings (for Root Signature)
static constexpr int GBUFFER_SRV_START = MAX_TEXTURE_COUNT;  // shader registers: t200, t201, t202, 203 204 <-其实没必要
static constexpr int SURFACE_CACHE_SRV = GBUFFER_SRV_START + GBUFFER_COUNT+ DEPTH_SRV_COUNT;

// 其他逐帧资源的 shader register 映射
static constexpr int PROBE_GRID_SRV_INDEX = PROBE_GRID_SRV_BASE;
static constexpr int SSR_SRV_INDEX = SSR_SRV_BASE;
static constexpr int TEMPORAL_PREV_SRV_INDEX = TEMPORAL_PREV_SRV_BASE;
static constexpr int TEMPORAL_MOTION_SRV_INDEX = TEMPORAL_MOTION_SRV_BASE;
static constexpr int SHADOW_MAP_SRV_INDEX = TEMPORAL_MOTION_SRV_INDEX + 1; 
static constexpr int SCREEN_INDIRECT_LIGHTING_SRV_REGISTER = SHADOW_MAP_SRV_INDEX + 1;  // t241
static constexpr int POINT_SHADOW_SRV_REGISTER = SCREEN_INDIRECT_LIGHTING_SRV_REGISTER + 1;  // t242
static constexpr int INSTANCE_DATA_SRV_REGISTER = POINT_SHADOW_SRV_REGISTER + 1;  // t243

constexpr int PrimaryAtlasSrvIndex(int bufferIndex) 
{ 
    return SURFCACHE_PRIMARY_BASE + bufferIndex * DESCRIPTORS_PER_SURFACE_BUFFER + 0; 
}
constexpr int PrimaryMetaSrvIndex(int bufferIndex) 
{ 
    return SURFCACHE_PRIMARY_BASE + bufferIndex * DESCRIPTORS_PER_SURFACE_BUFFER + 1; 
}
constexpr int PrimaryAtlasUavIndex(int bufferIndex) 
{ 
    return SURFCACHE_PRIMARY_BASE + bufferIndex * DESCRIPTORS_PER_SURFACE_BUFFER + 2; 
}
constexpr int PrimaryMetaUavIndex(int bufferIndex) 
{ 
    return SURFCACHE_PRIMARY_BASE + bufferIndex * DESCRIPTORS_PER_SURFACE_BUFFER + 3; 
}

static constexpr int s_maxCardsPerBatch = 128;
static constexpr int s_maxProbesPerUpdate = 256;
static constexpr int MAX_CARD_SIZE = 512;

struct SurfaceCacheConstants //整个都用bu上了
{
    float ScreenWidth;
    float ScreenHeight;
    float AtlasWidth;
    float AtlasHeight;

    uint32_t TileSize;  //Default Card Resolution
    uint32_t TilesPerRow; //这个可以优化掉，之后如果有还要往这里加的东西
    uint32_t CurrentFrame;
    uint32_t ActiveCardCount;  

    Mat44 ViewProj;
    Mat44 ViewProjInverse;
    Mat44 PrevViewProj;

    Vec3 CameraPosition;
    float TemporalBlend;

    //IntVec4 DirtyCardIndices[s_maxCardsPerBatch / 4];
};

struct SDFGenerationConstants
{
	Vec3 BoundsMin;
	uint32_t Resolution;
	Vec3 BoundsMax;
	uint32_t NumTriangles;
    uint32_t NumVertices;
    uint32_t NumBVHNodes;
    uint32_t ZSliceOffset;
    float padding;
};

struct CardCaptureConstants
{
    //Mat44 CardViewMatrix;        // Card的view matrix
    //Mat44 CardProjectionMatrix;  // Card的正交投影

    Vec3 CardOrigin;             // Card在世界空间的原点
    float padding0;
    Vec3 CardAxisX;              // Card的X轴（世界空间）
    float padding1;
    Vec3 CardAxisY;              // Card的Y轴（世界空间）
    float padding2;
    Vec3 CardNormal;             // Card的法线（世界空间）
    float CaptureDepth;         // 当前捕获深度

    Vec2 CardSize;               // Card的世界空间尺寸
    uint32_t CaptureDirection;   // 当前捕获方向 (0-5)
    uint32_t Resolution;         // Card分辨率

    // LightMask支持（从CardInstanceData传入）
    uint32_t LightMask[4];       // 支持128个lights (4 * 32 bits)
};

struct RenderItem //TODO: 删掉~
{
    Mat44 m_worldMatrix;
    uint32_t m_meshID;
    uint32_t m_materialID;
    uint32_t m_objectID;
    AABB3 m_bounds;
    bool m_visible;
};

enum class ActivePass
{
    BACKBUFFER,
    GBUFFER,
    CARDCAPTURE,
    UNKNOWN
};

struct SurfaceRadiosityConstants
{
    // Surface Cache Atlas 信息
    uint32_t AtlasWidth;                           // 4096
    uint32_t AtlasHeight;                          // 4096
    uint32_t ProbeGridWidth;                       // 1024 (4096 / 4)
    uint32_t ProbeGridHeight;                      // 1024
    
    // Radiosity 追踪配置
    uint32_t RaysPerProbe;                         // 16
    float ProbeSpacing;                            // 4.0
    float TraceMaxDistance;                        // 200.0
    uint32_t TraceMaxSteps;                        // 64
    
    // 追踪参数
    float TraceHitThreshold;                       // 0.02
    float RayBias;                                 // 0.5
    float TemporalBlendFactor;                     // 0.05
    float SkyIntensity;                            // 0.3
    
    // Global SDF 信息
    float GlobalSDFCenter[3];
    float GlobalSDFExtent;

    float GlobalSDFInvExtent[3];
    uint32_t GlobalSDFResolution;

    // Voxel Lighting 场景边界 (与 InjectVoxelLighting 一致)
    float SceneBoundsMin[3];
    float VoxelLightingPadding0;
    float SceneBoundsMax[3];
    float VoxelLightingPadding1;
    
    // 滤波参数
    float DepthWeightScale;                        // 10.0
    float NormalWeightScale;                       // 4.0
    uint32_t FilterRadius;                         // 1
    float IndirectIntensity;                       // 1.0
    
    // 其他
    uint32_t FrameIndex;
    uint32_t ActiveCardCount;
    uint32_t Padding0;
    uint32_t Padding1;
};

struct ScreenProbeConstants
{
    uint32_t ScreenWidth;           // 1920
    uint32_t ScreenHeight;          // 1080
    uint32_t ProbeGridWidth;        // 240 (1920/8)
    uint32_t ProbeGridHeight;       // 135 (1080/8)
    
    uint32_t ProbeSpacing;          // 8
    uint32_t RaysPerProbe;          // 64
    uint32_t RaysTexWidth;          // ProbeGridWidth × 8
    uint32_t RaysTexHeight;         // ProbeGridHeight × 8
    
    float    TraceMaxDistance;      // 500.0
    uint32_t TraceMaxSteps;         // 128
    float    TraceHitThreshold;     // 0.5
    float    RayBias;               // 0.5
    
    float    MeshSDFTraceDistance;  // 100.0
    float    VoxelTraceDistance;    // 500.0
    float    TemporalBlendFactor;   // 0.05
    float    SkyIntensity;          // 0.3
    
    uint32_t CurrentFrame;
    uint32_t OctahedronSize;        // 8 (width, legacy)
    uint32_t OctahedronBorder;      // 1
    uint32_t BorderedOctSize;       // 10

    uint32_t OctahedronWidth;       // 8
    uint32_t OctahedronHeight;      // 16 (8x16 = 128 rays)
    uint32_t MeshInstanceCount;
    uint32_t Padding6;

    float    DepthThreshold;        // 0.1f - Pass 6.1
    float    PlaneDepthWeight;      // 10.0f - Pass 6.2
    float    BRDFWeight;            // 0.5f - Pass 6.4
    float    LightingWeight;        // 0.5f - Pass 6.4
    
    uint32_t FilterRadius;          // 1 - Pass 6.8
    float    DepthWeightScale;      // 10.0f - Pass 6.8
    float    NormalWeightScale;     // 4.0f - Pass 6.8
    float    AOStrength;            // 0.5f - Pass 6.10
    
    uint32_t OctTexWidth;           // ProbeGridWidth × 10
    uint32_t OctTexHeight;          // ProbeGridHeight × 10
    uint32_t Padding3;
    uint32_t Padding4;
    
    // Global SDF 参数
    Vec3     GlobalSDFCenter;
    float    GlobalSDFExtent;
    
    Vec3     GlobalSDFInvExtent;
    uint32_t GlobalSDFResolution;
    
    // Voxel Lighting 参数
    Vec3     VoxelGridMin;
    float    VoxelSize;
    
    Vec3     VoxelGridMax;
    uint32_t VoxelResolution;
    
    // Surface Cache 参数
    uint32_t AtlasWidth;            // 4096
    uint32_t AtlasHeight;           // 4096
    uint32_t TileSize;              // 128
    uint32_t ActiveCardCount;
    
    // 相机参数
    Vec3     CameraPosition;
    float    Padding0;
    
    Mat44    WorldToCamera;       // View
    Mat44    CameraToRender;      // 中间变换（通常是单位矩阵）
    Mat44    RenderToClip;        // Projection
    Mat44    CameraToWorld;       
    Mat44    RenderToCamera;      
    Mat44    ClipToRender;        
    
    Mat44    PrevWorldToCamera;   // 上一帧 View
    Mat44    PrevCameraToRender;  // 上一帧
    Mat44    PrevRenderToClip;    // 上一帧 Projection
    
    float    IndirectIntensity;     // 1.0
    uint32_t UseHistoryBufferB;     // 0 = 使用A, 1 = 使用B
    float    CameraNear;
    float    CameraFar;
};

struct CompositeConstants
{
    Mat44 ClipToRenderTransform;
    Mat44 RenderToCameraTransform;
    Mat44 CameraToWorldTransform;

    float ScreenWidth;
    float ScreenHeight;
    float IndirectIntensity;
    float DirectIntensity;

    // 太阳光参数
    float SunColor[4];

    float SunNormal[3];
    float AmbientIntensity;

    Vec3 AmbientColor;
    float ShadowBias;

    Mat44 LightWorldToCamera;
    Mat44 LightCameraToRender;
    Mat44 LightRenderToClip;

    float ShadowMapSize;
    float AOStrength;
    float SoftnessFactor;
    float LightSize;

    // SDF Shadow 参数
    Vec3 SDFCenter;
    float SDFExtent;
    float SDFShadowSoftness;
    float UseSDFShadow;
    float SDFPadding[2];
};

enum class GIVisualizationMode : uint32_t
{
    // Output (3种) - Fullscreen Compute
    FinalLighting = 0,
    DirectLightingOnly,
    IndirectLightingOnly,
    
    // Surface Cache (5种) - VS/PS Instance 渲染
    SurfaceCache_Albedo,
    SurfaceCache_Normal,
    SurfaceCache_DirectLight,
    SurfaceCache_IndirectLight,
    SurfaceCache_CombinedLight,
    
    // Voxel (1种) - Fullscreen Compute
    VoxelLighting,
    
    // Radiosity (1种) - Fullscreen Compute
    Radiosity_TraceResult,
    
    // Screen Probe (6种) - Fullscreen Compute
    ScreenProbe_BRDF_PDF,
    ScreenProbe_LightingPDF,
    ScreenProbe_MeshSDFTrace,
    ScreenProbe_RadianceOct,
    ScreenProbe_RadianceFiltered,
    
    // SDF (1种) - Fullscreen Compute
    MeshSDF_Normal,

    // AO (1种) - Fullscreen Compute
    ProbeAO,

    COUNT  // = 17
};

struct GIVisualizationParams
{
    GIVisualizationMode Mode = GIVisualizationMode::FinalLighting;
    float Exposure = 1.0f;
};


inline const char* GetVisualizationModeName(GIVisualizationMode mode)
{
    switch (mode)
    {
        case GIVisualizationMode::FinalLighting:              return "Final Lighting";
        case GIVisualizationMode::DirectLightingOnly:         return "Direct Lighting";
        case GIVisualizationMode::IndirectLightingOnly:       return "Indirect Lighting (GI)";
        
        case GIVisualizationMode::SurfaceCache_Albedo:        return "SurfaceCache: Albedo";
        case GIVisualizationMode::SurfaceCache_Normal:        return "SurfaceCache: Normal";
        case GIVisualizationMode::SurfaceCache_DirectLight:   return "SurfaceCache: Direct";
        case GIVisualizationMode::SurfaceCache_IndirectLight: return "SurfaceCache: Indirect";
        case GIVisualizationMode::SurfaceCache_CombinedLight: return "SurfaceCache: Combined";
        
        case GIVisualizationMode::VoxelLighting:              return "Voxel Lighting";
        case GIVisualizationMode::Radiosity_TraceResult:      return "Radiosity Trace";
        
        case GIVisualizationMode::ScreenProbe_BRDF_PDF:       return "ScreenProbe: BRDF PDF";
        case GIVisualizationMode::ScreenProbe_LightingPDF:    return "ScreenProbe: Lighting PDF";
        case GIVisualizationMode::ScreenProbe_MeshSDFTrace:   return "ScreenProbe: MeshSDF Trace";
        case GIVisualizationMode::ScreenProbe_RadianceOct:    return "ScreenProbe: Radiance Oct";
        case GIVisualizationMode::ScreenProbe_RadianceFiltered: return "ScreenProbe: Radiance Filtered";
        
        case GIVisualizationMode::MeshSDF_Normal:             return "MeshSDF: Normal";
        case GIVisualizationMode::ProbeAO:                    return "Probe AO";

        default: return "Unknown";
    }
}

inline bool IsSurfaceCacheMode(GIVisualizationMode mode)
{
    // SurfaceCache layers (0-4) 和 Radiosity (5) 都使用 VS/PS Instance 渲染
    return (mode >= GIVisualizationMode::SurfaceCache_Albedo
        && mode <= GIVisualizationMode::SurfaceCache_CombinedLight)
        || mode == GIVisualizationMode::Radiosity_TraceResult;
}

inline uint32_t GetSurfaceCacheLayerIndex(GIVisualizationMode mode)
{
    // 层索引必须与 CacheCommon.h 中的 SurfaceCacheLayerType 枚举匹配：
    // 0=Albedo, 1=Normal, 2=Material, 3=DirectLight, 4=IndirectLight, 5=CombinedLight
    switch (mode)
    {
        case GIVisualizationMode::SurfaceCache_Albedo:        return 0;
        case GIVisualizationMode::SurfaceCache_Normal:        return 1;
        case GIVisualizationMode::SurfaceCache_DirectLight:   return 3;  // SURFACE_CACHE_LAYER_DIRECT_LIGHT
        case GIVisualizationMode::SurfaceCache_IndirectLight: return 4;  // SURFACE_CACHE_LAYER_INDIRECT_LIGHT
        case GIVisualizationMode::SurfaceCache_CombinedLight: return 5;  // SURFACE_CACHE_LAYER_COMBINED_LIGHT
        case GIVisualizationMode::Radiosity_TraceResult:      return 6;  // Special radiosity mode
        default: return 0;
    }
}