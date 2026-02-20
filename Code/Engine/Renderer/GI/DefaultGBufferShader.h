#pragma once

#ifdef ENGINE_DX12_RENDERER
const char* m_gBufferShaderSource = R"(

#define MAX_TEXTURE_COUNT 200

cbuffer PerFrameConstants : register(b0) 
{
    float Time;
    int DebugInt;
    float DebugFloat;
    float padding0;
}

cbuffer CameraConstants : register(b1) 
{
    float4x4 WorldToCameraTransform;
    float4x4 CameraToRenderTransform;
    float4x4 RenderToClipTransform;
    //float3 CameraWorldPosition;
    //float padding1;
}

cbuffer DrawConstants : register(b21)
{
    uint InstanceOffset;
}

cbuffer MaterialConstants : register(b3)
{
    int DiffuseId;
    int NormalId;
    int SpecularId;  // R:roughness G:metallic B:AO
    float padding2;
}

struct InstanceData
{
    float4x4 ModelToWorld;
    float4 Color;
};
StructuredBuffer<InstanceData> g_Instances : register(t243);

Texture2D g_textures[MAX_TEXTURE_COUNT] : register(t0);
SamplerState g_sampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 Normal : NORMAL;
    uint InstanceID : SV_InstanceID;
};

struct VSOutput 
{
    float4 Position : SV_POSITION;
    float3 WorldPos : WORLDPOS;
    float3 WorldNormal : NORMAL;
    float3 WorldTangent : TANGENT;
    float3 WorldBitangent : BITANGENT;
    float2 TexCoord : TEXCOORD;
    float4 Color : COLOR;
    float4 PrevClipPos : PREVPOS;
    float4 CurrClipPos : CURRPOS;
};

VSOutput GBufferVS(VSInput input)
{
    VSOutput output;

    InstanceData inst = g_Instances[input.InstanceID + InstanceOffset];
    float4x4 ModelToWorld = inst.ModelToWorld;
    float4 InstanceColor = inst.Color;

    float4 modelPos = float4(input.Position, 1.0f);

    float4 worldPos = mul(ModelToWorld, modelPos);
    float4 cameraPos = mul(WorldToCameraTransform, worldPos);
    float4 renderPos = mul(CameraToRenderTransform, cameraPos);
    float4 clipPos = mul(RenderToClipTransform, renderPos);

    float4 modelNormal = float4(input.Normal, 0.0f);
    float4 modelTangent = float4(input.Tangent, 0.0f);
    float4 modelBitangent = float4(input.Bitangent, 0.0f);

    float4 worldNormal = mul(ModelToWorld, modelNormal);
    float4 worldTangent = mul(ModelToWorld, modelTangent);
    float4 worldBitangent = mul(ModelToWorld, modelBitangent);

    output.Position = clipPos;
    output.WorldPos = worldPos.xyz;
    output.WorldNormal = normalize(worldNormal.xyz);
    output.WorldTangent = normalize(worldTangent.xyz);
    output.WorldBitangent = normalize(worldBitangent.xyz);

    output.CurrClipPos = clipPos;
    output.PrevClipPos = clipPos;

    output.TexCoord = input.TexCoord;
    output.Color = input.Color * InstanceColor;

    return output;
}

struct PSOutput 
{
    float4 Albedo : SV_TARGET0;     // RGBA8
    float4 Normal : SV_TARGET1;     // R10G10B10A2 or RGBA16F
    float4 Material : SV_TARGET2;   // R:roughness G:metallic B:AO 
    float4 WorldPos  : SV_TARGET3;     // RG16F
};

PSOutput GBufferPS(VSOutput input) 
{
    PSOutput output;
    
    // 1. Albedo (sRGB)
    float4 diffuseColor = g_textures[DiffuseId].Sample(g_sampler, input.TexCoord);
    output.Albedo = diffuseColor * input.Color;
    
    // 2. Normal (world space)
    float3 T = normalize(input.WorldTangent);
    float3 B = normalize(input.WorldBitangent);
    float3 N = normalize(input.WorldNormal);
    
    float3x3 TBN = float3x3(T, B, N);
    
    float3 nTS = g_textures[NormalId].Sample(g_sampler, input.TexCoord).xyz;
    nTS = nTS * 2.0f - 1.0f; 
    
    float3 worldNormal = normalize(mul(nTS, TBN));
    
    output.Normal.xyz = (worldNormal + 1.0f) * 0.5f;
    output.Normal.w = 1.0f;
    
    // 3. Material 
    // R=roughness, G=metallic, B=AO
    float3 materialParams = g_textures[SpecularId].Sample(g_sampler, input.TexCoord).xyz;
    
    output.Material.r = materialParams.r;  // Roughness
    output.Material.g = materialParams.g;  // Metallic
    output.Material.b = materialParams.b;  // AO
    
    // ObjectID->alpha
    // ATT: if format RGB8
    //output.Material.a = float(ObjectID) / 65535.0;  
    output.Material.a = 1.0; 
    
    // 4. Motion Vectors
    //float2 currPos = input.CurrClipPos.xy / input.CurrClipPos.w;
    //float2 prevPos = input.PrevClipPos.xy / input.PrevClipPos.w;
    //output.Motion = (currPos - prevPos) * 0.5;
    output.WorldPos = float4(input.WorldPos, 1.0); 

    return output;
}
    )";
#endif