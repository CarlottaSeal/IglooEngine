#pragma once
#include <string>
#include <vector>

#include "Vertex_PCUTBN.hpp"
#include "Engine/Math/Sphere.h"
#include "Engine/Renderer/Texture.hpp"
#include "Engine/Scene/BVH.h"
#include "Engine/Scene/SDF/SDFCommon.h"

struct SurfaceCardTemplate;
class SDFTexture3D;

enum class StaticMeshType
{
    OBJ,
    GLB,
    COUNT
};

enum class GLBChannel
{
    Albedo,
    Normal,
    AO,
    Metallic,
    Roughness,
    Count
};

struct MtlInfo
{
	Rgba8 m_ambientColor = Rgba8(255, 255, 255, 255);   // Ka
	Rgba8 m_diffuseColor = Rgba8(255, 255, 255, 255);   // Kd
	Rgba8 m_specularColor = Rgba8(255, 255, 255, 255);  // Ks
	Rgba8 m_emissiveColor = Rgba8(0, 0, 0, 255);        // Ke
    
	float m_shininess = 32.0f;      // Ns - 高光指数
	float m_transparency = 1.0f;     // d 或 Tr - 透明度
	float m_refractiveIndex = 1.0f; // Ni - 折射率
	int m_illumModel = 2;           // illum - 光照模型
    
	std::string m_diffuseMap;       
	std::string m_normalMap;        
	std::string m_specularMap;      
	std::string m_roughnessMap;     
	std::string m_ambientMap;       
	std::string m_emissiveMap;      
	std::string m_opacityMap;       
};

class StaticMesh
{
public:
	StaticMesh();
    StaticMesh(Renderer* renderer, std::string const& xmlPathNoExtensions, bool enableCardTemplates = false);
    ~StaticMesh();

    void GenerateCardTemplates();
    SurfaceCardTemplate* GetCardTemplate(uint8_t direction);
    const SurfaceCardTemplate* GetCardTemplate(uint8_t direction) const;

    Sphere GetBoundsSphere() const;
    Sphere GetTransformedBoundsSphere() const;
    AABB3 GetAABB3Bounds() const;
    AABB3 GetScaledBounds(float scale) const;
    AABB3 GetTransformedAABB3Bounds() const;
    AABB3 GetTransformedAABB3BoundsWithoutAxisTransform() const;

	SDFTexture3D* GenerateOrGetSDF(Renderer* renderer, float scale, int meshIndex);

	void BuildBVH(float scale = 1.0f);
	bool HasBVH(float scale = 1.0f) const;
	const BVH* GetBVH(float scale = 1.0f) const;

	SDFTexture3D* GetSDF(float scale) const;
	//MeshSDFData GetMeshSDFData(float scale) const;
	void SetSDF(float scale, SDFTexture3D* sdf, int meshID);
	bool HasSDF(float scale) const;

    std::vector<Vertex_PCUTBN> GetScaledAndTransformedVertices(float scale) const;
    std::vector<Vertex_PCUTBN> GetTransformedVertices() const;
    std::vector<Vertex_PCUTBN> GetTransformedVerticesWithoutAxisTransform() const;

private:
    void ApplyTransformToVertices();
    static float QuantizeScale(float scale);

	SDFTexture3D* GenerateSDFInternal(Renderer* renderer, float scale, int meshID);

public:
    std::string m_filePath;
    std::vector<Vertex_PCUTBN> m_verts;
    std::vector<unsigned int> m_indices;
    Texture* m_normalTexture = nullptr;
    Texture* m_diffuseTexture = nullptr;
    Texture* m_specularTexture = nullptr;
    Shader* m_shader = nullptr;

    float m_unitsPerMeter;
    float m_modelRelativeScale;

    std::string m_x;
    std::string m_y;
    std::string m_z;

    Mat44 m_transform = Mat44(); //no位移，aligned with SD engine
    Mat44 m_transformWithoutAxisTransform = Mat44(); //no位移no轴变换，aligned with SD engine

    VertexBuffer* m_vertexBuffer = nullptr;
    IndexBuffer* m_indexBuffer = nullptr;
	std::vector<SurfaceCardTemplate> m_cardTemplates;
	bool m_hasCardTemplates = false;

    std::unordered_map<float, BVH> m_bvhsByScale;
	bool m_bvhBuilt = false;
	// scale → sdfResourceID 
    std::map<float, SDFTexture3D*> m_sdfsByScale;
    //std::map<float, MeshSDFData*> m_sdfsByScale;
};
