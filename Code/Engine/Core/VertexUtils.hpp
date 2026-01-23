#pragma once
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Vertex_PCUTBN.hpp"
#include "Engine/Math/AABB2.hpp"
#include <vector>

#include "Engine/Math/OBB3.h"

struct OBB2;
struct AABB3;
struct Mat44;
struct FloatRange;

//typedef std::vector<Vertex_PCU> CpuMesh;

void TransformVertexArray3D(int numVerts, Vertex_PCU* verts, float scale, float orientationDegrees, const Vec2& translation);
void TransformVertexArray3D(std::vector<Vertex_PCU>& verts, const Mat44& transform);

AABB2 GetVertexBounds2D(const std::vector<Vertex_PCU>& verts);
AABB3 GetVertexBounds3D(const std::vector<Vertex_PCUTBN>& verts);

void AddVertsForDisc2D(std::vector<Vertex_PCU>& verts, Vec2 const& discCenter, float discRadius, Rgba8 const& color = Rgba8::WHITE, int numSlices = 32);
void AddVertsForAABB2D(std::vector<Vertex_PCU>& verts, AABB2 const& alignedBox, Rgba8 const& color, AABB2 uv);
void AddVertsForAABB2D(std::vector<Vertex_PCU>& verts, AABB2 const& alignedBox, Rgba8 const& color);
void AddVertsForOBB2D(std::vector<Vertex_PCU>& verts, OBB2 const& orientedBox, Rgba8 const& color);
void AddVertsForCapsule2D(std::vector<Vertex_PCU>& verts, Vec2 const& boneStart, Vec2 const& boneEnd, float radius, Rgba8 const& color);
void AddVertsForTriangle2D(std::vector<Vertex_PCU>& verts, Vec2 const& ccw0, Vec2 const& ccw1, Vec2 const& ccw2, Rgba8 const& color);
void AddVertsForLineSegment2D(std::vector<Vertex_PCU>& verts, Vec2 const& start, Vec2 const& end, float thickness, Rgba8 const& color);
void AddVertsForArrow2D(std::vector<Vertex_PCU>& verts, Vec2 tailPos, Vec2 tipPos, float arrowSize, float lineThickness, Rgba8 const& color);

void AddVertsForQuad3D(std::vector<Vertex_PCU>& verts, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft,
	const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForQuad3DUV(std::vector<Vertex_PCU>& verts, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft,
	const Rgba8& color = Rgba8::WHITE, Vec2 uv0 = Vec2::ZERO, Vec2 uv1 = Vec2::ZERO, Vec2 uv2 = Vec2::ONE, Vec2 uv3 = Vec2::ONE);
void RotateQuadAroundCenterCW90(Vec3& p0, Vec3& p1, Vec3& p2, Vec3& p3, int times);

void AddVertsForQuad3D(std::vector<Vertex_PCUTBN>& verts, const Vec3& bottomLeft, const Vec3& bottomRight, 
					   const Vec3& topRight, const Vec3& topLeft,const Rgba8& color = Rgba8::WHITE, 
					   const AABB2& UVs = AABB2::ZERO_TO_ONE,const Vec3& normal = Vec3(0, 0, 1));

void AddVertsForAABB3D(std::vector<Vertex_PCU>& verts, const AABB3& bounds, const Rgba8& color = Rgba8::WHITE,
	const AABB2& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForAABBZWireframe3D(std::vector<Vertex_PCU>& verts, AABB3 const& bounds, float lineThickness, Rgba8 const& tint = Rgba8::WHITE);

void AddVertsForSphere3D(std::vector<Vertex_PCU>& verts, const Vec3& center, float radius, const Rgba8& color = Rgba8::WHITE,
	const AABB2& UVs = AABB2::ZERO_TO_ONE, int numSlices = 32, int numStacks = 16);
void AddVertsForUVSphereZ3D(std::vector<Vertex_PCU>& verts, Vec3 const& center, float radius, int numSlices, int numStacks, Rgba8 const& tint = Rgba8::WHITE, AABB2 const& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForUVSphereZWireframe3D(std::vector<Vertex_PCU>& verts, Vec3 const& center, float radius, int numSlices, int numStacks, float lineThickness, Rgba8 const& tint = Rgba8::WHITE);

void AddVertsForCylinder3D(std::vector<Vertex_PCU>& verts,
	const Vec3& start, const Vec3& end, float radius,
	const Rgba8& color = Rgba8::WHITE,
	const AABB2& UVs = AABB2::ZERO_TO_ONE,
	int numSlices = 8);
void AddVertsForCylinderZ3D(std::vector<Vertex_PCU>& verts, Vec2 const& centerXY, FloatRange const& minMaxZ, float radius, int numSlices, Rgba8 const& tint = Rgba8::WHITE, AABB2 const& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForCylinderZWireframe3D(std::vector<Vertex_PCU>& verts, Vec2 const& centerXY, FloatRange const& minMaxZ, float radius, int numSlices, float lineThickness, Rgba8 const& tint = Rgba8::WHITE);

void AddVertsForOBB3D(std::vector<Vertex_PCU>& verts, OBB3 const& box, Rgba8 const& tint = Rgba8::WHITE, AABB2 const& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForOBBWireframe3D(std::vector<Vertex_PCU>& verts, OBB3 const& box, float lineThickness, Rgba8 const& tint = Rgba8::WHITE);

void AddVertsForCone3D(std::vector<Vertex_PCU>& verts,
	const Vec3& start, const Vec3& end, float radius,
	const Rgba8& color = Rgba8::WHITE,
	const AABB2& UVs = AABB2::ZERO_TO_ONE,
	int numSlices = 8);
void AddVertsForArrow3D(std::vector<Vertex_PCU>& verts,
	const Vec3& start, const Vec3& end, float radius,
	const Rgba8& color = Rgba8::WHITE,
	const AABB2& UVs = AABB2::ZERO_TO_ONE,
	int numSlices = 8);

void AddVertsForIndexQuad3D(std::vector<Vertex_PCUTBN>& vertexes, std::vector<unsigned int>& indexes,
	const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft,
	const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForIndexSphere3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, 
	Vec3 const& center, float radius, int numSlices = 32, int numStacks = 16, Rgba8 const& color = Rgba8::WHITE, AABB2 const& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForIndexCylinder3D( std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes,
	const Vec3& start, const Vec3& end, float radius, const Rgba8& color = Rgba8::WHITE, 
	const AABB2& UVs = AABB2::ZERO_TO_ONE, int numSlices = 16);
void AddVertsForIndexCylinderZ3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices,
	Vec2 const& centerXY, FloatRange const& minMaxZ, float radius,
	Rgba8 const& tint = Rgba8::WHITE, AABB2 const& UVs = AABB2::ZERO_TO_ONE, int numSlices = 16);
void AddVertsForIndexAABB3D(std::vector<Vertex_PCUTBN>& vertexes, std::vector<unsigned int>& indexes, const AABB3& cube,
	const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE);
void AddVertsForIndexOBB3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, const OBB3& cube,
	const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE);

void AddVertsForIndexAABBZWireframe3D(std::vector<Vertex_PCU>& verts, 
								  std::vector<unsigned int>& indices,
								  AABB3 const& bounds, 
								  Rgba8 const& tint = Rgba8::WHITE);

void CalculateTangentAndBiTangent(std::vector<Vertex_PCUTBN>& vertices, const std::vector<unsigned int>& indices);

void AddVertsForRoundedQuad3D(std::vector<Vertex_PCUTBN>& vertexes, const Vec3& topLeft, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE);

void AddVertsForSphere3D(std::vector<Vertex_PCUTBN>& verts, const Vec3& center, float radius, const Rgba8& color = Rgba8::WHITE, const AABB2& UVs = AABB2::ZERO_TO_ONE, int numSlices = 16, int numStacks = 8);