#include "Engine/Math/MathUtils.hpp"
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/Vec3.hpp"
#include "Engine/Math/Vec4.hpp"
#include "Engine/Math/IntVec2.hpp"
#include "Engine/Math/AABB2.hpp"
#include "Engine/Math/OBB2.hpp"
#include "Engine/Math/FloatRange.hpp"
#include "Engine/Math/Mat44.hpp"
#include "Engine/Math/AABB3.hpp"

#include <math.h>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "IntVec3.h"
#include "RandomNumberGenerator.hpp"

constexpr float PI = 3.1415926535897932384626433832795f;

float ConvertDegreesToRadians(float degrees)
{
	return degrees * (PI / 180.f);
}

float ConvertRadiansToDegrees(float radians)
{
	return radians * (180.f / PI);
}

float CosDegrees(float degrees)
{
	float radians = ConvertDegreesToRadians(degrees);

	return cosf(radians);
}

float SinDegrees(float degrees)
{
	float radians = ConvertDegreesToRadians(degrees);

	return sinf(radians);
}

float Atan2Degrees(float y, float x)
{
	float radians = atan2f(y, x);

	return ConvertRadiansToDegrees(radians);
}

float GetShortestAngularDispDegrees(float startDegrees, float endDegrees)
{
	float deltaDegrees = endDegrees - startDegrees;
	while (deltaDegrees > 180.0f) deltaDegrees -= 360.0f;
	while (deltaDegrees < -180.0f) deltaDegrees += 360.0f;
	return deltaDegrees;
}

float GetTurnedTowardDegrees(float currentDegrees, float goalDegrees, float maxDeltaDegrees)
{
	float angularDisp = GetShortestAngularDispDegrees(currentDegrees, goalDegrees);
	if (angularDisp >= maxDeltaDegrees)
	{
		return currentDegrees + maxDeltaDegrees;
	}
	else if (angularDisp < -maxDeltaDegrees)
	{
		return currentDegrees - maxDeltaDegrees;
	}
	return goalDegrees;
}

float GetAngleDegreesBetweenVectors2D(Vec2 const& a, Vec2 const& b)
{
    float dotProduct = DotProduct2D(a, b);

    float lengthA = a.GetLength();
    float lengthB = b.GetLength();

    float cosTheta = dotProduct / (lengthA * lengthB);

    cosTheta = GetClamped(cosTheta, -1.0f, 1.0f);

    float angleRadians = acos(cosTheta);

    float angleDegrees = ConvertRadiansToDegrees(angleRadians);

    return angleDegrees;
}

float GetDistance2D(Vec2 const& a, Vec2 const& b) 
{
	float x = b.x - a.x;
	float y = b.y - a.y;
	float d = sqrtf(x * x + y * y);
	return d;
}

float GetDistanceSquared2D(Vec2 const& positionA, Vec2 const& positionB)
{
	float x = positionB.x - positionA.x;
	float y = positionB.y - positionA.y;
	float d = (x * x) + (y * y);
	return d;
}

float GetDistance3D(Vec3 const& positionA, Vec3 const& positionB)
{
	float x = positionB.x - positionA.x;
	float y = positionB.y - positionA.y;
	float z = positionB.z - positionA.z;
	float d = sqrtf( (x * x) + (y * y) + (z * z) );
	return d;
}

float GetDistanceSquared3D(Vec3 const& positionA, Vec3 const& positionB)
{
	float x = positionB.x - positionA.x;
	float y = positionB.y - positionA.y;
	float z = positionB.z - positionA.z;
	float d = (x * x) + (y * y) + (z * z);
	return d;
}

float GetDistanceXY3D(Vec3 const& positionA, Vec3 const& positionB)
{
	float x = positionB.x - positionA.x;
	float y = positionB.y - positionA.y;
	float d = sqrtf( (x * x) + (y * y) );
	return d;
}

float GetDistanceXYSquared3D(Vec3 const& positionA, Vec3 const& positionB)
{
	float x = positionB.x - positionA.x;
	float y = positionB.y - positionA.y;
	float d = (x * x) + (y * y);
	return d;
}

int GetTaxicabDistance2D(IntVec2 const& pointA, IntVec2 const& pointB)
{
	return abs(pointA.x - pointB.x) + abs(pointA.y - pointB.y);
}

int GetTaxicabDistance3D(IntVec3 const& pointA, IntVec3 const& pointB)
{
	return abs(pointA.x - pointB.x) + abs(pointA.y - pointB.y) + abs(pointA.z - pointB.z);
}

float GetProjectedLength2D(Vec2 const& vectorToProject, Vec2 const& vectorToProjectOnto)
{
	// 投影长度公式：dot(V, N) / |N|，其中 N 是被投影的向量
	float dotProduct = DotProduct2D(vectorToProject, vectorToProjectOnto);
	float lengthOfOnto = vectorToProjectOnto.GetLength(); // 计算 |N|
	return dotProduct / lengthOfOnto;
}

Vec2 const GetProjectedOnto2D(Vec2 const& vectorToProject, Vec2 const& vectorToProjectOnto)
{
	// 投影公式：((V • N) / (|N|^2)) * N
	float dotProduct = DotProduct2D(vectorToProject, vectorToProjectOnto);
	float lengthSquared = vectorToProjectOnto.GetLengthSquared(); // 计算 |N|^2
	Vec2 projected = (dotProduct / lengthSquared) * vectorToProjectOnto;
	return projected;
}

bool IsPointInsideDisc2D(Vec2 const& point, Vec2 const& discCenter, float discRadius)
{
	float distanceSquared = (point - discCenter).GetLengthSquared();
	float radiusSquared = discRadius * discRadius;

	return distanceSquared <= radiusSquared;
}

bool IsPointInsideOrientedSector2D(Vec2 const& point, Vec2 const& sectorTip, float sectorForwardDegrees, float sectorApertureDegrees, float sectorRadius)
{
	Vec2 sectorForwardNormal = Vec2::MakeFromPolarDegrees(sectorForwardDegrees);
	return IsPointInsideDirectedSector2D(point, sectorTip, sectorForwardNormal, sectorApertureDegrees, sectorRadius);
}

bool IsPointInsideDirectedSector2D(Vec2 const& point, Vec2 const& sectorTip, Vec2 const& sectorForwardNormal, float sectorApertureDegrees, float sectorRadius)
{
	Vec2 toPoint = point - sectorTip;

	if (toPoint.GetLength() > sectorRadius)
		return false;

	float cosHalfAperture = cosf(ConvertDegreesToRadians(sectorApertureDegrees * 0.5f));
	float dotProduct = DotProduct2D(toPoint.GetNormalized(), sectorForwardNormal);

	return dotProduct >= cosHalfAperture;
}

bool IsPointInsideAABB2D(Vec2 const& point, AABB2 const& box)
{
	return (point.x >= box.m_mins.x && point.x <= box.m_maxs.x) &&
		(point.y >= box.m_mins.y && point.y <= box.m_maxs.y);
}

bool IsPointInsideOBB2D(Vec2 const& point, OBB2 const& box)
{
	// point->OBB(i,j)
	Vec2 toPoint = point - box.m_center;
	Vec2 jBasisNormal(-box.m_iBasisNormal.y, box.m_iBasisNormal.x); 

	float localX = DotProduct2D(toPoint, box.m_iBasisNormal);
	float localY = DotProduct2D(toPoint, jBasisNormal);

	return (localX >= -box.m_halfDimensions.x && localX <= box.m_halfDimensions.x) &&
		   (localY >= -box.m_halfDimensions.y && localY <= box.m_halfDimensions.y);
}

bool IsPointInsideCapsule(Vec2 const& point, Vec2 const& boneStart, Vec2 const& boneEnd, float radius)
{
	Vec2 nearestPoint = GetNearestPointOnLineSegment2D(point, boneStart, boneEnd);

	float distanceSquared = (point - nearestPoint).GetLengthSquared(); 

	return distanceSquared <= (radius * radius);
}

bool IsPointInsideTriangle2D(Vec2 const& point, Vec2 const& triCCW0, Vec2 const& triCCW1, Vec2 const& triCCW2)
{
	// 3 lines
	Vec2 edge0 = triCCW1 - triCCW0;
	Vec2 edge1 = triCCW2 - triCCW1;
	Vec2 edge2 = triCCW0 - triCCW2;

	// AP,BP,CP
	Vec2 toPoint0 = point - triCCW0;
	Vec2 toPoint1 = point - triCCW1;
	Vec2 toPoint2 = point - triCCW2;

	float dot0 = DotProduct2D(toPoint0, Vec2(-edge0.y, edge0.x)); // edge0 normal
	float dot1 = DotProduct2D(toPoint1, Vec2(-edge1.y, edge1.x)); // edge1 normal
	float dot2 = DotProduct2D(toPoint2, Vec2(-edge2.y, edge2.x)); // edge2 normal

	bool isSameSide0 = (dot0 >= 0);
	bool isSameSide1 = (dot1 >= 0);
	bool isSameSide2 = (dot2 >= 0);

	return (isSameSide0 == isSameSide1) && (isSameSide1 == isSameSide2);
}

bool IsPointInsideSphere3D(Vec3 const& point, Vec3 sphereCenter, float radius)
{
	return GetDistanceSquared3D(point, sphereCenter) <= radius * radius;
}

bool DoDiscsOverlap(Vec2 const& centerA, float radiusA,
                    Vec2 const& centerB, float radiusB)
{
	float dSquared = GetDistanceSquared2D(centerA, centerB);
	float radiusSum = radiusA + radiusB;
	float radiusSumSquared = radiusSum * radiusSum;

	return dSquared <= radiusSumSquared;
}

bool DoSpheresOverlap3D(Vec3 const& centerA, float radiusA,
	Vec3 const& centerB, float radiusB)
{
	float d = GetDistanceSquared3D(centerA, centerB);

	if (d < (radiusA + radiusB)* (radiusA + radiusB))
	{
		return true;
	}

	else
	{
		return false;
	}
}

bool DoAABBsOverlap3D(AABB3 const& first, AABB3 const& second)
{
	if (first.m_maxs.x < second.m_mins.x)
		return false;

	if (first.m_mins.x > second.m_maxs.x)
		return false;

	if (first.m_maxs.y < second.m_mins.y)
		return false;

	if (first.m_mins.y > second.m_maxs.y)
		return false;

	if (first.m_maxs.z < second.m_mins.z)
		return false;

	if (first.m_mins.z > second.m_maxs.z)
		return false;

	return true;
}

bool DoAABBAndPlaneOverlap3D(AABB3 const& box, Plane3 const& plane)
{
	Vec3 center = (box.m_mins + box.m_maxs) * 0.5f;
	// float circumstanceSquared = ((box.m_maxs - box.m_mins)/2.f).GetLengthSquared();
	// if ((center - GetNearestPointOnPlane3D(center, plane)).GetLengthSquared() >= circumstanceSquared)
	// {
	// 	return false;
	// }
	// else
	// {
	// 	
	// }
	Vec3 halfExtents = (box.m_maxs - box.m_mins) * 0.5f;

	float projectedRadius =
		fabs(DotProduct3D(plane.m_normal, Vec3(1.f, 0.f, 0.f))) * halfExtents.x +
		fabs(DotProduct3D(plane.m_normal, Vec3(0.f, 1.f, 0.f))) * halfExtents.y +
		fabs(DotProduct3D(plane.m_normal, Vec3(0.f, 0.f, 1.f))) * halfExtents.z;

	float centerToPlaneDist = fabs(DotProduct3D(center, plane.m_normal) - plane.m_distToPlaneAloneNormalFromOrigin);

	if (centerToPlaneDist <= projectedRadius)
	{
		return true;
	}
	return false;
}

bool DoZCylindersOverlap3D(Vec2 cylinder1CenterXY, float cylinder1Radius, FloatRange cylinder1MinMaxZ, Vec2 cylinder2CenterXY, float cylinder2Radius, FloatRange cylinder2MinMaxZ)
{
	if (cylinder1MinMaxZ.m_max < cylinder2MinMaxZ.m_min)
		return false;
	if (cylinder1MinMaxZ.m_min > cylinder2MinMaxZ.m_max)
		return false;

	float distSquared = GetDistanceSquared2D(cylinder1CenterXY, cylinder2CenterXY);
	if (distSquared > (cylinder1Radius + cylinder2Radius) * (cylinder1Radius + cylinder2Radius))
		return false;

	return true;
}

bool DoSphereAndAABBOverlap3D(Vec3 sphereCenter, float sphereRadius, AABB3 box)
{
	if (box.m_mins.z - sphereRadius > sphereCenter.z)
		return false;
	if (box.m_maxs.z + sphereRadius < sphereCenter.z)
		return false;

	Vec2 nearPos = GetNearestPointOnAABB2D(Vec2(sphereCenter.x, sphereCenter.y), AABB2(Vec2(box.m_mins.x, box.m_mins.y), Vec2(box.m_maxs.x, box.m_maxs.y)));
	float distSqr = GetDistanceSquared2D(Vec2(sphereCenter.x, sphereCenter.y), nearPos);
	if (distSqr > sphereRadius * sphereRadius)
		return false;

	return true;
}

bool DoSphereAndPlaneOverlap3D(Vec3 const& sphereCenter, float const& sphereRadius, Plane3 const& plane)
{
	Vec3 pos = GetNearestPointOnPlane3D(sphereCenter, plane);
	if (GetDistanceSquared3D(pos, sphereCenter) > sphereRadius * sphereRadius)
	{
		return false;
	}
	return true;
}

bool DoSphereAndOBBOverlap3D(Vec3 const& sphereCenter, float const& sphereRadius, OBB3 const& box)
{
	Vec3 newCenter = box.GetLocalPosForWorldPos(sphereCenter);
	if (DoSphereAndAABBOverlap3D(newCenter, sphereRadius, AABB3(-box.m_halfDimensions, box.m_halfDimensions)))
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool DoZCylinderAndAABBOverlap3D(Vec2 cylinderCenterXY, float cylinderRadius, FloatRange cylinderMinMaxZ, AABB3 box)
{
	if (cylinderMinMaxZ.m_min > box.m_maxs.z)
		return false;
	if (cylinderMinMaxZ.m_max < box.m_mins.z)
		return false;

	if (cylinderCenterXY.x > box.m_mins.x && cylinderCenterXY.x < box.m_maxs.x
		&& cylinderCenterXY.y > box.m_mins.y && cylinderCenterXY.y < box.m_maxs.y)
		return true;

	Vec2 nearPos = GetNearestPointOnAABB2D(cylinderCenterXY, AABB2(Vec2(box.m_mins.x, box.m_mins.y), Vec2(box.m_maxs.x, box.m_maxs.y)));
	float distSqr = GetDistanceSquared2D(cylinderCenterXY, nearPos);
	if (distSqr > cylinderRadius * cylinderRadius)
		return false;
	else
		return true;
}

bool DoZCylinderAndSphereOverlap3D(Vec2 cylinderCenterXY, float cylinderRadius, FloatRange cylinderMinMaxZ, Vec3 sphereCenter, float sphereRadius)
{
	if (cylinderMinMaxZ.m_min - sphereRadius > sphereCenter.z)
		return false;
	if (cylinderMinMaxZ.m_max + sphereRadius < sphereCenter.z)
		return false;

	float distSquared = GetDistanceSquared2D(cylinderCenterXY, Vec2(sphereCenter.x,sphereCenter.y));
	if (distSquared > (cylinderRadius + sphereRadius) * (cylinderRadius + sphereRadius))
		return false;

	return true;
}

bool DoOBBAndPlaneOverlap3D(OBB3 const& box, Plane3 const& plane)
{
	// float circumscribedRadiusSquared = box.GetCircumscribedRadiusSquared();
	// Vec3 point = GetNearestPointOnPlane3D(box.m_center, plane);
	// if (GetDistanceSquared3D(point, box.m_center) > circumscribedRadiusSquared)
	// {
	// 	
	// }
	// return true;
	float projectedRadius =
		fabs(DotProduct3D(plane.m_normal, box.m_iBasisNormal)) * box.m_halfDimensions.x +
		fabs(DotProduct3D(plane.m_normal, box.m_jBasisNormal)) * box.m_halfDimensions.y +
		fabs(DotProduct3D(plane.m_normal, box.m_kBasisNormal)) * box.m_halfDimensions.z;

	float distanceToPlane = fabs(DotProduct3D(box.m_center, plane.m_normal) - plane.m_distToPlaneAloneNormalFromOrigin);
	if (distanceToPlane <= projectedRadius)
	{
		return true;
	}
	return false;
}

Vec2 GetNearestPointOnDisc2D(Vec2 const& referencePosition, Vec2 const& discCenter, float discRadius)
{
	// 计算参考点到圆心的向量
	Vec2 displacement = referencePosition - discCenter;
	float distance = displacement.GetLength();

	// 如果参考点在圆内，返回参考点
	if (distance <= discRadius)
	{
		return referencePosition;
	}

	// 如果参考点在圆外，找到圆周上的最近点
	displacement.Normalize();
	return discCenter + displacement * discRadius;
}

Vec2 GetNearestPointOnAABB2D(Vec2 const& referencePosition, AABB2 const& alignedBox)
{
	float nearestX = referencePosition.x;
	float nearestY = referencePosition.y;

	if (referencePosition.x < alignedBox.m_mins.x) 
	{
		nearestX = alignedBox.m_mins.x;
	}
	else if (referencePosition.x > alignedBox.m_maxs.x) 
	{
		nearestX = alignedBox.m_maxs.x;
	}

	if (referencePosition.y < alignedBox.m_mins.y) 
	{
		nearestY = alignedBox.m_mins.y;
	}
	else if (referencePosition.y > alignedBox.m_maxs.y) 
	{
		nearestY = alignedBox.m_maxs.y;
	}

	return Vec2(nearestX, nearestY);
}

Vec2 GetNearestPointOnOBB2D(Vec2 const& referencePosition, OBB2 const& orientedBox)
{
	// Step 1: 将 referencePosition 转换到 OBB 的局部坐标系中
	Vec2 localPos = orientedBox.GetLocalPosForWorldPos(referencePosition);

	// Step 2: 限制局部坐标，使其在 OBB 的边界内
	Vec2 clampedLocalPos;
	clampedLocalPos.x = GetClamped(localPos.x, -orientedBox.m_halfDimensions.x, orientedBox.m_halfDimensions.x);
	clampedLocalPos.y = GetClamped(localPos.y, -orientedBox.m_halfDimensions.y, orientedBox.m_halfDimensions.y);

	// Step 3: 将最近的局部坐标转换回世界坐标系
	Vec2 nearestWorldPos = orientedBox.GetWorldPosForLocalPos(clampedLocalPos);

	return nearestWorldPos;
}

Vec2 GetNearestPointOnInfiniteLine2D(Vec2 const& referencePosition, Vec2 const& pointOnLine, Vec2 const& anotherPointOnline)
{
    Vec2 lineDir = anotherPointOnline - pointOnLine;
    Vec2 toReference = referencePosition - pointOnLine;

    float projectionFactor = DotProduct2D(toReference, lineDir) / DotProduct2D(lineDir, lineDir);

    Vec2 nearestPoint = pointOnLine + lineDir * projectionFactor;
    return nearestPoint;
}

Vec2 GetNearestPointOnLineSegment2D(Vec2 const& referencePosition, Vec2 const& start, Vec2 const& end)
{
	Vec2 lineDir = end - start;
	float lineLengthSquared = DotProduct2D(lineDir, lineDir);

	// if 0, start :)
	/*if (lineLengthSquared == 0.0f) 
	{
		return start;
	}*/

	Vec2 toReference = referencePosition - start;

	float projectionFactor = DotProduct2D(toReference, lineDir) / lineLengthSquared;

	// restrict the factor within [0,1]
	projectionFactor = GetClampedZeroToOne(projectionFactor);

	Vec2 nearestPoint = start + lineDir * projectionFactor;
	return nearestPoint;
}

Vec2 GetNearestPointOnCapsule2D(Vec2 const& referencePosition, Vec2 const& boneStart, Vec2 const& boneEnd, float radius)
{
	Vec2 nearestPointOnLineSegment = GetNearestPointOnLineSegment2D(referencePosition, boneStart, boneEnd);

	Vec2 toReference = referencePosition - nearestPointOnLineSegment;

	if (toReference.GetLengthSquared() <= radius * radius) 
	{
		return nearestPointOnLineSegment;
	}

	else
	{
		toReference.Normalize();
		Vec2 nearestPointOnCapsule = nearestPointOnLineSegment + toReference * radius;

		return nearestPointOnCapsule;
	}	
}

Vec2 GetNearestPointOnTriangle2D(Vec2 const& referencePosition, Vec2 const& triCCW0, Vec2 const& triCCW1, Vec2 const& triCCW2)
{
	if (IsPointInsideTriangle2D(referencePosition, triCCW0, triCCW1, triCCW2)) 
	{
		return referencePosition;
	}

	Vec2 nearestPointOnEdge0 = GetNearestPointOnLineSegment2D(referencePosition, triCCW0, triCCW1);
	Vec2 nearestPointOnEdge1 = GetNearestPointOnLineSegment2D(referencePosition, triCCW1, triCCW2);
	Vec2 nearestPointOnEdge2 = GetNearestPointOnLineSegment2D(referencePosition, triCCW2, triCCW0);

	float distanceSquared0 = (referencePosition - nearestPointOnEdge0).GetLengthSquared();
	float distanceSquared1 = (referencePosition - nearestPointOnEdge1).GetLengthSquared();
	float distanceSquared2 = (referencePosition - nearestPointOnEdge2).GetLengthSquared();

	if (distanceSquared0 < distanceSquared1 && distanceSquared0 < distanceSquared2) 
	{
		return nearestPointOnEdge0;
	}
	else if (distanceSquared1 < distanceSquared2) 
	{
		return nearestPointOnEdge1;
	}
	else 
	{
		return nearestPointOnEdge2;
	}
}

Vec3 GetNearestPointOnAABB3D(Vec3 const& referencePos, AABB3 box)
{
	if (referencePos.x > box.m_mins.x && referencePos.x < box.m_maxs.x
		&& referencePos.y>box.m_mins.y && referencePos.y < box.m_maxs.y
		&& referencePos.z>box.m_mins.z && referencePos.z < box.m_maxs.z)
	{
		return referencePos;
	}
	Vec2 nearPosOnXY = GetNearestPointOnAABB2D(Vec2(referencePos.x, referencePos.y),
		AABB2(Vec2(box.m_mins.x, box.m_mins.y), Vec2(box.m_maxs.x, box.m_maxs.y)));

	if (referencePos.z > box.m_mins.z && referencePos.z < box.m_maxs.z)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, referencePos.z);
	if (referencePos.z >= box.m_maxs.z)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, box.m_maxs.z);
	if (referencePos.z <= box.m_mins.z)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, box.m_mins.z);

	return referencePos;
}

Vec3 GetNearestPointOnCylinderZ(Vec3 const& referencePos, Vec2 const& centerXY, FloatRange const& minMaxZ, float radiusXY)
{
	if ((Vec2(referencePos.x, referencePos.y) - centerXY).GetLengthSquared() <= radiusXY * radiusXY)		
	{
		if(referencePos.z > minMaxZ.m_min && referencePos.z < minMaxZ.m_max)
			return referencePos;
		if (referencePos.z > minMaxZ.m_max)
			return Vec3(referencePos.x, referencePos.y, minMaxZ.m_max);
		if (referencePos.z < minMaxZ.m_min)
			return Vec3(referencePos.x, referencePos.y, minMaxZ.m_min);
	}
	Vec2 nearPosOnXY = GetNearestPointOnDisc2D(Vec2(referencePos.x, referencePos.y), centerXY, radiusXY);
	
	if (referencePos.z > minMaxZ.m_min && referencePos.z < minMaxZ.m_max)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, referencePos.z);
	if (referencePos.z >= minMaxZ.m_max)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, minMaxZ.m_max);
	if (referencePos.z <= minMaxZ.m_min)
		return Vec3(nearPosOnXY.x, nearPosOnXY.y, minMaxZ.m_min);

	return referencePos;
}

Vec3 GetNearestPointOnSphere(Vec3 const& referencePos, Vec3 const& sphereCenter, float const& sphereRadius)
{
	float distSqr = (referencePos - sphereCenter).GetLengthSquared();

	if (distSqr < sphereRadius * sphereRadius)
	{
		return referencePos;
	}

	Vec3 toRef = (referencePos - sphereCenter).GetNormalized();

	return sphereCenter + toRef * sphereRadius;
}

Vec3 GetNearestPointOnOBB3D(Vec3 const& referencePos, OBB3 const& box)
{
	Vec3 newPos = box.GetLocalPosForWorldPos(referencePos);
	Vec3 localPos =  GetNearestPointOnAABB3D(newPos, AABB3(-box.m_halfDimensions, box.m_halfDimensions));
	return box.GetWorldPosForLocalPos(localPos);
}

Vec3 GetNearestPointOnPlane3D(Vec3 const& referencePos, Plane3 const& plane)
{
	float distance = DotProduct3D(referencePos, plane.m_normal) - plane.m_distToPlaneAloneNormalFromOrigin;
	return referencePos - distance * plane.m_normal;
}

float DistanceSquaredPointToSegment(const Vec3& p, const Vec3& a, const Vec3& b)
{
	Vec3 ab = b - a;
	Vec3 ap = p - a;
	float abLen2 = DotProduct3D(ab, ab);
	if (abLen2 <= 0.f)
	{
		return GetDistanceSquared3D(p, a);
	}
	float t = DotProduct3D(ap, ab) / abLen2;
	t = GetClampedZeroToOne(t);
	Vec3 nearest = a + ab * t;
	return GetDistanceSquared3D(p, nearest);
}

float DistanceToTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
	Vec3 ba = b - a, pa = p - a;
	Vec3 cb = c - b, pb = p - b;
	Vec3 ac = a - c, pc = p - c;
	Vec3 nor = CrossProduct3D(ba, ac);

	float sign = SignF(DotProduct3D(CrossProduct3D(ba, nor), pa)) +
		SignF(DotProduct3D(CrossProduct3D(cb, nor), pb)) +
		SignF(DotProduct3D(CrossProduct3D(ac, nor), pc));

	if (sign < 2.0f)
	{
		float distSq = DotProduct3D(nor, pa) * DotProduct3D(nor, pa) / DotProduct3D(nor, nor);
		return sqrtf(distSq);
	}

	float minDistSq = FLT_MAX;
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, a, b));
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, b, c));
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, c, a));

	return sqrtf(minDistSq);
}

float DistanceSquaredToTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
	Vec3 ba = b - a, pa = p - a;
	Vec3 cb = c - b, pb = p - b;
	Vec3 ac = a - c, pc = p - c;
	Vec3 nor = CrossProduct3D(ba, ac);

	float sign = SignF(DotProduct3D(CrossProduct3D(ba, nor), pa)) +
		SignF(DotProduct3D(CrossProduct3D(cb, nor), pb)) +
		SignF(DotProduct3D(CrossProduct3D(ac, nor), pc));

	if (sign < 2.0f)
	{
		return DotProduct3D(nor, pa) * DotProduct3D(nor, pa) / DotProduct3D(nor, nor);
	}

	float minDistSq = FLT_MAX;
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, a, b));
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, b, c));
	minDistSq = MinF(minDistSq, DistanceSquaredPointToSegment(p, c, a));

	return minDistSq;
}

bool PushDiscOutOfFixedPoint2D(Vec2& mobileDiscCenter, float discRadius, Vec2 const& fixedPoint)
{
	Vec2 displacement = mobileDiscCenter - fixedPoint;
	float distance = displacement.GetLength();

	if (distance < discRadius)
	{
		float overlap = discRadius - distance;
		displacement.Normalize();
		mobileDiscCenter += displacement * overlap;

		return true;
	}

	return false;
}

bool PushDiscOutOfFixedDisc2D(Vec2& mobileDiscCenter, float mobileDiscRadius, Vec2 const& fixedDiscCenter, float fixedDiscRadius)
{
	Vec2 displacement = mobileDiscCenter - fixedDiscCenter;
	float distance = displacement.GetLength();
	float combinedRadius = mobileDiscRadius + fixedDiscRadius;

	if (distance < combinedRadius)
	{
		float overlap = combinedRadius - distance;
		displacement.Normalize();
		mobileDiscCenter += displacement * overlap;

		return true;
	}

	return false;
}

bool PushDiscsOutOfEachOther2D(Vec2& aCenter, float aRadius, Vec2& bCenter, float bRadius)
{
	Vec2 displacement = aCenter - bCenter;
	float distance = displacement.GetLength();
	float combinedRadius = aRadius + bRadius;

	if (distance < combinedRadius)
	{
		float overlap = combinedRadius - distance;
		displacement.Normalize();

		Vec2 moveVector = displacement * (overlap / 2);
		aCenter += moveVector;
		bCenter -= moveVector;

		return true;
	}

	return false;
}

bool PushDiscOutOfFixedAABB2D(Vec2& mobileDiscCenter, float discRadius, AABB2 const& fixedBox)
{
	bool hasMoved = false;

	// fine the nearest point on AABB to center
	float nearestX = fmax(fixedBox.m_mins.x, fmin(mobileDiscCenter.x, fixedBox.m_maxs.x));
	float nearestY = fmax(fixedBox.m_mins.y, fmin(mobileDiscCenter.y, fixedBox.m_maxs.y));

	// length
	float deltaX = mobileDiscCenter.x - nearestX;
	float deltaY = mobileDiscCenter.y - nearestY;
	float distanceSquared = deltaX * deltaX + deltaY * deltaY;
	float radiusSquared = discRadius * discRadius;

	// check if overlapping
	if (distanceSquared < radiusSquared)
	{
		float distance = sqrt(distanceSquared);
		float overlap = discRadius - distance;

		// compute the direction of pushing
		if (distance != 0.0f) 
		{
			deltaX /= distance;
			deltaY /= distance;
		}
		else
		{
			// if center is in AABB, the direction is random
			deltaX = 1.0f;
			deltaY = 0.0f;
		}
	
		mobileDiscCenter.x += deltaX * overlap;	// Push
		mobileDiscCenter.y += deltaY * overlap;

		hasMoved = true;
	}

	return hasMoved;
}

bool PushDiscOutOfFixedOBB2D(OBB2 const& obb, Vec2& mobileDiscCenter, float discRadius)
{
	Vec2 localCirclePos = obb.GetLocalPosForWorldPos(mobileDiscCenter);

	Vec2 nearestLocalPos = localCirclePos;
	nearestLocalPos.x = GetClamped(localCirclePos.x, -obb.m_halfDimensions.x, obb.m_halfDimensions.x);
	nearestLocalPos.y = GetClamped(localCirclePos.y, -obb.m_halfDimensions.y, obb.m_halfDimensions.y);

	Vec2 localDisp = localCirclePos - nearestLocalPos;
	float distSq = localDisp.GetLengthSquared();

	if (distSq >= discRadius * discRadius)
	{
		return false;
	}

	float dist = sqrtf(distSq);
	Vec2 pushDirLocal = (dist > 0.f) ? localDisp / dist : Vec2(1.f, 0.f);
	float penetration = discRadius - dist;
	Vec2 correctionLocal = pushDirLocal * penetration;

	Vec2 correctionWorld = obb.GetWorldPosForLocalPos(localCirclePos + correctionLocal) - mobileDiscCenter;
	mobileDiscCenter += correctionWorld;

	return true;
}

void TransformPosition2D(Vec2& posToTransform, float uniformscale,float RotationDegrees, Vec2 const& translation)
{
	float degrees = Atan2Degrees(posToTransform.y, posToTransform.x);
	float R = sqrtf(posToTransform.x * posToTransform.x + posToTransform.y * posToTransform.y);
	
	R *= uniformscale;
	degrees += RotationDegrees;

	posToTransform.x = R * CosDegrees(degrees) + translation.x;
	posToTransform.y = R * SinDegrees(degrees) + translation.y;
}

void TransformPosition2D(Vec2& posToTransform, Vec2 const& iBasis, Vec2 const& jBasis, Vec2 const& translation)
{
	float x = posToTransform.x;
	float y = posToTransform.y;

	float transformedX = (x * iBasis.x) + (y * jBasis.x) + translation.x;
	float transformedY = (x * iBasis.y) + (y * jBasis.y) + translation.y;

	posToTransform.x = transformedX;
	posToTransform.y = transformedY;
}

void TransformPositionXY3D(Vec3& posToTransform, float scaleXY,
	float zRotationDegrees, Vec2 const& translationXY)
{
	float degrees = Atan2Degrees(posToTransform.y, posToTransform.x);
	float R = sqrtf(posToTransform.x * posToTransform.x + posToTransform.y * posToTransform.y);

	R *= scaleXY;
	degrees += zRotationDegrees;

	posToTransform.x = R * CosDegrees(degrees) + translationXY.x;
	posToTransform.y = R * SinDegrees(degrees) + translationXY.y;
}

void TransformPositionXY3D(Vec3& positionToTransform, Vec2 const& iBasis, Vec2 const& jBasis, Vec2 const& translationXY)
{
	float x = positionToTransform.x;
	float y = positionToTransform.y;

	float transformedX = (x * iBasis.x) + (y * jBasis.x) + translationXY.x;
	float transformedY = (x * iBasis.y) + (y * jBasis.y) + translationXY.y;

	positionToTransform.x = transformedX;
	positionToTransform.y = transformedY;
}

void TransformPositionXYZ3D(Vec3& positionToTransform, Vec3 const& iBasis, Vec3 const& jBasis, Vec3 const& kBasis,
	Vec3 const& translation)
{
	float x = positionToTransform.x;
	float y = positionToTransform.y;
	float z = positionToTransform.z;

	float transformedX = (x * iBasis.x) + (y * jBasis.x) + (z * kBasis.x) + translation.x;
	float transformedY = (x * iBasis.y) + (y * jBasis.y) + (z * kBasis.y) + translation.y;
	float transformedZ = (x * iBasis.z) + (y * jBasis.z) + (z * kBasis.z) + translation.z;

	positionToTransform.x = transformedX;
	positionToTransform.y = transformedY;
	positionToTransform.z = transformedZ;
}

float GetClamped(float value, float minValue, float maxValue)
{
	if (value < minValue) return minValue;
	if (value > maxValue) return maxValue;
	return value;
}

int GetClampedInt(int value, int minValue, int maxValue)
{
	if (value < minValue) return minValue;
	if (value > maxValue) return maxValue;
	return value;
}

float GetClampedZeroToOne(float value)
{
	return GetClamped(value, 0.0f, 1.0f);
}

float Interpolate(float start, float end, float fractionTowardEnd)
{
	return start + fractionTowardEnd * (end - start);
}

Vec2 InterpolateVec2(Vec2 start, Vec2 end, float fractionTowardEnd)
{
	float newX = Interpolate(start.x, end.x, fractionTowardEnd);
	float newY = Interpolate(start.y, end.y, fractionTowardEnd);
	return Vec2(newX, newY);
}

Vec3 InterpolateVec3(Vec3 start, Vec3 end, float fractionTowardEnd)
{
	float newX = Interpolate(start.x, end.x, fractionTowardEnd);
	float newY = Interpolate(start.y, end.y, fractionTowardEnd);
	float newZ = Interpolate(start.z, end.z, fractionTowardEnd);
	return Vec3(newX,newY,newZ);
}

float GetFractionWithinRange(float value, float rangeStart, float rangeEnd)
{
	//value = GetClamped(value, rangeStart, rangeEnd);
	return (value - rangeStart) / (rangeEnd - rangeStart);
}

float RangeMap(float inValue, float inStart, float inEnd, float outStart, float outEnd)
{
	float fraction = GetFractionWithinRange(inValue, inStart, inEnd);
	return Interpolate(outStart, outEnd, fraction);
}

float RangeMapClamped(float inValue, float inStart, float inEnd, float outStart, float outEnd)
{
	float clampedInValue = GetClamped(inValue, inStart, inEnd);
	return RangeMap(clampedInValue, inStart, inEnd, outStart, outEnd);
}

int RoundDownToInt(float value)
{
	return static_cast<int>(floor(value));
}

std::string RoundToOneDecimalString(float value)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(1) << value;
	return out.str(); 
}

std::string RoundToTwoDecimalsString(float value)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << value;
	return out.str();
}

float DotProduct2D(Vec2 const& a, Vec2 const& b)
{
	return (a.x * b.x) + (a.y * b.y);
}

float DotProduct3D(Vec3 const& a, Vec3 const& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

float DotProduct4D(Vec4 const& a, Vec4 const& b)
{
	return (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
}

float CrossProduct2D(Vec2 const& a, Vec2 const& b)
{
	return a.x * b.y - a.y * b.x;
}

Vec3 CrossProduct3D(Vec3 const& a, Vec3 const& b)
{
	return Vec3(
		a.y * b.z - a.z * b.y, 
		a.z * b.x - a.x * b.z, 
		a.x * b.y - a.y * b.x  
	);
}

float NormalizeByte(unsigned char byteValue)
{
	return static_cast<float>(byteValue) / 255.0f;
}

unsigned char DenormalizeByte(float normalizedValue)
{
	//[1-1/256,1] <==> 255.0
	//[0, 1/256) <==> 0
	//[1/256, 2/256) <==> 1
	if (normalizedValue <= 0.0f) return 0;
	if (normalizedValue >= 1.0f) return 255;

	return static_cast<unsigned char>(normalizedValue * 256.0f );
}

RaycastResult2D RaycastVsDisc2D(Vec2 startPos, Vec2 fwdNormal, float maxDist, Vec2 discCenter, float discRadius)
{
	RaycastResult2D result;
	result.m_rayStartPos = startPos;
	result.m_rayFwdNormal = fwdNormal;
	result.m_rayMaxLength = maxDist;

	// startPos->center distance
	Vec2 m = discCenter - startPos;

	// m's projection on ray-cast's normal
	float tClosest = DotProduct2D(m, fwdNormal);

	// projection point -> center distance squared
	float distSquared = m.GetLengthSquared() - (tClosest * tClosest);

	// if distSquared > radiusSquared, no crossing
	float radiusSquared = discRadius * discRadius;
	if (distSquared > radiusSquared)
		return result;

	// compute crossing point's distance
	float tOffset = sqrtf(radiusSquared - distSquared);
	float t1 = tClosest - tOffset;
	float t2 = tClosest + tOffset;

	// check if crossing point's on the ray-cast
	//float impactDist = (t1 >= 0 && t1 <= maxDist) ? t1 : (t2 >= 0 && t2 <= maxDist) ? t2 : maxDist + 1;
	float impactDist = maxDist + 100.f;

	if (t1 >= 0 && t1 <= maxDist) 
	{
		impactDist = t1;
	}

	if (t2 >= 0 && t2 <= maxDist) 
	{
		if (t2 < impactDist) 
		{
			impactDist = t2;
		}
	}

	if (impactDist <= maxDist)
	{
		result.m_didImpact = true;
		result.m_impactDist = impactDist;
		result.m_impactPos = startPos + impactDist * fwdNormal;
		result.m_impactNormal = (result.m_impactPos - discCenter).GetNormalized();
	}

	return result;
}

RaycastResult2D RaycastVsLineSegment2D(RaycastResult2D ray, Vec2 const& lineStart, Vec2 const& lineEnd)
{
	Vec2 jNormal = ray.m_rayFwdNormal.GetRotated90Degrees();
	Vec2 RStart = lineStart - ray.m_rayStartPos;
	Vec2 REnd = lineEnd - ray.m_rayStartPos;

	float RStartj = DotProduct2D(RStart, jNormal);
	float REndj = DotProduct2D(REnd, jNormal);
	if ((RStartj * REndj) >= 0.f)
	{
		return ray;
	}

	float RStarti = DotProduct2D(RStart, ray.m_rayFwdNormal);
	float REndi = DotProduct2D(REnd, ray.m_rayFwdNormal);
	if (RStarti >= ray.m_rayMaxLength && REndi >= ray.m_rayMaxLength)
	{
		return ray;
	}
	if (RStarti <=0.f && REndi <= 0.f)
	{
		return ray;
	}

	float t = RStartj / (RStartj - REndj);
	float impactDist = RStarti + t*(REndi - RStarti);
	if (impactDist <= 0.f || impactDist >= ray.m_rayMaxLength)
	{
		return ray;
	}

	Vec2 impactPos = lineStart + t * (lineEnd - lineStart);
	Vec2 lineFwd = (lineEnd - lineStart).GetNormalized();
	Vec2 impactNormal = lineFwd.GetRotated90Degrees();
	if (DotProduct2D(impactNormal, ray.m_rayFwdNormal) > 0.f)
	{
		impactNormal = -impactNormal;
	}

	ray.m_didImpact = true;
	ray.m_impactDist = impactDist;
	ray.m_impactPos = impactPos;
	ray.m_impactNormal = impactNormal;
	return ray;
}

RaycastResult2D RaycastVsAABB2D(RaycastResult2D ray, const AABB2 aabb)
{
	if (IsPointInsideAABB2D(ray.m_rayStartPos, aabb)) 
	{
		ray.m_didImpact = true;
		ray.m_impactPos = ray.m_rayStartPos;
		ray.m_impactNormal = -ray.m_rayFwdNormal;
		ray.m_impactDist = 0.f;
		return ray;
	}
	Vec2 bottomLeft = aabb.m_mins;
	Vec2 topRight = aabb.m_maxs;

	Vec2 fwd = ray.m_rayFwdNormal;
	Vec2 start = ray.m_rayStartPos;
	Vec2 end = start + ray.m_rayMaxLength * fwd;
	if (fwd.x == 0.f)
	{
		if (start.x<=bottomLeft.x || start.x>=topRight.x)
		{
			return ray;
		}
		else
		{
			if ((end.y <= bottomLeft.y && start.y <= bottomLeft.y) || (end.y >= bottomLeft.y && start.y >= bottomLeft.y))
			{
				return ray;
			}
			else
			{
				Vec2 impactPos = GetNearestPointOnAABB2D(start, aabb);
				ray.m_didImpact = true;
				ray.m_impactNormal = -fwd;
				ray.m_impactPos = impactPos;
				ray.m_impactDist = abs(impactPos.y - start.y);
				return ray;
			}
		}
	}
	if (fwd.y == 0.f)
	{
		if (start.y <= bottomLeft.y || start.y >= topRight.y)
		{
			return ray;
		}
		else
		{
			if ((end.x <= bottomLeft.x && start.x <= bottomLeft.x) || (end.x >= bottomLeft.x && start.x >= bottomLeft.x))
			{
				return ray;
			}
			else
			{
				Vec2 impactPos = GetNearestPointOnAABB2D(start, aabb);
				ray.m_didImpact = true;
				ray.m_impactNormal = -fwd;
				ray.m_impactPos = impactPos;
				ray.m_impactDist = abs(impactPos.x - start.x);
				return ray;
			}
		}
	}
	if ((start.x <= bottomLeft.x && end.x <= bottomLeft.x) || (start.x >= topRight.x && end.x >= topRight.x))
	{
		return ray;
	}

	float tminX = (bottomLeft.x - start.x) / (fwd.x * ray.m_rayMaxLength);
	float tmaxX = (topRight.x - start.x) / (fwd.x * ray.m_rayMaxLength);
	if (tminX > tmaxX)
	{
		std::swap(tminX, tmaxX);
	}
	float tminY = (bottomLeft.y - start.y) / (fwd.y * ray.m_rayMaxLength);
	float tmaxY = (topRight.y - start.y) / (fwd.y * ray.m_rayMaxLength);
	if (tminY > tmaxY)
	{
		std::swap(tminY, tmaxY);
	}
	if (tminX > tmaxY || tminY > tmaxX)
	{
		return ray; //miss
	}
	//float tLow = std::max(tminX, tminY);
	//float tHigh = std::min(tmaxX, tmaxY);
	float tLow; 
	if (tminX < tminY)
	{
		tLow = tminY;
	}
	else
	{
		tLow = tminX;
	}
	float tHigh;
	if (tmaxX < tmaxY)
	{
		tHigh = tmaxX;
	}
	else
	{
		tHigh = tmaxY;
	}
		

	if (tLow < 0.f || tHigh>1.f)
	{
		return ray;
	}

	ray.m_didImpact = true;
	ray.m_impactDist = tLow * ray.m_rayMaxLength;
	ray.m_impactPos = start + fwd * ray.m_impactDist;

	if (tLow == tminX)
	{
		if (fwd.x > 0)
		{
			ray.m_impactNormal = Vec2(-1, 0);
		}
		else
		{
			ray.m_impactNormal = Vec2(1, 0);
		}
	}
	else
	{
		if (fwd.y > 0)
		{
			ray.m_impactNormal = Vec2(0, -1);
		}
		else
		{
			ray.m_impactNormal = Vec2(0, 1);
		}
	}
	return ray;
}

RaycastResult3D RaycastVsAABB3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, AABB3 box)
{
	RaycastResult3D ray3D;
	ray3D.m_didImpact = false;
	ray3D.m_rayStartPos = rayStart;
	ray3D.m_rayFwdNormal = rayForwardNormal;
	ray3D.m_rayMaxLength = rayLength;

	if (DoSphereAndAABBOverlap3D(rayStart, 0.f, box))
	{
		ray3D.m_didImpact = true;
		ray3D.m_impactNormal = -rayForwardNormal;
		ray3D.m_impactPos = rayStart;
		ray3D.m_impactDist = 0.f;
		return ray3D;
	}

	float tMinX, tMaxX, tMinY, tMaxY, tMinZ, tMaxZ;
	if (rayForwardNormal.x != 0.0f)
	{
		float invDx = 1.0f / rayForwardNormal.x;
		if (invDx >= 0.0f) 
		{
			tMinX = (box.m_mins.x - rayStart.x) * invDx;
			tMaxX = (box.m_maxs.x - rayStart.x) * invDx;
		}
		else 
		{
			tMinX = (box.m_maxs.x - rayStart.x) * invDx;
			tMaxX = (box.m_mins.x - rayStart.x) * invDx;
		}
	}
	else
	{
		if (rayStart.x < box.m_mins.x || rayStart.x > box.m_maxs.x)
			return ray3D;

		tMinX = -FLT_MAX;
		tMaxX = FLT_MAX;
	}

	if (rayForwardNormal.y != 0.0f)
	{
		float invDy = 1.0f / rayForwardNormal.y;
		if (invDy >= 0.0f) 
		{
			tMinY = (box.m_mins.y - rayStart.y) * invDy;
			tMaxY = (box.m_maxs.y - rayStart.y) * invDy;
		}
		else 
		{
			tMinY = (box.m_maxs.y - rayStart.y) * invDy;
			tMaxY = (box.m_mins.y - rayStart.y) * invDy;
		}
	}
	else
	{
		if (rayStart.y < box.m_mins.y || rayStart.y > box.m_maxs.y)
			return ray3D;

		tMinY = -FLT_MAX;
		tMaxY = FLT_MAX;
	}

	if (rayForwardNormal.z != 0.0f)
	{
		float invDz = 1.0f / rayForwardNormal.z;
		if (invDz >= 0.0f) 
		{
			tMinZ = (box.m_mins.z - rayStart.z) * invDz;
			tMaxZ = (box.m_maxs.z - rayStart.z) * invDz;
		}
		else 
		{
			tMinZ = (box.m_maxs.z - rayStart.z) * invDz;
			tMaxZ = (box.m_mins.z - rayStart.z) * invDz;
		}
	}
	else 
	{
		if (rayStart.z < box.m_mins.z || rayStart.z > box.m_maxs.z)
			return ray3D;

		tMinZ = -FLT_MAX;
		tMaxZ = FLT_MAX;
	}

	float tEntry = fmaxf(fmaxf(tMinX, tMinY), tMinZ);
	float tExit = fminf(fminf(tMaxX, tMaxY), tMaxZ);

	if (tEntry > tExit || tExit < 0.f || tEntry > rayLength)
		return ray3D;

	ray3D.m_didImpact = true;
	ray3D.m_impactDist = tEntry;
	ray3D.m_impactPos = rayStart + rayForwardNormal * tEntry;

	if (tEntry == tMinX)
	{
		if (rayForwardNormal.x > 0)
			ray3D.m_impactNormal = Vec3(-1, 0, 0); 
		else
			ray3D.m_impactNormal = Vec3(1, 0, 0);  
	}
	else if (tEntry == tMinY)
	{
		if (rayForwardNormal.y > 0)
			ray3D.m_impactNormal = Vec3(0, -1, 0); 
		else
			ray3D.m_impactNormal = Vec3(0, 1, 0);  
	}
	else if (tEntry == tMinZ)
	{
		if (rayForwardNormal.z > 0)
			ray3D.m_impactNormal = Vec3(0, 0, -1); 
		else
			ray3D.m_impactNormal = Vec3(0, 0, 1);  
	}
	return ray3D;
}

RaycastResult3D RaycastVsSphere3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, Vec3 sphereCenter, float sphereRadius)
{
	/*RaycastResult3D ray3D;
	ray3D.m_didImpact = false;
	ray3D.m_rayStartPos = rayStart;
	ray3D.m_rayFwdNormal = rayForwardNormal;
	ray3D.m_rayMaxLength = rayLength;

	Vec3 rayToCenter = sphereCenter - rayStart;

	if (rayToCenter.GetLengthSquared() <= sphereRadius * sphereRadius)
	{
		ray3D.m_didImpact = true;
		ray3D.m_impactDist = 0.f;
		ray3D.m_impactNormal = (rayStart - sphereCenter).GetNormalized();
		ray3D.m_impactPos = rayStart;
		return ray3D;
	}

	float rToCCosProjection = DotProduct3D(rayToCenter, rayForwardNormal);

	if (rToCCosProjection <= 0.f)
		return ray3D;

	float distSqr = rayToCenter.GetLengthSquared() - rToCCosProjection * rToCCosProjection;

	if (distSqr < sphereRadius * sphereRadius)
	{
		float perDist = sqrt(sphereRadius * sphereRadius - distSqr);
		if (rToCCosProjection - perDist >= rayLength)
		{
			return ray3D;
		}
		else
		{
			ray3D.m_didImpact = true;
			ray3D.m_impactDist = rToCCosProjection - perDist;
			ray3D.m_impactPos = rayStart + rayForwardNormal * ray3D.m_impactDist;
			ray3D.m_impactNormal = (ray3D.m_impactPos - sphereCenter).GetNormalized();
		}
	}

	return ray3D;*/
	RaycastResult3D result;
	result.m_didImpact = false;
	result.m_rayStartPos = rayStart;
	result.m_rayFwdNormal = rayForwardNormal;
	result.m_rayMaxLength = rayLength;

	Vec3 toCenter = sphereCenter - rayStart;
	float radiusSqr = sphereRadius * sphereRadius;
	float centerDistSqr = toCenter.GetLengthSquared();

	// Case 1: rayStart inside sphere
	if (centerDistSqr <= radiusSqr)
	{
		result.m_didImpact = true;
		result.m_impactDist = 0.f;
		result.m_impactPos = rayStart;

		Vec3 normal = rayStart - sphereCenter;
		if (normal.GetLengthSquared() > 0.f)
			result.m_impactNormal = normal.GetNormalized();
		else
			result.m_impactNormal = -rayForwardNormal; // fallback if exactly center

		return result;
	}

	// Project toCenter onto ray direction
	float tCenter = DotProduct3D(toCenter, rayForwardNormal);

	// Sphere is behind the ray
	if (tCenter < 0.f)
		return result;

	float distSqrFromRayToCenter = centerDistSqr - tCenter * tCenter;
	if (distSqrFromRayToCenter > radiusSqr)
		return result;

	float offset = sqrtf(radiusSqr - distSqrFromRayToCenter);
	float tHit = tCenter - offset;

	if (tHit < 0.f || tHit > rayLength)
		return result;

	result.m_didImpact = true;
	result.m_impactDist = tHit;
	result.m_impactPos = rayStart + rayForwardNormal * tHit;
	result.m_impactNormal = (result.m_impactPos - sphereCenter).GetNormalized();

	return result;
}

RaycastResult3D RaycastVsCylinderZ3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, Vec2 const& centerXY, FloatRange const& minMaxZ, float radiusXY)
{
	RaycastResult3D ray3D;
	ray3D.m_didImpact = false;
	ray3D.m_rayStartPos = rayStart;
	ray3D.m_rayFwdNormal = rayForwardNormal;
	ray3D.m_rayMaxLength = rayLength;

	if (DoZCylinderAndSphereOverlap3D(centerXY, radiusXY, minMaxZ, rayStart, 0.f))
	{
		ray3D.m_didImpact = true;
		ray3D.m_impactNormal = -rayForwardNormal;
		ray3D.m_impactPos = rayStart;
		ray3D.m_impactDist = 0.f;
		return ray3D;
	}

	//float speed = rayForwardNormal.GetLength();
	Vec2 dirXY(rayForwardNormal.x, rayForwardNormal.y); 
	float scaleXY = dirXY.GetLength();
	//float scaleZ = fabsf(rayForwardNormal.z);

	float tZIn = (minMaxZ.m_min - rayStart.z) / rayForwardNormal.z;
	float tZOut = (minMaxZ.m_max - rayStart.z) / rayForwardNormal.z;
	if (tZIn > tZOut) std::swap(tZIn, tZOut);

	float tDiscIn3D = FLT_MAX;
	bool didXYImpact = false;

	if (scaleXY > 0.f) {
		RaycastResult2D result2D = RaycastVsDisc2D(Vec2(rayStart.x, rayStart.y), dirXY.GetNormalized(), rayLength * scaleXY, centerXY, radiusXY);
		if (result2D.m_didImpact) 
		{
			tDiscIn3D = result2D.m_impactDist / scaleXY;
			didXYImpact = true;
		}
	}

	float tEntry = fmaxf(tZIn, tDiscIn3D);
	float tExit = fminf(tZOut, rayLength);

	if (tEntry > tExit || tExit < 0 || tEntry > rayLength) 
	{
		return ray3D;
	}

	ray3D.m_didImpact = true;
	ray3D.m_impactPos = rayStart + tEntry * rayForwardNormal;
	ray3D.m_impactDist = tEntry; 

	float impactXYDist = (Vec2(ray3D.m_impactPos.x, ray3D.m_impactPos.y) - centerXY).GetLength();
	if (impactXYDist > radiusXY + 0.001) 
	{
		ray3D.m_didImpact = false;
		return ray3D;
	}

	if (fabsf(ray3D.m_impactPos.z - minMaxZ.m_max) < 0.0001f) 
	{
		ray3D.m_impactNormal = Vec3(0.f, 0.f, 1.f);
	}
	else if (fabsf(ray3D.m_impactPos.z - minMaxZ.m_min) < 0.0001f) 
	{
		ray3D.m_impactNormal = Vec3(0.f, 0.f, -1.f);
	}
	else 
	{
		Vec2 normalXY = Vec2(ray3D.m_impactPos.x - centerXY.x,
			ray3D.m_impactPos.y - centerXY.y);
		float normalLength = normalXY.GetLength();

		if (normalLength > 0.000001f) 
		{
			normalXY /= normalLength;
		}
		else 
		{
			normalXY = Vec2(1.f, 0.f); 
		}

		ray3D.m_impactNormal = Vec3(normalXY.x, normalXY.y, 0.f);
	}

	return ray3D;

	////side detect
	//Vec2 fwdXY = Vec2(rayForwardNormal.x, rayForwardNormal.y).GetNormalized();
	//Vec2 toCenterXY = centerXY - Vec2(rayStart.x, rayStart.y);
	//Vec2 toCenterXYNormal = toCenterXY.GetNormalized();

	//Vec2 projectionXY = DotProduct3D(rayForwardNormal * rayLength, Vec3(fwdXY.x, fwdXY.y, 0.f)) * fwdXY;
	//float projectionToCenterXY = DotProduct2D(projectionXY, toCenterXYNormal);
	////Vec2 projectionToCenterXY = DotProduct3D(rayStart + rayLength* rayForwardNormal, Vec3(toCenterXYNormal.x,toCenterXYNormal.y,0.f)) * toCenterXYNormal;
	////Vec2 projectionToCenterZ = DotProduct3D(rayLength * rayForwardNormal, Vec3(0.f, 0.f, 1.f)) * Vec2(0.f, 1.f);
	//Vec2 fakeRayEnd = Vec2(projectionToCenterXY, rayLength * rayForwardNormal.z);// +Vec2(projectionToCenterXY.GetLength(), 0.f);
	////float fake2DRayLength = (Vec2(0.f, rayLength * rayForwardNormal.z) + projectionToCenterXY).GetLength();
	//float fake2DRayLength = fakeRayEnd.GetLength();

	//RaycastResult2D result2D;
	//result2D.m_rayStartPos = Vec2(0.f, rayStart.z);
	//result2D.m_rayFwdNormal = fakeRayEnd.GetNormalized();
	//result2D.m_rayMaxLength = fake2DRayLength;

	//AABB2 fakeAABB2D;
	//Vec2 fakeBottomCenter = Vec2(toCenterXY.GetLength(), minMaxZ.m_min);
	//Vec2 fakeMins = fakeBottomCenter - Vec2(radiusXY, 0.f);
	//Vec2 fakeMaxs = fakeBottomCenter + Vec2(radiusXY, 0.f) + Vec2(0.f, minMaxZ.m_max - minMaxZ.m_min);
	//fakeAABB2D = AABB2(fakeMins, fakeMaxs);
	//result2D = RaycastVsAABB2D(result2D, fakeAABB2D);

	//if (result2D.m_didImpact == false)
	//	return ray3D;
	//else
	//{	//chuizhixiangxiakan
	//	RaycastResult2D result2DForDisc;
	//	//result2DForDisc.m_rayFwdNormal = fwdXY;
	//	//result2DForDisc.m_rayMaxLength = DotProduct3D(rayForwardNormal * rayLength, Vec3(fwdXY.x, fwdXY.y, 0.f));
	//	//result2DForDisc.m_rayStartPos = Vec2(rayStart.x, rayStart.y);
	//	result2DForDisc = RaycastVsDisc2D(Vec2(rayStart.x, rayStart.y), toCenterXYNormal, DotProduct3D(rayForwardNormal * rayLength, Vec3(toCenterXYNormal.x, toCenterXYNormal.y, 0.f)),
	//		centerXY, radiusXY);
	//	if (result2DForDisc.m_didImpact == false)
	//		return ray3D;
	//	else
	//	{
	//		ray3D.m_didImpact = true;
	//		ray3D.m_impactPos = Vec3(result2DForDisc.m_impactPos.x, result2DForDisc.m_impactPos.y, result2D.m_impactPos.y);
	//		ray3D.m_impactNormal = Vec3(Vec3(result2DForDisc.m_impactPos.x, result2DForDisc.m_impactPos.y, 0.f) -
	//			Vec3(centerXY.x, centerXY.y, 0.f)).GetNormalized();
	//		ray3D.m_impactDist = (ray3D.m_impactPos - rayStart).GetLength();
	//		if (result2D.m_impactPos.y == minMaxZ.m_max)
	//		{
	//			ray3D.m_impactNormal = Vec3(0.f, 0.f, 1.f);
	//		}
	//		if (result2D.m_impactPos.y == minMaxZ.m_min)
	//		{
	//			ray3D.m_impactNormal = Vec3(0.f, 0.f, -1.f);
	//		}
	//	}
	//}

	//return ray3D;

	//AABB2 bbox;
	//bbox.m_mins = Vec2(centerXY.x - radiusXY, minMaxZ.m_min);
	//bbox.m_maxs = Vec2(centerXY.x + radiusXY, minMaxZ.m_max);

	//RaycastResult2D rayForBBox;
	//rayForBBox.m_rayStartPos = Vec2(rayStart.x, rayStart.z);
	//rayForBBox.m_rayFwdNormal = Vec2(rayForwardNormal.x, rayForwardNormal.z).GetNormalized();
	//rayForBBox.m_rayMaxLength = DotProduct3D(rayLength * rayForwardNormal,
	//	Vec3((rayForBBox.m_rayFwdNormal.x), 0.f, (rayForBBox.m_rayFwdNormal.y)));
	//rayForBBox = RaycastVsAABB2D(rayForBBox, bbox);
	//if (rayForBBox.m_didImpact == false)
	//	return ray3D;

	//RaycastResult2D result2DForDisc;
	//result2DForDisc = RaycastVsDisc2D(Vec2(rayStart.x, rayStart.y), Vec2(rayForwardNormal.x, rayForwardNormal.y).GetNormalized(),
	//	DotProduct3D(rayForwardNormal * rayLength, Vec3(rayForwardNormal.x, rayForwardNormal.y, 0.f)),
	//	centerXY, radiusXY);
	//if (result2DForDisc.m_didImpact == false)
	//	return ray3D;

	//else
	//{
	//	//ray3D.m_didImpact = true;

	//	Vec2 fwdXY = Vec2(rayForwardNormal.x, rayForwardNormal.y).GetNormalized();
	//	float plainDiff = (result2DForDisc.m_impactPos - Vec2(rayStart.x, rayStart.y)).GetLength();
	//	float projectionXY = DotProduct3D(rayForwardNormal * rayLength, Vec3(fwdXY.x, fwdXY.y, 0.f));

	//	ray3D.m_impactPos = rayStart + rayForwardNormal*rayLength*(plainDiff/projectionXY);
	//	ray3D.m_impactNormal = Vec3(Vec3(result2DForDisc.m_impactPos.x, result2DForDisc.m_impactPos.y, 0.f) -
	//		Vec3(centerXY.x, centerXY.y, 0.f)).GetNormalized();
	//	ray3D.m_impactDist = (ray3D.m_impactPos - rayStart).GetLength();

	//	if (rayForBBox.m_impactNormal == Vec2(0.f,1.f))
	//	{
	//		ray3D.m_impactNormal = Vec3(0.f, 0.f, 1.f);
	//		//ray3D.m_impactPos = rayForBBox.m_impactPos;
	//		ray3D.m_impactDist = (ray3D.m_impactPos - rayStart).GetLength();
	//	}
	//	if (rayForBBox.m_impactNormal == Vec2(0.f, -1.f))
	//	{
	//		ray3D.m_impactNormal = Vec3(0.f, 0.f, -1.f);
	//		//ray3D.m_impactPos = rayForBBox.m_impactPos;
	//		ray3D.m_impactDist = (ray3D.m_impactPos - rayStart).GetLength();
	//	}

	//	if ((Vec2(ray3D.m_impactPos.x, ray3D.m_impactPos.y) - centerXY).GetLengthSquared() >= radiusXY * radiusXY)
	//	{
	//		RaycastResult3D rayFail;
	//		rayFail.m_didImpact = false;
	//		rayFail.m_rayStartPos = rayStart;
	//		rayFail.m_rayFwdNormal = rayForwardNormal;
	//		rayFail.m_rayMaxLength = rayLength;
	//		return rayFail;
	//	}
	//	else
	//	{
	//		ray3D.m_didImpact = true;
	//		return ray3D;
	//	}
	//}
	
}

RaycastResult3D RaycastVsOBB3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, OBB3 const& box)
{
	Vec3 rayEnd = rayStart + rayForwardNormal * rayLength;
	Vec3 localStart = box.GetLocalPosForWorldPos(rayStart);
	Vec3 localEnd = box.GetLocalPosForWorldPos(rayEnd);
	Vec3 localDir = (localEnd - localStart).GetNormalized();
	float localLength = (localEnd - localStart).GetLength();

	RaycastResult3D localResult =  RaycastVsAABB3D(localStart, localDir, localLength,
		AABB3(-box.m_halfDimensions, box.m_halfDimensions));

	RaycastResult3D result;
	result.m_rayStartPos = rayStart;
	result.m_rayFwdNormal = rayForwardNormal;
	result.m_rayMaxLength = rayLength;
	if (!localResult.m_didImpact)
	{
		return result;
	}
	
	result.m_didImpact = true;
	result.m_impactDist = rayLength * (localResult.m_impactDist / localLength);
	result.m_impactPos = box.GetWorldPosForLocalPos(localResult.m_impactPos);
	result.m_impactNormal =
		localResult.m_impactNormal.x * box.m_iBasisNormal +
		localResult.m_impactNormal.y * box.m_jBasisNormal +
		localResult.m_impactNormal.z * box.m_kBasisNormal;

	result.m_impactNormal.GetNormalized();

	return result;
}

RaycastResult3D RaycastVsPlane3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, Plane3 const& plane)
{
	RaycastResult3D result;

	float dotProduct = DotProduct3D(plane.m_normal, rayForwardNormal);
	if (dotProduct == 0.f)
		return result;

	float t = (plane.m_distToPlaneAloneNormalFromOrigin - DotProduct3D(plane.m_normal, rayStart)) / dotProduct;
	
	if (t < 0.f || t > rayLength)
	{
		return result;
	}

	result.m_didImpact = true;
	result.m_impactDist = t;
	result.m_impactPos = rayStart + rayForwardNormal * t;
	if (dotProduct <0.f)
	{
		result.m_impactNormal = plane.m_normal;
	}
	else
	{
		result.m_impactNormal = -plane.m_normal;
	}
	return result;
}

Mat44 GetBillboardMatrix(BillboardType billboardType, Mat44 const& targetMatrix, const Vec3& billboardPosition, const Vec2& billboardScale)
{
	Vec3 cameraI = targetMatrix.GetIBasis3D();
	Vec3 cameraJ = targetMatrix.GetJBasis3D();
	Vec3 cameraK = targetMatrix.GetKBasis3D();
	Vec3 cameraT = targetMatrix.GetTranslation3D();

	Vec3 bbI, bbJ, bbK, bbT;
	Vec3 worldUp = Vec3(0.f, 0.f, 1.f);
	Vec3 worldLeft = Vec3(0.f, 1.f, 0.f);

	if (billboardType == BillboardType::WORLD_UP_FACING)
	{
		bbI = (cameraT - billboardPosition).GetNormalized();
		bbI.z = 0.f;
		bbK = worldUp;
		bbJ = CrossProduct3D(bbK, bbI).GetNormalized();
	}
	if (billboardType == BillboardType::WORLD_UP_OPPOSING)
	{
		bbK = worldUp;
		bbI = -cameraI;
		bbI.z = 0.f;
		bbI.GetNormalized();
		bbJ = CrossProduct3D(bbK, bbI);
		bbJ.GetNormalized();
	}
	if (billboardType == BillboardType::FULL_FACING)
	{
		bbI = cameraT - billboardPosition;
		if (bbI.GetLengthSquared() == 0)
		{
			bbI = -cameraI;
			bbJ = -cameraJ;
			bbK = -cameraK;
		}
		else
		{
			bbI.GetNormalized();
			if (bbI.z > -1 && bbI.z < 1)
			{
				bbJ = CrossProduct3D(worldUp, bbI).GetNormalized();
				bbK = CrossProduct3D(bbI, bbJ).GetNormalized();
			}
			else
			{
				bbK = CrossProduct3D(bbI, worldLeft).GetNormalized();
				bbJ = CrossProduct3D(bbK, bbI).GetNormalized();
			}
		}
	}
	if (billboardType == BillboardType::FULL_OPPOSING)
	{
		bbI = -cameraI;
		bbJ = -cameraJ;
		bbK = cameraK;
	}
	Mat44 result;
	result.SetIJKT3D(bbI, bbJ, bbK, billboardPosition);
	result.AppendScaleNonUniform2D(billboardScale);
	return result;
}

float ComputeCubicBezier1D(float A, float B, float C, float D, float t)
{
	float ab = Interpolate(A, B, t);
	float bc = Interpolate(B, C, t);
	float cd = Interpolate(C, D, t);
	float abc = Interpolate(ab, bc, t);
	float bcd = Interpolate(bc, cd, t);
	float abcd = Interpolate(abc, bcd, t);
	return abcd;
}

float ComputeQuinticBezier1D(float A, float B, float C, float D, float E, float F, float t)
{
	float ab = Interpolate(A, B, t);
	float bc = Interpolate(B, C, t);
	float cd = Interpolate(C, D, t);
	float de = Interpolate(D, E, t);
	float ef = Interpolate(E, F, t);

	float abc = Interpolate(ab, bc, t);
	float bcd = Interpolate(bc, cd, t);
	float cde = Interpolate(cd, de, t);
	float def = Interpolate(de, ef, t);

	float abcd = Interpolate(abc, bcd, t);
	float bcde = Interpolate(bcd, cde, t);
	float cdef = Interpolate(cde, def, t);

	float abcde = Interpolate(abcd, bcde, t);
	float bcdef = Interpolate(bcde, cdef, t);

	float abcdef = Interpolate(abcde, bcdef, t);

	return abcdef;
}

float Identity(float t)
{
	return t;
}

float SmoothStart2(float t)
{
	return t*t;
}

float SmoothStart3(float t)
{
	return t*t*t;
}

float SmoothStart4(float t)
{
	return t*t*t*t;
}

float SmoothStart5(float t)
{
	return t*t*t*t*t;
}

float SmoothStart6(float t)
{
	return t*t*t*t*t*t;
}

float SmoothStop2(float t)
{
	return 1.f - ((1-t)*(1-t));
}

float SmoothStop3(float t)
{
	return 1.f - ((1-t)*(1-t)*(1-t));
}

float SmoothStop4(float t)
{
	return 1.f - ((1-t)*(1-t)*(1-t)*(1-t));
}

float SmoothStop5(float t)
{
	return 1.f - ((1-t)*(1-t)*(1-t)*(1-t)*(1-t));
}

float SmoothStop6(float t)
{
	return 1.f - ((1-t)*(1-t)*(1-t)*(1-t)*(1-t)*(1-t));
}

float SmoothStep3(float t)
{
	return ComputeCubicBezier1D(0, 0, 1, 1, t);
}

float SmoothStep5(float t)
{
	return ComputeQuinticBezier1D(0, 0, 0,1, 1, 1, t);
}

float Hesitate3(float t)
{
	return ComputeCubicBezier1D(0, 1, 0, 1, t);
}

float Hesitate5(float t)
{
	return ComputeQuinticBezier1D(0, 1, 0, 1, 0, 1, t);
}

float CustomFunkyEasing(float t)
{
	//return t;
	float wave = sinf(t * 10.0f) * 0.1f;     // 添加轻微抖动
	float ease = t * t * (3.0f - 2.0f * t);  // smoothstep base
	float bounce = fabsf(sinf(t * 3.14159f * 2.0f)) * (1.0f - t); // 尾部跳动

	return ease + wave * (1.0f - t) + bounce * 0.1f;
}

bool BounceDiscOffFixedDisc2D(Vec2& aCenter, Vec2& bCenter, float aRadius, float bRadius, Vec2& bVel, float elasticity)
{
	if (!DoDiscsOverlap(aCenter, aRadius, bCenter, bRadius))
	{
		return false;
	}
	PushDiscsOutOfEachOther2D(aCenter, aRadius, bCenter, bRadius);

	Vec2 normal = (bCenter - aCenter).GetNormalized();
	float velAlongNormal = DotProduct2D(bVel, normal);
	if (velAlongNormal >= 0.f)
	{
		return true;
	}
	Vec2 velN = velAlongNormal * normal;
	Vec2 velT = bVel - velN;
	bVel =  velT - (velN * elasticity);
	return true;
}

bool BounceDiscOffEachOther(Vec2& aCenter, Vec2& bCenter, float aRadius, float bRadius, Vec2& aVel, Vec2& bVel, float elasticity)
{
	if (!DoDiscsOverlap(aCenter, aRadius, bCenter, bRadius))
	{
		return false;
	}
	PushDiscsOutOfEachOther2D(aCenter, aRadius, bCenter, bRadius);
	
	Vec2 normalAB = (bCenter - aCenter).GetNormalized(); 
	Vec2 relativeVel = bVel - aVel;
	float velAlongNormal = DotProduct2D(relativeVel, normalAB);

	if (velAlongNormal >= 0.f)
	{
		return false;
	}
	
	Vec2 velNOfA = DotProduct2D(aVel, normalAB) * normalAB;
	Vec2 velNOfB = DotProduct2D(bVel, normalAB) * normalAB;
	Vec2 velTOfA = aVel - velNOfA;
	Vec2 velTOfB = bVel - velNOfB;
	aVel = velTOfA + velNOfB*(elasticity);
	bVel = velTOfB + velNOfA*(elasticity);

	return true;
	// Vec2 normalAB = (bCenter - aCenter).GetNormalized();
	// Vec2 velNOfA = DotProduct2D(aVel, normalAB) * normalAB;
	// Vec2 velNOfB = DotProduct2D(bVel, normalAB) * normalAB;
	// Vec2 velTOfA = aVel - velNOfA;
	// Vec2 velTOfB = bVel - velNOfB;
	// aVel = velTOfA + velNOfB*(elasticity);
	// bVel = velTOfB + velNOfA*(elasticity);
	//return true;
}

bool BounceDiscOffPoint(Vec2& discPos, float discRadius, Vec2& discVel, Vec2 const& point, float elasticity)
{
	if (IsPointInsideDisc2D(point, discPos, discRadius))
	{
		PushDiscOutOfFixedPoint2D(discPos, discRadius, point);
	}
	else
	{
		return false;
	}
	Vec2 pointNormal = (discPos - point).GetNormalized();
	Vec2 velH = DotProduct2D(discVel, pointNormal) * pointNormal;
	Vec2 velV = discVel -velH;
	velH *= -elasticity;
	discVel = velH + velV;

	return true;
	//return PushDiscOutOfFixedPoint2D(discPos, discRadius, point);
}

Vec3 GetRandomDirectionInSphere()
{
	RandomNumberGenerator* rng = new RandomNumberGenerator();
	float x = rng->RollRandomFloatInRange(-1.f, 1.f);
	float y = rng->RollRandomFloatInRange(-1.f, 1.f);
	float z = rng->RollRandomFloatInRange(-1.f, 1.f);

	Vec3 dir(x, y, z);
	if (dir.GetLengthSquared() <= 1.f)
		return dir.GetNormalized();
	else
	{
		return dir;
	}
}

Vec3 GetPerpendicularUnitVector(Vec3 const& normal)
{
	if (fabsf(normal.x) < 0.99f)
		return CrossProduct3D(normal, Vec3(1.f, 0.f, 0.f)).GetNormalized();
	else
		return CrossProduct3D(normal, Vec3(0.f, 1.f, 0.f)).GetNormalized();
}

size_t AlignUp(size_t value, size_t alignment)
{
	return ((value + alignment - 1) / alignment) * alignment;
}

int FloorDivision(int a, int b)
{
	int q = a / b;
	int r = a % b;
	if (r != 0 && ((r > 0) != (b > 0)))
	{
		--q;
	}
	return q;
}

int FloorToInt(float v)
{
	return (int)std::floor(v);
}

int GetEuclideanMod(int a, int b)
{
	int m = a % b;
	if (m < 0)
	{
		m += b;
	}
	return m;
}

float MinF(float a, float b)
{
	return (a < b) ? a : b;
}

float MaxF(float a, float b)
{
	return (a > b) ? a : b;
}

float SignF(float x)
{
	if (x > 0.f)
		return 1.f;
	if (x < 0.f)
		return -1.f;
	return 0.f;
}

int MinI(int a, int b)
{
	return (a < b) ? a : b;
}

int MaxI(int a, int b)
{
	return (a > b) ? a : b;
}

uint32_t MinUint32(uint32_t a, uint32_t b)
{
	return (a < b) ? a : b;
}

uint32_t MaxUint32(uint32_t a, uint32_t b)
{
	return (a > b) ? a : b;
}

float DiminishingAdd(float a, float b)
{
	return 1.0f - (1.0f - a) * (1.0f - b);
}

float DiminishingAdd(float a, float b, float c)
{
	return 1.0f - (1.0f - a) * (1.0f - b) * (1.0f - c);
}

Vec3 DiminishingAdd(const Vec3& a, const Vec3& b)
{
	return Vec3(
		1.0f - (1.0f - a.x) * (1.0f - b.x),
		1.0f - (1.0f - a.y) * (1.0f - b.y),
		1.0f - (1.0f - a.z) * (1.0f - b.z)
	);
}


