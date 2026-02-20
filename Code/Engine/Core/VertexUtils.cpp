#include "VertexUtils.hpp"
#include "Engine/Math/Vec3.hpp"  
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/OBB2.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/Mat44.hpp"
#include "Engine/Math/FloatRange.hpp"

#include <math.h> 
#include <vector>

const float PIE = 3.14159265358979323846f;

void TransformVertexArray3D(int numVerts, Vertex_PCU* verts, float scale, float orientationDegrees, const Vec2& translation)
{
    float radians = orientationDegrees * (PIE / 180.0f);
    float cosTheta = cosf(radians);
    float sinTheta = sinf(radians);

    for (int i = 0; i < numVerts; ++i) 
    {
        Vec3& pos = verts[i].m_position;

        pos.x *= scale;
        pos.y *= scale;

        float rotatedX = pos.x * cosTheta - pos.y * sinTheta;
        float rotatedY = pos.x * sinTheta + pos.y * cosTheta;

        pos.x = rotatedX;
        pos.y = rotatedY;

        pos.x += translation.x;
        pos.y += translation.y;
    }
}

void TransformVertexArray3D(std::vector<Vertex_PCU>& verts, const Mat44& transform)
{
	for (Vertex_PCU& vert : verts)
	{
		vert.m_position = transform.TransformPosition3D(vert.m_position);
	}
}

AABB2 GetVertexBounds2D(const std::vector<Vertex_PCU>& verts)
{
	AABB2 bounds;
	bounds.m_maxs.x = FLT_MIN;
	bounds.m_maxs.y = FLT_MIN;
	bounds.m_mins.x = FLT_MAX;
	bounds.m_mins.y = FLT_MAX;
	for (int vertIndex = 0; vertIndex < verts.size(); vertIndex++) 
	{
		if (verts[vertIndex].m_position.x < bounds.m_mins.x) 
		{
			bounds.m_mins.x = verts[vertIndex].m_position.x;
		}
		if (verts[vertIndex].m_position.y < bounds.m_mins.y) 
		{
			bounds.m_mins.y = verts[vertIndex].m_position.y;
		}
		if (verts[vertIndex].m_position.x > bounds.m_maxs.x) 
		{
			bounds.m_maxs.x = verts[vertIndex].m_position.x;
		}
		if (verts[vertIndex].m_position.y > bounds.m_maxs.y) 
		{
			bounds.m_maxs.y = verts[vertIndex].m_position.y;
		}
	}
	return bounds;
}

AABB3 GetVertexBounds3D(const std::vector<Vertex_PCUTBN>& verts)
{
	if (verts.empty())
		return AABB3();
        
	Vec3 minPos = verts[0].m_position;
	Vec3 maxPos = verts[0].m_position;
        
	for (const auto& vertex : verts)
	{
		Vec3 pos = vertex.m_position;
            
		minPos.x = std::min(minPos.x, pos.x);
		minPos.y = std::min(minPos.y, pos.y);
		minPos.z = std::min(minPos.z, pos.z);
            
		maxPos.x = std::max(maxPos.x, pos.x);
		maxPos.y = std::max(maxPos.y, pos.y);
		maxPos.z = std::max(maxPos.z, pos.z);
	}
        
	AABB3 bounds;
	bounds.m_mins = minPos;
	bounds.m_maxs = maxPos;
	return bounds;
}

void AddVertsForDisc2D(std::vector<Vertex_PCU>& verts, Vec2 const& discCenter, float discRadius, Rgba8 const& color, int numSlides)
{
	const float angleStep = 360.0f / numSlides;  

	//center
	Vertex_PCU centerVertex(Vec2(discCenter), color, Vec2(0.5f, 0.5f));

	for (int i = 0; i < numSlides; ++i) 
    {
		float angle1 = i * angleStep;
		float angle2 = (i + 1) * angleStep;

		Vec2 point1 = discCenter + Vec2(cosf(angle1 * (3.14159265f / 180.0f)) * discRadius,
			sinf(angle1 * (3.14159265f / 180.0f)) * discRadius);
		Vec2 point2 = discCenter + Vec2(cosf(angle2 * (3.14159265f / 180.0f)) * discRadius,
			sinf(angle2 * (3.14159265f / 180.0f)) * discRadius);

		// Add a triangle
		verts.push_back(centerVertex); 
		verts.push_back(Vertex_PCU(Vec2(point1), color, Vec2(0.5f, 0.5f)));  // first point
        verts.push_back(Vertex_PCU(Vec2(point2), color, Vec2(0.5f, 0.5f)));  // next point
	}
}

void AddVertsForAABB2D(std::vector<Vertex_PCU>& verts, AABB2 const& alignedBox, Rgba8 const& color, AABB2 uv)
{
	Vec2 bottomLeft = alignedBox.m_mins;
	Vec2 bottomRight = Vec2(alignedBox.m_maxs.x, alignedBox.m_mins.y);
	Vec2 topLeft = Vec2(alignedBox.m_mins.x, alignedBox.m_maxs.y);
	Vec2 topRight = alignedBox.m_maxs;

	// 2 triangles
	// triangle1: bottomLeft, bottomRight, topRight
	verts.push_back(Vertex_PCU(bottomLeft, color, Vec2(uv.m_mins.x, uv.m_mins.y)));
	verts.push_back(Vertex_PCU(bottomRight, color, Vec2(uv.m_maxs.x, uv.m_mins.y)));
	verts.push_back(Vertex_PCU(topRight, color, Vec2(uv.m_maxs.x, uv.m_maxs.y)));

	// triangle2: bottomLeft, topRight, topLeft
	verts.push_back(Vertex_PCU(bottomLeft, color, Vec2(uv.m_mins.x, uv.m_mins.y)));
	verts.push_back(Vertex_PCU(topRight, color, Vec2(uv.m_maxs.x, uv.m_maxs.y)));
	verts.push_back(Vertex_PCU(topLeft, color, Vec2(uv.m_mins.x, uv.m_maxs.y)));
}

void AddVertsForAABB2D(std::vector<Vertex_PCU>& verts, AABB2 const& alignedBox, Rgba8 const& color)
{
	Vec2 bottomLeft = alignedBox.m_mins;
	Vec2 bottomRight = Vec2(alignedBox.m_maxs.x, alignedBox.m_mins.y);
	Vec2 topLeft = Vec2(alignedBox.m_mins.x, alignedBox.m_maxs.y);
	Vec2 topRight = alignedBox.m_maxs;

	// 2 triangles
	// triangle1: bottomLeft, bottomRight, topRight
	verts.push_back(Vertex_PCU(bottomLeft, color, Vec2(0.f, 0.f)));
	verts.push_back(Vertex_PCU(bottomRight, color, Vec2(1.f, 0.f)));
	verts.push_back(Vertex_PCU(topRight, color, Vec2(1.f, 1.f)));

	// triangle2: bottomLeft, topRight, topLeft
	verts.push_back(Vertex_PCU(bottomLeft, color, Vec2(0.f, 0.f)));
	verts.push_back(Vertex_PCU(topRight, color, Vec2(1.f, 1.f)));
	verts.push_back(Vertex_PCU(topLeft, color, Vec2(0.f,1.f)));
}

void AddVertsForOBB2D(std::vector<Vertex_PCU>& verts, OBB2 const& orientedBox, Rgba8 const& color)
{
	Vec2 cornerPoints[4];
	orientedBox.GetCornerPoints(cornerPoints);

	// make 2 triangles, connect 3 points in order
	verts.push_back(Vertex_PCU(Vec2(cornerPoints[0]), color, Vec2(0.0f, 0.0f)));
	verts.push_back(Vertex_PCU(Vec2(cornerPoints[1]), color, Vec2(1.0f, 0.0f)));
	verts.push_back(Vertex_PCU(Vec2(cornerPoints[2]), color, Vec2(1.0f, 1.0f)));

	verts.push_back(Vertex_PCU(Vec2(cornerPoints[0]), color, Vec2(0.0f, 0.0f)));
	verts.push_back(Vertex_PCU(Vec2(cornerPoints[2]), color, Vec2(1.0f, 1.0f)));
	verts.push_back(Vertex_PCU(Vec2(cornerPoints[3]), color, Vec2(0.0f, 1.0f)));
}

void AddVertsForCapsule2D(std::vector<Vertex_PCU>& verts, Vec2 const& boneStart, Vec2 const& boneEnd, float radius, Rgba8 const& color)
{
	Vec2 forwardStep = (boneEnd - boneStart);
	forwardStep.SetLength(radius);
	Vec2 leftStep = forwardStep.GetRotated90Degrees();

	Vec2 ER = boneEnd + leftStep;   // End-Right
	Vec2 EL = boneEnd - leftStep;   // End-Left
	Vec2 SR = boneStart + leftStep; // Start-Right
	Vec2 SL = boneStart - leftStep; // Start-Left

	verts.push_back(Vertex_PCU(SR, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(SL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(ER, color, Vec2(0.5f, 0.5f)));

	verts.push_back(Vertex_PCU(SL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(EL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(ER, color, Vec2(0.5f, 0.5f)));

	constexpr int NUM_SLICES = 8;
	constexpr float DEGREES_PER_SLICE = 180.0f / static_cast<float>(NUM_SLICES);

	//hemi-circle
	for (int i = 0; i < NUM_SLICES; ++i)
	{
		float angleDegrees = i * DEGREES_PER_SLICE;
		float nextAngleDegrees = (i + 1) * DEGREES_PER_SLICE;

		Vec2 offset1 = leftStep.GetRotatedDegrees(angleDegrees);
		Vec2 offset2 = leftStep.GetRotatedDegrees(nextAngleDegrees);

		verts.push_back(Vertex_PCU(boneStart, color, Vec2(0.5f, 0.5f)));
		verts.push_back(Vertex_PCU(boneStart + offset1, color, Vec2(1.0f, 0.5f)));
		verts.push_back(Vertex_PCU(boneStart + offset2, color, Vec2(0.5f, 1.0f)));
	}

	for (int i = 0; i < NUM_SLICES; ++i)
	{
		float angleDegrees = i * DEGREES_PER_SLICE;
		float nextAngleDegrees = (i + 1) * DEGREES_PER_SLICE;

		Vec2 offset1 = leftStep.GetRotatedDegrees(-angleDegrees);
		Vec2 offset2 = leftStep.GetRotatedDegrees(-nextAngleDegrees);

		verts.push_back(Vertex_PCU(boneEnd, color, Vec2(0.5f, 0.5f)));
		verts.push_back(Vertex_PCU(boneEnd + offset1, color, Vec2(1.0f, 0.5f)));
		verts.push_back(Vertex_PCU(boneEnd + offset2, color, Vec2(0.5f, 1.0f)));
	}
}

void AddVertsForTriangle2D(std::vector<Vertex_PCU>& verts, Vec2 const& ccw0, Vec2 const& ccw1, Vec2 const& ccw2, Rgba8 const& color)
{
	verts.push_back(Vertex_PCU(Vec2(ccw0), color, Vec2(0.0f, 0.0f)));
	verts.push_back(Vertex_PCU(Vec2(ccw1), color, Vec2(1.0f, 0.0f)));
	verts.push_back(Vertex_PCU(Vec2(ccw2), color, Vec2(0.5f, 1.0f)));
}

void AddVertsForLineSegment2D(std::vector<Vertex_PCU>& verts, Vec2 const& start, Vec2 const& end, float thickness, Rgba8 const& color)
{
	Vec2 lineDir = (end - start).GetNormalized();
	Vec2 normal = Vec2(-lineDir.y, lineDir.x); 

	// offset
	Vec2 offset = normal * (thickness * 0.5f);

	// 4 points
	Vec2 SR = start + offset;  
	Vec2 SL = start - offset;  
	Vec2 ER = end + offset;    
	Vec2 EL = end - offset;    

	// cut it into 2 triangles and add vertex
	verts.push_back(Vertex_PCU(SR, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(SL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(ER, color, Vec2(0.5f, 0.5f)));

	verts.push_back(Vertex_PCU(SL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(EL, color, Vec2(0.5f, 0.5f)));
	verts.push_back(Vertex_PCU(ER, color, Vec2(0.5f, 0.5f)));
}

void AddVertsForArrow2D(std::vector<Vertex_PCU>& verts, Vec2 tailPos, Vec2 tipPos, float arrowSize, float lineThickness, Rgba8 const& color)
{
	AddVertsForLineSegment2D(verts, tailPos, tipPos, lineThickness, color);

	Vec2 direction = (tipPos - tailPos).GetNormalized();

	Vec2 leftDir = direction.GetRotatedDegrees(135.0f); 
	Vec2 rightDir = direction.GetRotatedDegrees(-135.0f); 
	Vec2 leftTip = tipPos + leftDir * arrowSize; 
	Vec2 rightTip = tipPos + rightDir * arrowSize; 

	AddVertsForLineSegment2D(verts, tipPos, leftTip, lineThickness, color);

	AddVertsForLineSegment2D(verts, tipPos, rightTip, lineThickness, color);
}

void AddVertsForQuad3D(std::vector<Vertex_PCU>& verts, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft, const Rgba8& color, const AABB2& UVs)
{
	Vec2 uvBottomLeft = UVs.m_mins;
	Vec2 uvBottomRight = Vec2(UVs.m_maxs.x, UVs.m_mins.y);
	Vec2 uvTopRight = UVs.m_maxs;
	Vec2 uvTopLeft = Vec2(UVs.m_mins.x, UVs.m_maxs.y);

	verts.push_back(Vertex_PCU(bottomLeft, color, uvBottomLeft));
	verts.push_back(Vertex_PCU(bottomRight, color, uvBottomRight));
	verts.push_back(Vertex_PCU(topRight, color, uvTopRight));

	verts.push_back(Vertex_PCU(bottomLeft, color, uvBottomLeft));
	verts.push_back(Vertex_PCU(topRight, color, uvTopRight));
	verts.push_back(Vertex_PCU(topLeft, color, uvTopLeft));
}

void AddVertsForQuad3DUV(std::vector<Vertex_PCU>& verts, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft, const Rgba8& color, Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3)
{
	verts.push_back(Vertex_PCU(bottomLeft, color, uv0));
	verts.push_back(Vertex_PCU(bottomRight, color, uv1));
	verts.push_back(Vertex_PCU(topRight, color, uv3));

	verts.push_back(Vertex_PCU(bottomLeft, color, uv0));
	verts.push_back(Vertex_PCU(topRight, color, uv3));
	verts.push_back(Vertex_PCU(topLeft, color, uv2));
}

void RotateQuadAroundCenterCW90(Vec3& p0, Vec3& p1, Vec3& p2, Vec3& p3, int times)
{
	times = (times % 4 + 4) % 4;
	if (times == 0) return;

	Vec3 center = (p0 + p2) * 0.5f; // 对于单位方块，p0=(0,0), p2=(1,1)，中心=(0.5,0.5)

	auto rotatePoint = [&](Vec3 p) -> Vec3
	{
		Vec3 d = p - center;
		for (int i = 0; i < times; ++i)
		{
			// 顺时针 90°： (x, y) -> (y, -x)
			float oldX = d.x;
			float oldY = d.y;
			d.x =  oldY;
			d.y = -oldX;
		}
		return center + d;
	};

	p0 = rotatePoint(p0);
	p1 = rotatePoint(p1);
	p2 = rotatePoint(p2);
	p3 = rotatePoint(p3);
}

void AddVertsForQuad3D(std::vector<Vertex_PCUTBN>& verts, const Vec3& bottomLeft, const Vec3& bottomRight,
	const Vec3& topRight, const Vec3& topLeft, const Rgba8& color, const AABB2& UVs, const Vec3& normal)
{
	Vec3 edge1 = bottomRight - bottomLeft;
	Vec3 edge2 = topLeft - bottomLeft;
	Vec3 tangent = edge1.GetNormalized();
	Vec3 bitangent = CrossProduct3D(normal, tangent).GetNormalized();
    
	// 添加 6 个顶点（2 个三角形）
	verts.emplace_back(bottomLeft, color, Vec2(UVs.m_mins.x, UVs.m_mins.y), tangent, bitangent, normal);
	verts.emplace_back(bottomRight, color, Vec2(UVs.m_maxs.x, UVs.m_mins.y), tangent, bitangent, normal);
	verts.emplace_back(topRight, color, Vec2(UVs.m_maxs.x, UVs.m_maxs.y), tangent, bitangent, normal);
    
	verts.emplace_back(bottomLeft, color, Vec2(UVs.m_mins.x, UVs.m_mins.y), tangent, bitangent, normal);
	verts.emplace_back(topRight, color, Vec2(UVs.m_maxs.x, UVs.m_maxs.y), tangent, bitangent, normal);
	verts.emplace_back(topLeft, color, Vec2(UVs.m_mins.x, UVs.m_maxs.y), tangent, bitangent, normal);

}

void AddVertsForAABB3D(std::vector<Vertex_PCU>& verts, const AABB3& bounds, const Rgba8& color, const AABB2& UVs)
{
	Vec2 uvAtMins = UVs.m_mins;
	Vec2 uvAtMaxs = UVs.m_maxs;

	float minX = bounds.m_mins.x;
	float minY = bounds.m_mins.y;
	float minZ = bounds.m_mins.z;
	float maxX = bounds.m_maxs.x;
	float maxY = bounds.m_maxs.y;
	float maxZ = bounds.m_maxs.z;

	// +x
	AddVertsForQuad3D(verts, Vec3(maxX, minY, minZ), Vec3(maxX, maxY, minZ), Vec3(maxX, maxY, maxZ), Vec3(maxX, minY, maxZ), color, UVs);
	// -x
	AddVertsForQuad3D(verts, Vec3(minX, maxY, minZ), Vec3(minX, minY, minZ), Vec3(minX, minY, maxZ), Vec3(minX, maxY, maxZ), color, UVs);
	// +y
	AddVertsForQuad3D(verts, Vec3(maxX, maxY, minZ), Vec3(minX, maxY, minZ), Vec3(minX, maxY, maxZ), Vec3(maxX, maxY, maxZ), color, UVs);
	// -y
	AddVertsForQuad3D(verts, Vec3(minX, minY, minZ), Vec3(maxX, minY, minZ), Vec3(maxX, minY, maxZ), Vec3(minX, minY, maxZ), color, UVs);
	// +z
	AddVertsForQuad3D(verts, Vec3(minX, minY, maxZ), Vec3(maxX, minY, maxZ), Vec3(maxX, maxY, maxZ), Vec3(minX, maxY, maxZ), color, UVs);
	// -z
	AddVertsForQuad3D(verts, Vec3(minX, maxY, minZ), Vec3(maxX, maxY, minZ), Vec3(maxX, minY, minZ), Vec3(minX, minY, minZ), color, UVs);
}

void AddVertsForSphere3D(std::vector<Vertex_PCU>& verts, const Vec3& center, float radius, const Rgba8& color, const AABB2& UVs, int numSlices, int numStacks)
{
	float longitudeDegree = 360.f / static_cast<float>(numSlices);
	float latitudeDegree = 180.f / static_cast<float>(numStacks);
	float uPerPieces = UVs.GetDimensions().x / static_cast<float>(numSlices);
	float vPerPieces = UVs.GetDimensions().y / static_cast<float>(numStacks);


	for (int loIndex = 0; loIndex < numSlices; loIndex++) 
	{
		for (int laIndex = 0; laIndex < numStacks; laIndex++)
		{

			Vec3 BL = Vec3::MakeFromPolarDegrees(-90.f + (float)laIndex * latitudeDegree, (float)loIndex * longitudeDegree, radius) + center;
			Vec3 BR = Vec3::MakeFromPolarDegrees(-90.f + (float)laIndex * latitudeDegree, (float)(loIndex + 1) * longitudeDegree, radius) + center;
			Vec3 TL = Vec3::MakeFromPolarDegrees(-90.f + (float)(laIndex + 1) * latitudeDegree, (float)loIndex * longitudeDegree, radius) + center;
			Vec3 TR = Vec3::MakeFromPolarDegrees(-90.f + (float)(laIndex + 1) * latitudeDegree, (float)(loIndex + 1) * longitudeDegree, radius) + center;

			Vec2 uvAtMins = Vec2(uPerPieces * (float)(loIndex), vPerPieces * (float)(laIndex));
			Vec2 uvAtMaxs = Vec2(uPerPieces * (float)(loIndex + 1), vPerPieces * (float)(laIndex + 1));

			verts.push_back(Vertex_PCU(BL, color, uvAtMins));
			verts.push_back(Vertex_PCU(BR, color, Vec2(uvAtMaxs.x, uvAtMins.y)));
			verts.push_back(Vertex_PCU(TR, color, uvAtMaxs));

			verts.push_back(Vertex_PCU(BL, color, uvAtMins));
			verts.push_back(Vertex_PCU(TR, color, uvAtMaxs));
			verts.push_back(Vertex_PCU(TL, color, Vec2(uvAtMins.x, uvAtMaxs.y)));
		}
	}
}

void AddVertsForCylinder3D(std::vector<Vertex_PCU>& verts, const Vec3& start, const Vec3& end, float radius, const Rgba8& color, const AABB2& UVs, int numSlices)
{
	if (numSlices < 3) return;

	Vec3 cylinderI = (end - start).GetNormalized();
	Vec3 cylinderJ = CrossProduct3D(cylinderI, Vec3(1.f, 0.f, 0.f));

	Vec3 iBasis;
	if (cylinderJ.GetLengthSquared() == 0.f)
	{
		iBasis = CrossProduct3D(Vec3(0.f, 1.f, 0.f), cylinderI).GetNormalized();
		cylinderJ = CrossProduct3D(cylinderI, iBasis).GetNormalized();
	}
	else
	{
		iBasis = CrossProduct3D(cylinderJ, cylinderI).GetNormalized();
		cylinderJ = cylinderJ.GetNormalized();
	}

	float angleStep = 360.0f / static_cast<float>(numSlices);

	std::vector<Vec3> baseCircle;
	std::vector<Vec3> topCircle;

	// Placeholders for all verts needed in side/top/bottom
	for (int i = 0; i <= numSlices; ++i)
	{
		float angle = i * angleStep;
		float cosA = CosDegrees(angle);
		float sinA = SinDegrees(angle);

		Vec3 circleOffset = (iBasis * cosA + cylinderJ * sinA) * radius;
		baseCircle.push_back(start + circleOffset);
		topCircle.push_back(end + circleOffset);
	}

	// side
	for (int i = 0; i < numSlices; ++i)
	{
		Vec3 v0 = baseCircle[i];
		Vec3 v1 = baseCircle[i + 1];
		Vec3 v2 = topCircle[i];
		Vec3 v3 = topCircle[i + 1];

		float u0 = UVs.m_mins.x + (UVs.m_maxs.x - UVs.m_mins.x) * (static_cast<float>(i) / numSlices);
		float u1 = UVs.m_mins.x + (UVs.m_maxs.x - UVs.m_mins.x) * (static_cast<float>(i + 1) / numSlices);

		float vBottom = UVs.m_mins.y;
		float vTop = UVs.m_maxs.y;

		verts.push_back(Vertex_PCU(v0, color, Vec2(u0, vBottom)));
		verts.push_back(Vertex_PCU(v1, color, Vec2(u1, vBottom)));
		verts.push_back(Vertex_PCU(v2, color, Vec2(u0, vTop)));

		verts.push_back(Vertex_PCU(v2, color, Vec2(u0, vTop)));
		verts.push_back(Vertex_PCU(v1, color, Vec2(u1, vBottom)));
		verts.push_back(Vertex_PCU(v3, color, Vec2(u1, vTop)));
	}

	// top
	Vec3 topCenter = end;
	Vec2 uvCenter = Vec2(UVs.m_mins.x + 0.5f * (UVs.m_maxs.x - UVs.m_mins.x),
		UVs.m_mins.y + 0.5f * (UVs.m_maxs.y - UVs.m_mins.y));

	for (int i = 0; i < numSlices; ++i)
	{
		Vec3 v0 = topCircle[i];
		Vec3 v1 = topCircle[i + 1];

		Vec2 uv0 = Vec2(UVs.m_mins.x + 0.5f * (UVs.m_maxs.x - UVs.m_mins.x) + 0.5f * (UVs.m_maxs.x - UVs.m_mins.x) * CosDegrees(i * angleStep),
			UVs.m_mins.y + 0.5f * (UVs.m_maxs.y - UVs.m_mins.y) + 0.5f * (UVs.m_maxs.y - UVs.m_mins.y) * SinDegrees(i * angleStep));

		Vec2 uv1 = Vec2(UVs.m_mins.x + 0.5f * (UVs.m_maxs.x - UVs.m_mins.x) + 0.5f * (UVs.m_maxs.x - UVs.m_mins.x) * CosDegrees((i + 1) * angleStep),
			UVs.m_mins.y + 0.5f * (UVs.m_maxs.y - UVs.m_mins.y) + 0.5f * (UVs.m_maxs.y - UVs.m_mins.y) * SinDegrees((i + 1) * angleStep));

		verts.push_back(Vertex_PCU(topCenter, color, uvCenter));
		verts.push_back(Vertex_PCU(v0, color, uv0));
		verts.push_back(Vertex_PCU(v1, color, uv1));
	}

	// bottom
	Vec3 bottomCenter = start;

	for (int i = 0; i < numSlices; ++i)
	{
		float angle0 = i * angleStep;
		float angle1 = (i + 1) * angleStep;

		Vec3 v0 = baseCircle[i];
		Vec3 v1 = baseCircle[i + 1];

		Vec2 uv0 = uvCenter + 0.5f * UVs.GetDimensions() * Vec2(CosDegrees(angle0), -SinDegrees(angle0));
		Vec2 uv1 = uvCenter + 0.5f * UVs.GetDimensions() * Vec2(CosDegrees(angle1), -SinDegrees(angle1));

		verts.push_back(Vertex_PCU(bottomCenter, color, uvCenter));
		verts.push_back(Vertex_PCU(v1, color, uv1));
		verts.push_back(Vertex_PCU(v0, color, uv0));
	}
}

void AddVertsForCone3D(std::vector<Vertex_PCU>& verts, const Vec3& start, const Vec3& end, float radius, const Rgba8& color, const AABB2& UVs, int numSlices)
{
	if (numSlices < 3) return;

	Vec3 kBasis = (end - start).GetNormalized(); 

	Vec3 jBasis = CrossProduct3D(kBasis, Vec3(1.f, 0.f, 0.f));

	Vec3 iBasis;
	if (jBasis.GetLengthSquared() == 0.f) 
	{
		iBasis = CrossProduct3D(Vec3(0.f, 1.f, 0.f), kBasis).GetNormalized();
		jBasis = CrossProduct3D(kBasis, iBasis).GetNormalized();
	}
	else
	{
		iBasis = CrossProduct3D(jBasis, kBasis).GetNormalized();
		jBasis = jBasis.GetNormalized();
	}

	float angleStep = 360.0f / static_cast<float>(numSlices);

	std::vector<Vec3> baseCircle;
	//for locating the verts
	for (int i = 0; i <= numSlices; ++i)
	{
		float angle = i * angleStep;
		float cosA = CosDegrees(angle);
		float sinA = SinDegrees(angle);

		Vec3 circleOffset = (iBasis * cosA + jBasis * sinA) * radius;
		baseCircle.push_back(start + circleOffset);
	}
	//add sides
	for (int i = 0; i < numSlices; ++i)
	{
		Vec3 v0 = baseCircle[i];
		Vec3 v1 = baseCircle[i + 1];

		verts.push_back(Vertex_PCU(v0, color, UVs.m_mins));
		verts.push_back(Vertex_PCU(v1, color, Vec2(UVs.m_maxs.x, UVs.m_mins.y)));
		verts.push_back(Vertex_PCU(end, color, UVs.m_maxs)); 
	}

	//bottom
	Vec3 bottomCenter = start;
	for (int i = 0; i < numSlices; ++i)
	{
		Vec3 v0 = baseCircle[i + 1];
		Vec3 v1 = baseCircle[i];

		verts.push_back(Vertex_PCU(bottomCenter, color, UVs.m_mins));
		verts.push_back(Vertex_PCU(v0, color, Vec2(UVs.m_mins.x, UVs.m_maxs.y)));
		verts.push_back(Vertex_PCU(v1, color, UVs.m_maxs));
	}
}

void AddVertsForArrow3D(std::vector<Vertex_PCU>& verts, const Vec3& start, const Vec3& end, float radius, const Rgba8& color, const AABB2& UVs, int numSlices)
{
	Vec3 mid = Vec3((start.x + end.x) / 2.f, (start.y + end.y) / 2.f, (start.z + end.z) / 2.f);
	AddVertsForCylinder3D(verts, start, mid, radius, color, UVs, numSlices);
	//Vec3 normalDir = (end - start).GetNormalized();
	//Vec3 coneEnd = end + normalDir * coneHeight;
	AddVertsForCone3D(verts, mid, end, radius * 1.5f, color, UVs, numSlices);
}

void AddVertsForIndexQuad3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, const Vec3& bottomLeft, const Vec3& bottomRight, const Vec3& topRight, const Vec3& topLeft, const Rgba8& color, const AABB2& UVs)
{
	unsigned int vertOffset = (unsigned int)verts.size();

	Vec2 uvBL = Vec2(UVs.m_mins.x, UVs.m_mins.y);
	Vec2 uvBR = Vec2(UVs.m_maxs.x, UVs.m_mins.y);
	Vec2 uvTR = Vec2(UVs.m_maxs.x, UVs.m_maxs.y);
	Vec2 uvTL = Vec2(UVs.m_mins.x, UVs.m_maxs.y);

	Vec3 normal = CrossProduct3D((bottomRight - bottomLeft).GetNormalized(), (topLeft - bottomLeft).GetNormalized()).GetNormalized();
	Vec3 tangent = Vec3(0.f, 0.f, 0.f);
	Vec3 biNormal = Vec3(0.f, 0.f, 0.f);
	verts.push_back(Vertex_PCUTBN(bottomLeft, color, uvBL, tangent, biNormal, normal));
	verts.push_back(Vertex_PCUTBN(bottomRight, color, uvBR, tangent, biNormal, normal));
	verts.push_back(Vertex_PCUTBN(topRight, color, uvTR, tangent, biNormal, normal));
	verts.push_back(Vertex_PCUTBN(topLeft, color, uvTL, tangent, biNormal, normal));

	indexes.push_back(vertOffset + 0);
	indexes.push_back(vertOffset + 1);
	indexes.push_back(vertOffset + 2);

	indexes.push_back(vertOffset + 0);
	indexes.push_back(vertOffset + 2);
	indexes.push_back(vertOffset + 3);

	//CalculateTangentAndBiTangent(verts, indexes); TODO: uncomment it?
}

void AddVertsForIndexSphere3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, Vec3 const& center,
	float radius, int numSlices, int numStacks, Rgba8 const& color, AABB2 const& UVs)
{
	//int NUM_VERTS = numSlices * numSlices * 2 * 3;
	//verts.reserve(verts.size() + NUM_VERTS);

	float degreePerSlice = 360.f / numSlices;
	float degreePerStack = 180.f / numStacks;

	float uPerSlice = UVs.GetDimensions().x / numSlices;
	float vPerStack = UVs.GetDimensions().y / numStacks;

	int numofVerts = 2 + (numStacks - 1) * numSlices;
	int numofIndexes = 3 * ((numStacks - 2) * numSlices * 2 + 2 * numSlices);
	verts.reserve(verts.size() + numofVerts);
	indexes.reserve(indexes.size() + numofIndexes);

	int base = (int)verts.size();

	Vertex_PCUTBN bottom;
	bottom.m_position = center - radius * Vec3(0.f, 0.f, 1.f);
	bottom.m_color = color;
	bottom.m_uvTexCoords = Vec2(0.f, 0.f);
	verts.push_back(bottom);

	for (int stackIndex = 0; stackIndex < numStacks; stackIndex++)
	{
		if (stackIndex == 0)
			continue;
		for (int sliceIndex = 0; sliceIndex < numSlices; sliceIndex++)
		{
			float startSliceDegree = GetClamped((float)sliceIndex * degreePerSlice, 0.f, 360.f);
			float startStackDegree = GetClamped((float)stackIndex * degreePerStack - 90.f, -90.f, 90.f);
			float startSliceU = GetClamped((float)sliceIndex * uPerSlice + UVs.m_mins.x, UVs.m_mins.x, UVs.m_maxs.x);
			float startStackV = GetClamped((float)stackIndex * vPerStack + UVs.m_mins.y, UVs.m_mins.y, UVs.m_maxs.y);
			Vec3 BL = Vec3::MakeFromPolarDegrees(startStackDegree, startSliceDegree, radius) + center;
			Vec2 uvAtMins = Vec2(startSliceU, startStackV);

			Vec3 normal = (BL - center).GetNormalized();
			Vec3 tangent = Vec3(-SinDegrees(startSliceDegree), CosDegrees(startSliceDegree), 0.f).GetNormalized();
			Vec3 bitangent = CrossProduct3D(normal, tangent).GetNormalized();

			verts.emplace_back(Vertex_PCUTBN(BL, color, uvAtMins, tangent, bitangent, normal));
		}
	}

	Vertex_PCUTBN top;
	top.m_position = center + radius * Vec3(0.f, 0.f, 1.f);
	top.m_color = color;
	top.m_uvTexCoords = Vec2(1.f, 1.f);
	verts.push_back(top);
	int full = (int)verts.size() - 1;
	for (int stackIndex = 0; stackIndex < numStacks; stackIndex++)
	{
		for (int sliceIndex = 0; sliceIndex < numSlices; sliceIndex++)
		{
			if (stackIndex == 0)
			{
				indexes.push_back(base);
				if (sliceIndex != numSlices - 1)
					indexes.push_back(base + sliceIndex + 2);
				else indexes.push_back(base + 1);
				indexes.push_back(base + sliceIndex + 1);
			}
			else if (stackIndex == numStacks - 1)
			{
				indexes.push_back(full);
				indexes.push_back(base + (stackIndex - 1) * numSlices + sliceIndex + 1);
				if (sliceIndex == numSlices - 1)
						indexes.push_back(base + (stackIndex - 1) * numSlices + 1);
				else
					indexes.push_back(base + (stackIndex - 1) * numSlices + sliceIndex + 2);
			}
			else
			{
				int BL = base + (stackIndex - 1) * numSlices + sliceIndex + 1;
				int BR = base + (stackIndex - 1) * numSlices + sliceIndex + 2;
				int TL = base + (stackIndex) * numSlices + sliceIndex + 1;
				int TR = base + (stackIndex) * numSlices + sliceIndex + 2;

				if (sliceIndex == numSlices - 1)
				{
					BR = base + (stackIndex - 1) * numSlices + 1;
					TR = base + (stackIndex)*numSlices + 1;
				}
				Vec3 x = verts[BR].m_position - verts[BL].m_position;
				Vec3 y = verts[TR].m_position - verts[BR].m_position;

				Vec3 normal = CrossProduct3D(x, y).GetNormalized();
				verts[BL].m_normal = normal;
				verts[BR].m_normal = normal;
				verts[TL].m_normal = normal;
				verts[TR].m_normal = normal;

				indexes.push_back(BL);
				indexes.push_back(BR);
				indexes.push_back(TR);

				indexes.push_back(BL);
				indexes.push_back(TR);
				indexes.push_back(TL);
			}
		}
	}
}

void AddVertsForIndexCylinder3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, const Vec3& start,
	const Vec3& end, float radius, const Rgba8& color, const AABB2& UVs, int numSlices)
{
	if (numSlices < 3) return;

    int baseIndex = (int)verts.size();

    Vec3 up = (end - start).GetNormalized();
    Vec3 iBasis = CrossProduct3D(up, Vec3(1.f, 0.f, 0.f));
    if (iBasis.GetLengthSquared() < 1e-5f)
        iBasis = CrossProduct3D(Vec3(0.f, 1.f, 0.f), up);
    iBasis = iBasis.GetNormalized();
    Vec3 jBasis = CrossProduct3D(up, iBasis).GetNormalized();

    Vec2 uvCenter = UVs.GetCenter();
    Vec2 uvHalfSize = 0.5f * UVs.GetDimensions();
    float vBottom = UVs.m_mins.y;
    float vTop = UVs.m_maxs.y;

    verts.push_back(Vertex_PCUTBN(start, color, uvCenter, -iBasis,  -jBasis, -up)); // baseIndex + 0 = bottom center
    verts.push_back(Vertex_PCUTBN(end,   color, uvCenter,  iBasis, jBasis,  up)); // baseIndex + 1 = top center

    std::vector<int> bottomRim;
    std::vector<int> topRim;

    for (int i = 0; i <= numSlices; ++i)
    {
        float t = static_cast<float>(i) / numSlices;
        float angle = 360.f * t;
        float u = RangeMap(t, 0.f, 1.f, UVs.m_mins.x, UVs.m_maxs.x);

        Vec3 offset = (iBasis * CosDegrees(angle) + jBasis * SinDegrees(angle)) * radius;
        Vec3 rimDir = offset.GetNormalized();  // outward

        Vec3 tangent = CrossProduct3D(up, rimDir).GetNormalized();     // tangent: U+
        Vec3 bitangent = CrossProduct3D(rimDir, tangent).GetNormalized(); // V+

        Vec3 bPos = start + offset;
        Vec3 tPos = end + offset;

        int bIdx = (int)verts.size(); verts.push_back(Vertex_PCUTBN(bPos, color, Vec2(u, vBottom), tangent, bitangent, rimDir));
        int tIdx = (int)verts.size(); verts.push_back(Vertex_PCUTBN(tPos, color, Vec2(u, vTop), tangent, bitangent, rimDir));

        bottomRim.push_back(bIdx);
        topRim.push_back(tIdx);
    }
    for (int i = 0; i < numSlices; ++i)
    {
        int b0 = bottomRim[i];
        int b1 = bottomRim[i + 1];
        int t0 = topRim[i];
        int t1 = topRim[i + 1];

        indexes.push_back(b0); indexes.push_back(b1); indexes.push_back(t1);
        indexes.push_back(b0); indexes.push_back(t1); indexes.push_back(t0);
    }

    for (int i = 0; i < numSlices; ++i)
    {
        float angle0 = 360.f * i / numSlices;
        float angle1 = 360.f * (i + 1) / numSlices;

        Vec3 offset0 = (iBasis * CosDegrees(angle0) + jBasis * SinDegrees(angle0)) * radius;
        Vec3 offset1 = (iBasis * CosDegrees(angle1) + jBasis * SinDegrees(angle1)) * radius;

        Vec2 uv0 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle0), SinDegrees(angle0));
        Vec2 uv1 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle1), SinDegrees(angle1));

        Vec3 pos0 = start + offset0;
        Vec3 pos1 = start + offset1;

        //Vec3 tangent = (offset1 - offset0).GetNormalized();
        Vec3 tangent = -iBasis;
        Vec3 normal = -up;
        Vec3 bitangent = CrossProduct3D(normal, tangent).GetNormalized();

        int i0 = (int)verts.size(); verts.push_back(Vertex_PCUTBN(pos0, color, uv0, tangent, bitangent, normal));
        int i1 = (int)verts.size(); verts.push_back(Vertex_PCUTBN(pos1, color, uv1, tangent, bitangent, normal));

        indexes.push_back(baseIndex); // bottom center
        indexes.push_back(i1);
        indexes.push_back(i0);
    }

    for (int i = 0; i < numSlices; ++i)
    {
        float angle0 = 360.f * i / numSlices;
        float angle1 = 360.f * (i + 1) / numSlices;

        Vec3 offset0 = (iBasis * CosDegrees(angle0) + jBasis * SinDegrees(angle0)) * radius;
        Vec3 offset1 = (iBasis * CosDegrees(angle1) + jBasis * SinDegrees(angle1)) * radius;

        Vec2 uv0 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle0), SinDegrees(angle0));
        Vec2 uv1 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle1), SinDegrees(angle1));

        Vec3 pos0 = end + offset0;
        Vec3 pos1 = end + offset1;

        //Vec3 tangent = (offset1 - offset0).GetNormalized();
        Vec3 tangent = iBasis;
        Vec3 normal = up;
        Vec3 bitangent = CrossProduct3D(normal, tangent).GetNormalized();

        int i0 = (int)verts.size(); verts.push_back(Vertex_PCUTBN(pos0, color, uv0, tangent, bitangent, normal));
        int i1 = (int)verts.size(); verts.push_back(Vertex_PCUTBN(pos1, color, uv1, tangent, bitangent, normal));

        indexes.push_back(baseIndex + 1); // top center
        indexes.push_back(i0);
        indexes.push_back(i1);
    }
}

void AddVertsForIndexCylinderZ3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indices,
	Vec2 const& centerXY, FloatRange const& minMaxZ, float radius, Rgba8 const& tint, AABB2 const& UVs, int numSlices)
{
	if (numSlices < 3) return;

	int baseIndex = static_cast<int>(verts.size());

	Vec3 start(centerXY.x, centerXY.y, minMaxZ.m_min);
	Vec3 end(centerXY.x, centerXY.y, minMaxZ.m_max);
	Vec3 up = Vec3(0.f,0.f,1.f);

	Vec3 iBasis = Vec3(1.0f, 0.f, 0.f);
	Vec3 jBasis = Vec3(0.f, 1.f, 0.f);

	Vec2 uvCenter = UVs.GetCenter();
	Vec2 uvHalfSize = 0.5f * UVs.GetDimensions();
	float vBottom = UVs.m_mins.y;
	float vTop = UVs.m_maxs.y;

	verts.push_back(Vertex_PCUTBN(start, tint, uvCenter, -iBasis, -jBasis, -up)); // baseIndex + 0
	verts.push_back(Vertex_PCUTBN(end,   tint, uvCenter,  iBasis,  jBasis,  up)); // baseIndex + 1

	std::vector<int> bottomRim;
	std::vector<int> topRim;

	for (int i = 0; i <= numSlices; ++i)
	{
		float t = static_cast<float>(i) / numSlices;
		float angle = 360.f * t;
		float u = RangeMap(t, 0.f, 1.f, UVs.m_mins.x, UVs.m_maxs.x);
		
		Vec3 offset = (iBasis * CosDegrees(angle) + jBasis * SinDegrees(angle)) * radius;
		Vec3 rimDir = offset.GetNormalized();
		Vec3 tangent = CrossProduct3D(up, rimDir).GetNormalized();
		Vec3 bitangent = CrossProduct3D(rimDir, tangent).GetNormalized();

		Vec3 bPos = start + offset;
		Vec3 tPos = end + offset;

		int bIdx = (int)verts.size(); verts.push_back(Vertex_PCUTBN(bPos, tint, Vec2(u, vBottom), tangent, bitangent, rimDir));
		int tIdx = (int)verts.size(); verts.push_back(Vertex_PCUTBN(tPos, tint, Vec2(u, vTop), tangent, bitangent, rimDir));

		if (i < numSlices)
		{
			bottomRim.push_back(bIdx);
			topRim.push_back(tIdx);
		}
	}

	for (int i = 0; i < numSlices; i++)
	{
		int b0 = bottomRim[i];
		int b1 = bottomRim[(i + 1) % numSlices];
		int t0 = topRim[i];
		int t1 = topRim[(i + 1) % numSlices];

		indices.push_back(b0); indices.push_back(b1); indices.push_back(t1);
		indices.push_back(b0); indices.push_back(t1); indices.push_back(t0);
	}

	Vec3 bottomTangent = -iBasis;
	Vec3 bottomNormal = -up;
	Vec3 bottomBitangent = CrossProduct3D(bottomNormal, bottomTangent).GetNormalized();

	for (int i = 0; i < numSlices; ++i)
	{
		float angle0 = 360.f * i / numSlices;
		float angle1 = 360.f * (i + 1) / numSlices;

		Vec2 uv0 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle0), SinDegrees(angle0));
		Vec2 uv1 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle1), SinDegrees(angle1));

		Vec3 offset0 = (iBasis * CosDegrees(angle0) + jBasis * SinDegrees(angle0)) * radius;
		Vec3 offset1 = (iBasis * CosDegrees(angle1) + jBasis * SinDegrees(angle1)) * radius;

		Vec3 pos0 = start + offset0;
		Vec3 pos1 = start + offset1;

		int i0 = (int)verts.size(); verts.emplace_back(pos0, tint, uv0, bottomTangent, bottomBitangent, bottomNormal);
		int i1 = (int)verts.size(); verts.emplace_back(pos1, tint, uv1, bottomTangent, bottomBitangent, bottomNormal);

		indices.push_back(baseIndex + 0);
		indices.push_back(i1);
		indices.push_back(i0);
	}

	Vec3 topTangent = iBasis;
	Vec3 topNormal = up;
	Vec3 topBitangent = CrossProduct3D(topNormal, topTangent).GetNormalized();

	for (int i = 0; i < numSlices; ++i)
	{
		float angle0 = 360.f * i / numSlices;
		float angle1 = 360.f * (i + 1) / numSlices;

		Vec2 uv0 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle0), SinDegrees(angle0));
		Vec2 uv1 = uvCenter + uvHalfSize * Vec2(CosDegrees(angle1), SinDegrees(angle1));

		Vec3 offset0 = (iBasis * CosDegrees(angle0) + jBasis * SinDegrees(angle0)) * radius;
		Vec3 offset1 = (iBasis * CosDegrees(angle1) + jBasis * SinDegrees(angle1)) * radius;

		Vec3 pos0 = end + offset0;
		Vec3 pos1 = end + offset1;

		int i0 = (int)verts.size(); verts.emplace_back(pos0, tint, uv0, topTangent, topBitangent, topNormal);
		int i1 = (int)verts.size(); verts.emplace_back(pos1, tint, uv1, topTangent, topBitangent, topNormal);

		indices.push_back(baseIndex + 1);
		indices.push_back(i0);
		indices.push_back(i1);
	}
}

void AddVertsForIndexAABB3D(std::vector<Vertex_PCUTBN>& vertexes, std::vector<unsigned int>& indexes, const AABB3& cube,
                            const Rgba8& color, const AABB2& UVs)
{
	vertexes.reserve(vertexes.size() + 24);

	Vec2 uvAtMins = UVs.m_mins;
	Vec2 uvAtMaxs = UVs.m_maxs;

	float minX = cube.m_mins.x;
	float minY = cube.m_mins.y;
	float minZ = cube.m_mins.z;
	float maxX = cube.m_maxs.x;
	float maxY = cube.m_maxs.y;
	float maxZ = cube.m_maxs.z;

	// +x
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(maxX, minY, minZ), Vec3(maxX, maxY, minZ), Vec3(maxX, maxY, maxZ), Vec3(maxX, minY, maxZ), color, UVs);
	// -x
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(minX, maxY, minZ), Vec3(minX, minY, minZ), Vec3(minX, minY, maxZ), Vec3(minX, maxY, maxZ), color, UVs);
	// +y
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(maxX, maxY, minZ), Vec3(minX, maxY, minZ), Vec3(minX, maxY, maxZ), Vec3(maxX, maxY, maxZ), color, UVs);
	// -y
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(minX, minY, minZ), Vec3(maxX, minY, minZ), Vec3(maxX, minY, maxZ), Vec3(minX, minY, maxZ), color, UVs);
	// +z
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(minX, minY, maxZ), Vec3(maxX, minY, maxZ), Vec3(maxX, maxY, maxZ), Vec3(minX, maxY, maxZ), color, UVs);
	// -z
	AddVertsForIndexQuad3D(vertexes, indexes, Vec3(minX, maxY, minZ), Vec3(maxX, maxY, minZ), Vec3(maxX, minY, minZ), Vec3(minX, minY, minZ), color, UVs);

	CalculateTangentAndBiTangent(vertexes, indexes);
}

void AddVertsForIndexOBB3D(std::vector<Vertex_PCUTBN>& verts, std::vector<unsigned int>& indexes, const OBB3& cube,
	const Rgba8& color, const AABB2& UVs)
{
	Vec3 xFwd = cube.m_iBasisNormal * cube.m_halfDimensions.x;
	Vec3 yLeft = cube.m_jBasisNormal * cube.m_halfDimensions.y;
	Vec3 zUp = cube.m_kBasisNormal * cube.m_halfDimensions.z;
	Vec3 center = cube.m_center;

	Vec3 corners[8];
	corners[0] = center - xFwd - yLeft - zUp; // LBB
	corners[1] = center + xFwd - yLeft - zUp; // RBB
	corners[2] = center + xFwd + yLeft - zUp; // RTB
	corners[3] = center - xFwd + yLeft - zUp; // LTB
	corners[4] = center - xFwd - yLeft + zUp; // LBF
	corners[5] = center + xFwd - yLeft + zUp; // RBF
	corners[6] = center + xFwd + yLeft + zUp; // RTF
	corners[7] = center - xFwd + yLeft + zUp; // LTF

	struct Face {
		int i0, i1, i2, i3;
		Vec3 normal;
	};

	Face faces[6] = {
		{ 0, 1, 2, 3, -zUp }, // back
		{ 5, 4, 7, 6, zUp },  // front
		{ 4, 0, 3, 7, -xFwd },   // left
		{ 1, 5, 6, 2, xFwd },    // right
		{ 4, 5, 1, 0, -yLeft },      // bottom
		{ 3, 2, 6, 7, yLeft },       // top
	};

	unsigned int startIndex = static_cast<unsigned int>(verts.size());

	for (int f = 0; f < 6; ++f)
	{
		const Face& face = faces[f];
		Vec3 n = face.normal;
		Vec3 tangent = (corners[face.i1] - corners[face.i0]).GetNormalized();
		Vec3 bitangent = CrossProduct3D(n, tangent).GetNormalized();

		Vec2 uv[4] = {
			Vec2(UVs.m_mins.x, UVs.m_maxs.y), // LT
			Vec2(UVs.m_maxs.x, UVs.m_maxs.y), // RT
			Vec2(UVs.m_maxs.x, UVs.m_mins.y), // RB
			Vec2(UVs.m_mins.x, UVs.m_mins.y)  // LB
		};

		verts.emplace_back(corners[face.i0], color, uv[3], tangent, bitangent, n); // LB
		verts.emplace_back(corners[face.i1], color, uv[2], tangent, bitangent, n); // RB
		verts.emplace_back(corners[face.i2], color, uv[1], tangent, bitangent, n); // RT
		verts.emplace_back(corners[face.i3], color, uv[0], tangent, bitangent, n); // LT

		unsigned int idx = startIndex + f * 4;
		indexes.push_back(idx + 0);
		indexes.push_back(idx + 1);
		indexes.push_back(idx + 2);
		indexes.push_back(idx + 0);
		indexes.push_back(idx + 2);
		indexes.push_back(idx + 3);
	}
}

void AddVertsForIndexAABBZWireframe3D(std::vector<Vertex_PCU>& verts, std::vector<unsigned int>& indices,
	AABB3 const& bounds, Rgba8 const& tint)
{
	unsigned int baseIndex = static_cast<unsigned int>(verts.size());
    
	float x0 = bounds.m_mins.x;  // W
	float x1 = bounds.m_maxs.x;  // E
	float y0 = bounds.m_mins.y;  // S
	float y1 = bounds.m_maxs.y;  // N
	float z0 = bounds.m_mins.z;  
	float z1 = bounds.m_maxs.z;  
	
	verts.push_back(Vertex_PCU(Vec3(x0, y0, z0), tint, Vec2(0, 0))); // 0
	verts.push_back(Vertex_PCU(Vec3(x1, y0, z0), tint, Vec2(0, 0))); // 1
	verts.push_back(Vertex_PCU(Vec3(x1, y1, z0), tint, Vec2(0, 0))); // 2
	verts.push_back(Vertex_PCU(Vec3(x0, y1, z0), tint, Vec2(0, 0))); // 3
	verts.push_back(Vertex_PCU(Vec3(x0, y0, z1), tint, Vec2(0, 0))); // 4
	verts.push_back(Vertex_PCU(Vec3(x1, y0, z1), tint, Vec2(0, 0))); // 5
	verts.push_back(Vertex_PCU(Vec3(x1, y1, z1), tint, Vec2(0, 0))); // 6
	verts.push_back(Vertex_PCU(Vec3(x0, y1, z1), tint, Vec2(0, 0))); // 7
    
	unsigned int edgeIndices[24] = {
		0, 1,  
		1, 2,  
		2, 3,  
		3, 0,  
		
		4, 5, 
		5, 6, 
		6, 7, 
		7, 4, 
        
		0, 4,  
		1, 5,  
		2, 6,  
		3, 7   
	};
    
	for (int i = 0; i < 24; i++)
	{
		indices.push_back(baseIndex + edgeIndices[i]);
	}
}

void CalculateTangentAndBiTangent(std::vector<Vertex_PCUTBN>& vertices, const std::vector<unsigned int>& indices)
{
	std::vector<Vec3> tempTangents(vertices.size(), Vec3());
	std::vector<Vec3> tempBitangents(vertices.size(), Vec3());

	for (int i = 0; i < (int)indices.size(); i += 3)
	{
		unsigned int i0 = indices[i];
		unsigned int i1 = indices[i + 1];
		unsigned int i2 = indices[i + 2];

		Vec3 p0 = vertices[i0].m_position;
		Vec3 p1 = vertices[i1].m_position;
		Vec3 p2 = vertices[i2].m_position;

		Vec2 w0 = vertices[i0].m_uvTexCoords;
		Vec2 w1 = vertices[i1].m_uvTexCoords;
		Vec2 w2 = vertices[i2].m_uvTexCoords;

		Vec3 e1 = p1 - p0;
		Vec3 e2 = p2 - p0;
		float x1 = w1.x - w0.x;
		float x2 = w2.x - w0.x;
		float y1 = w1.y - w0.y;
		float y2 = w2.y - w0.y;

		float denom = (x1 * y2 - x2 * y1);
		if (denom == 0.f) continue;

		float r = 1.f / denom;

		Vec3 t = (e1 * y2 - e2 * y1) * r;
		Vec3 b = (e2 * x1 - e1 * x2) * r;

		tempTangents[i0] += t;
		tempTangents[i1] += t;
		tempTangents[i2] += t;

		tempBitangents[i0] += b;
		tempBitangents[i1] += b;
		tempBitangents[i2] += b;
	}

	for (int i = 0; i < (int)vertices.size(); ++i)
	{
		Vec3 t = tempTangents[i];
		Vec3 b = tempBitangents[i];
		Vec3 n = vertices[i].m_normal;

		t = (t - n * DotProduct3D(n, t)).GetNormalized();
		b = (b - t * DotProduct3D(t, b) - n * DotProduct3D(n, b)).GetNormalized();

		vertices[i].m_tangent = t;
		vertices[i].m_bitangent = b;
	}
}

void AddVertsForRoundedQuad3D(std::vector<Vertex_PCUTBN>& vertexes, const Vec3& topLeft, const Vec3& bottomLeft,
	const Vec3& bottomRight, const Vec3& topRight, const Rgba8& color, const AABB2& UVs)
{
	Vec3 topCenter = (topLeft + topRight)/2.f;
	Vec3 bottomCenter = (bottomLeft + bottomRight)/2.f;
	Vec3 normal = CrossProduct3D(bottomRight - bottomLeft, topLeft - bottomLeft).GetNormalized();
	Vec3 tangent = (bottomRight - bottomLeft).GetNormalized();
	Vec3 bitangent = (topLeft - bottomLeft).GetNormalized();

	Vec2 uvBL = UVs.m_mins;
	Vec2 uvTL = Vec2(UVs.m_mins.x, UVs.m_maxs.y);
	Vec2 uvBR = Vec2(UVs.m_maxs.x, UVs.m_mins.y);
	Vec2 uvTR = UVs.m_maxs;
	Vec2 uvCT = Vec2((uvTL.x + uvTR.x) * 0.5f, uvTR.y);
	Vec2 uvCB = Vec2((uvBL.x + uvBR.x) * 0.5f, uvBL.y);

	vertexes.push_back(Vertex_PCUTBN(bottomCenter,	color, uvCB,  normal,  bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topLeft,		color, uvTL, -tangent, bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(bottomLeft,	color, uvBL, -tangent, bitangent, normal));

	vertexes.push_back(Vertex_PCUTBN(bottomCenter,	color, uvCB, normal,   bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topCenter,		color, uvCT, normal,   bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topLeft,		color, uvTL, -tangent, bitangent, normal));

	vertexes.push_back(Vertex_PCUTBN(bottomCenter,	color, uvCB, normal,   bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(bottomRight,	color, uvBR, tangent,  bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topCenter,		color, uvCT, normal,   bitangent, normal));

	vertexes.push_back(Vertex_PCUTBN(bottomRight,	color, uvBR, tangent,  bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topRight,		color, uvTR, tangent,  bitangent, normal));
	vertexes.push_back(Vertex_PCUTBN(topCenter,		color, uvCT, normal,   bitangent, normal));
}

void AddVertsForSphere3D(std::vector<Vertex_PCUTBN>& verts, const Vec3& center, float radius, const Rgba8& color,
	const AABB2& UVs, int numSlices, int numStacks)
{
	float longitudeDegree = 360.f / static_cast<float>(numSlices);
    float latitudeDegree = 180.f / static_cast<float>(numStacks);
    float uPerPiece = UVs.GetDimensions().x / static_cast<float>(numSlices);
    float vPerPiece = UVs.GetDimensions().y / static_cast<float>(numStacks);

    for (int loIndex = 0; loIndex < numSlices; loIndex++)
    {
        for (int laIndex = 0; laIndex < numStacks; laIndex++)
        {
            // Angles for current quad
            float lat0 = -90.f + laIndex * latitudeDegree;
            float lat1 = -90.f + (laIndex + 1) * latitudeDegree;
            float lon0 = loIndex * longitudeDegree;
            float lon1 = (loIndex + 1) * longitudeDegree;

            // Points on sphere
            Vec3 BL = Vec3::MakeFromPolarDegrees(lat0, lon0, radius) + center;
            Vec3 BR = Vec3::MakeFromPolarDegrees(lat0, lon1, radius) + center;
            Vec3 TL = Vec3::MakeFromPolarDegrees(lat1, lon0, radius) + center;
            Vec3 TR = Vec3::MakeFromPolarDegrees(lat1, lon1, radius) + center;

            // UVs
            Vec2 uvBL = Vec2(UVs.m_mins.x + loIndex * uPerPiece, UVs.m_mins.y + laIndex * vPerPiece);
            Vec2 uvBR = Vec2(UVs.m_mins.x + (loIndex + 1) * uPerPiece, UVs.m_mins.y + laIndex * vPerPiece);
            Vec2 uvTL = Vec2(UVs.m_mins.x + loIndex * uPerPiece, UVs.m_mins.y + (laIndex + 1) * vPerPiece);
            Vec2 uvTR = Vec2(UVs.m_mins.x + (loIndex + 1) * uPerPiece, UVs.m_mins.y + (laIndex + 1) * vPerPiece);

            // Normals (from center to point)
            Vec3 normalBL = (BL - center).GetNormalized();
            Vec3 normalBR = (BR - center).GetNormalized();
            Vec3 normalTL = (TL - center).GetNormalized();
            Vec3 normalTR = (TR - center).GetNormalized();

            // Approximate tangents and bitangents (local sphere orientation)
            Vec3 tangent = Vec3(0.f, 1.f, 0.f);     // Approximate tangent (horizontal on sphere)
            Vec3 bitangent = CrossProduct3D(tangent, normalBL); // Compute bitangent

            // Triangle 1
            verts.push_back(Vertex_PCUTBN(BL, color, uvBL, tangent, bitangent, normalBL));
            verts.push_back(Vertex_PCUTBN(BR, color, uvBR, tangent, bitangent, normalBR));
            verts.push_back(Vertex_PCUTBN(TR, color, uvTR, tangent, bitangent, normalTR));

            // Triangle 2
            verts.push_back(Vertex_PCUTBN(BL, color, uvBL, tangent, bitangent, normalBL));
            verts.push_back(Vertex_PCUTBN(TR, color, uvTR, tangent, bitangent, normalTR));
            verts.push_back(Vertex_PCUTBN(TL, color, uvTL, tangent, bitangent, normalTL));
        }
    }
}

void AddVertsForCylinderZ3D(std::vector<Vertex_PCU>& verts, Vec2 const& centerXY, FloatRange const& minMaxZ, float radius, int numSlices, Rgba8 const& tint /*= Rgba8::WHITE*/, AABB2 const& UVs /*= AABB2::ZERO_TO_ONE*/)
{
	AddVertsForCylinder3D(verts, Vec3(centerXY.x, centerXY.y, minMaxZ.m_min), Vec3(centerXY.x, centerXY.y, minMaxZ.m_max), radius, tint, UVs, numSlices);
}

void AddVertsForCylinderZWireframe3D(std::vector<Vertex_PCU>& verts, Vec2 const& centerXY, FloatRange const& minMaxZ, float radius, int numSlices, float lineThickness, Rgba8 const& tint /*= Rgba8::WHITE*/)
{
	if (numSlices < 3) return; 

	float angleStep = 360.0f / numSlices;
	float cylinderRadius = lineThickness * 0.5f;

	std::vector<Vec3> bottomCircle;
	std::vector<Vec3> topCircle;

	for (int i = 0; i <= numSlices; ++i)
	{
		float angle = i * angleStep;
		float cosA = CosDegrees(angle);
		float sinA = SinDegrees(angle);

		Vec3 pointBottom = Vec3(centerXY.x + radius * cosA, centerXY.y + radius * sinA, minMaxZ.m_min);
		Vec3 pointTop = Vec3(centerXY.x + radius * cosA, centerXY.y + radius * sinA, minMaxZ.m_max);

		bottomCircle.push_back(pointBottom);
		topCircle.push_back(pointTop);
	}

	for (int i = 0; i < numSlices; ++i)
	{
		AddVertsForCylinder3D(verts, bottomCircle[i], bottomCircle[i + 1], cylinderRadius, tint);
	}

	for (int i = 0; i < numSlices; ++i)
	{
		AddVertsForCylinder3D(verts, topCircle[i], topCircle[i + 1], cylinderRadius, tint);
	}

	for (int i = 0; i < numSlices; ++i)
	{
		AddVertsForCylinder3D(verts, bottomCircle[i], topCircle[i], cylinderRadius, tint);
	}
}

void AddVertsForOBB3D(std::vector<Vertex_PCU>& verts, OBB3 const& box, Rgba8 const& tint, AABB2 const& UVs)
{
	Vec3 p[8];
	box.GetCornerPoints(p);

	AddVertsForQuad3D(verts, p[1], p[3], p[2], p[0], tint, UVs);

	AddVertsForQuad3D(verts, p[5], p[4], p[6], p[7], tint, UVs);

	AddVertsForQuad3D(verts, p[0], p[4], p[5], p[1], tint, UVs);

	AddVertsForQuad3D(verts, p[2], p[3], p[7], p[6], tint, UVs);

	AddVertsForQuad3D(verts, p[0], p[2], p[6], p[4], tint, UVs);

	AddVertsForQuad3D(verts, p[1], p[5], p[7], p[3], tint, UVs);
}

void AddVertsForOBBWireframe3D(std::vector<Vertex_PCU>& verts, OBB3 const& box, float lineThickness, Rgba8 const& tint)
{
	Vec3 p[8];
	box.GetCornerPoints(p);                            

	AddVertsForCylinder3D(verts, p[0], p[1], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[1], p[5], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[5], p[4], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[4], p[0], lineThickness, tint);

	// top ring
	AddVertsForCylinder3D(verts, p[2], p[3], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[3], p[7], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[7], p[6], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[6], p[2], lineThickness, tint);

	// vertical struts
	AddVertsForCylinder3D(verts, p[0], p[2], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[1], p[3], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[5], p[7], lineThickness, tint);
	AddVertsForCylinder3D(verts, p[4], p[6], lineThickness, tint);
}

void AddVertsForAABBZWireframe3D(std::vector<Vertex_PCU>& verts, AABB3 const& bounds, float lineThickness, Rgba8 const& tint)
{
	float radius = lineThickness * 0.5f;
	Vec3 min = bounds.m_mins;
	Vec3 max = bounds.m_maxs;

	Vec3 v0 = Vec3(min.x, min.y, min.z);
	Vec3 v1 = Vec3(max.x, min.y, min.z);
	Vec3 v2 = Vec3(max.x, max.y, min.z);
	Vec3 v3 = Vec3(min.x, max.y, min.z);
	Vec3 v4 = Vec3(min.x, min.y, max.z);
	Vec3 v5 = Vec3(max.x, min.y, max.z);
	Vec3 v6 = Vec3(max.x, max.y, max.z);
	Vec3 v7 = Vec3(min.x, max.y, max.z);

	AddVertsForCylinder3D(verts, v0, v1, radius, tint);
	AddVertsForCylinder3D(verts, v1, v2, radius, tint);
	AddVertsForCylinder3D(verts, v2, v3, radius, tint);
	AddVertsForCylinder3D(verts, v3, v0, radius, tint);

	AddVertsForCylinder3D(verts, v4, v5, radius, tint);
	AddVertsForCylinder3D(verts, v5, v6, radius, tint);
	AddVertsForCylinder3D(verts, v6, v7, radius, tint);
	AddVertsForCylinder3D(verts, v7, v4, radius, tint);

	AddVertsForCylinder3D(verts, v0, v4, radius, tint);
	AddVertsForCylinder3D(verts, v1, v5, radius, tint);
	AddVertsForCylinder3D(verts, v2, v6, radius, tint);
	AddVertsForCylinder3D(verts, v3, v7, radius, tint);
}

void AddVertsForUVSphereZ3D(std::vector<Vertex_PCU>& verts, Vec3 const& center, float radius, int numSlices, int numStacks, Rgba8 const& tint /*= Rgba8::WHITE*/, AABB2 const& UVs /*= AABB2::ZERO_TO_ONE*/)
{
	float longitudeStep = 360.f / numSlices; 
	float latitudeStep = 180.f / numStacks;  
	float uStep = UVs.GetDimensions().x / numSlices;
	float vStep = UVs.GetDimensions().y / numStacks;

	for (int latIdx = 0; latIdx < numStacks; latIdx++)
	{
		float lat0 = -90.f + latIdx * latitudeStep;       
		float lat1 = -90.f + (latIdx + 1) * latitudeStep; 

		for (int lonIdx = 0; lonIdx < numSlices; lonIdx++)
		{
			float lon0 = lonIdx * longitudeStep;       
			float lon1 = (lonIdx + 1) * longitudeStep; 

			Vec3 BL = Vec3::MakeFromPolarDegrees(lat0, lon0, radius) + center;
			Vec3 BR = Vec3::MakeFromPolarDegrees(lat0, lon1, radius) + center;
			Vec3 TL = Vec3::MakeFromPolarDegrees(lat1, lon0, radius) + center;
			Vec3 TR = Vec3::MakeFromPolarDegrees(lat1, lon1, radius) + center;

			Vec2 uvBL = Vec2(UVs.m_mins.x + lonIdx * uStep, UVs.m_mins.y + latIdx * vStep);
			Vec2 uvBR = Vec2(UVs.m_mins.x + (lonIdx + 1) * uStep, UVs.m_mins.y + latIdx * vStep);
			Vec2 uvTL = Vec2(UVs.m_mins.x + lonIdx * uStep, UVs.m_mins.y + (latIdx + 1) * vStep);
			Vec2 uvTR = Vec2(UVs.m_mins.x + (lonIdx + 1) * uStep, UVs.m_mins.y + (latIdx + 1) * vStep);

			verts.push_back(Vertex_PCU(BL, tint, uvBL));
			verts.push_back(Vertex_PCU(BR, tint, uvBR));
			verts.push_back(Vertex_PCU(TR, tint, uvTR));

			verts.push_back(Vertex_PCU(BL, tint, uvBL));
			verts.push_back(Vertex_PCU(TR, tint, uvTR));
			verts.push_back(Vertex_PCU(TL, tint, uvTL));
		}
	}
}

void AddVertsForUVSphereZWireframe3D(std::vector<Vertex_PCU>& verts, Vec3 const& center, float radius, int numSlices, int numStacks, float lineThickness, Rgba8 const& tint /*= Rgba8::WHITE*/)
{
	float longitudeStep = 360.f / numSlices;
	float latitudeStep = 180.f / numStacks;
	float cylinderRadius = lineThickness * 0.5f;

	std::vector<Vec3> sphereVertices;

	for (int latIdx = 0; latIdx <= numStacks; latIdx++)
	{
		float latitude = -90.f + latIdx * latitudeStep;
		for (int lonIdx = 0; lonIdx <= numSlices; lonIdx++)
		{
			float longitude = lonIdx * longitudeStep;
			Vec3 point = Vec3::MakeFromPolarDegrees(latitude, longitude, radius) + center;
			sphereVertices.push_back(point);
		}
	}

	for (int lonIdx = 0; lonIdx < numSlices; lonIdx++)
	{
		for (int latIdx = 0; latIdx < numStacks; latIdx++)
		{
			int currentIdx = latIdx * (numSlices + 1) + lonIdx;
			int nextIdx = (latIdx + 1) * (numSlices + 1) + lonIdx;

			Vec3 start = sphereVertices[currentIdx];
			Vec3 end = sphereVertices[nextIdx];

			AddVertsForCylinder3D(verts, start, end, cylinderRadius, tint);
		}
	}

	for (int latIdx = 1; latIdx < numStacks; latIdx++) 
	{
		for (int lonIdx = 0; lonIdx < numSlices; lonIdx++)
		{
			int currentIdx = latIdx * (numSlices + 1) + lonIdx;
			int nextIdx = latIdx * (numSlices + 1) + (lonIdx + 1) % numSlices;

			Vec3 start = sphereVertices[currentIdx];
			Vec3 end = sphereVertices[nextIdx];

			AddVertsForCylinder3D(verts, start, end, cylinderRadius, tint);
		}
	}
}
