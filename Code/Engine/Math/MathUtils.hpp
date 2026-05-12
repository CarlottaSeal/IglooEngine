#pragma once
#include "Engine/Math/Vec2.hpp"
#include <string>

#include "OBB3.h"
#include "Plane3.h"
#include "Vec3.hpp"

struct IntVec3;
struct Vec2;
struct Vec3;
struct Vec4;
struct IntVec2;
struct AABB2;
struct OBB2;
struct Mat44;
struct AABB3;
struct FloatRange;

enum class BillboardType
{
	NONE = -1,
	WORLD_UP_FACING,
	WORLD_UP_OPPOSING,
	FULL_FACING,
	FULL_OPPOSING,
	COUNT
};

struct RaycastResult2D
{
	// Basic ray-cast result information (required)
	bool m_didImpact = false;
	float m_impactDist = 0.f;
	Vec2 m_impactPos;
	Vec2 m_impactNormal;
	//ray itself
	Vec2 m_rayFwdNormal;
	Vec2 m_rayStartPos;
	float m_rayMaxLength = 1.f;
};

struct RaycastResult3D
{
	// Basic ray-cast result information (required)
	bool m_didImpact = false;
	float m_impactDist = 0.f;
	Vec3 m_impactPos;
	Vec3 m_impactNormal;
	//ray itself
	Vec3 m_rayFwdNormal;
	Vec3 m_rayStartPos;
	float m_rayMaxLength = 1.f;

	//New added: Object
	uint32_t m_objectID;
};

//Angle utilities
float ConvertDegreesToRadians(float degrees);
float ConvertRadiansToDegrees(float radians);
float CosDegrees(float degrees);
float SinDegrees(float degrees);
float Atan2Degrees(float y, float x);

float GetShortestAngularDispDegrees(float startDegrees, float endDegrees);
float GetTurnedTowardDegrees(float currentDegrees, float goalDegrees, float maxDeltaDegrees);
float GetAngleDegreesBetweenVectors2D(Vec2 const& a, Vec2 const& b);

//Distance & Projections utilities
float GetDistance2D(Vec2 const& a, Vec2 const& b);
float GetDistanceSquared2D(Vec2 const& positionA, Vec2 const& positionB);
float GetDistance3D(Vec3 const& positionA, Vec3 const& positionB);
float GetDistanceSquared3D(Vec3 const& positionA, Vec3 const& positionB);
float GetDistanceXY3D(Vec3 const& positionA, Vec3 const& positionB);
float GetDistanceXYSquared3D(Vec3 const& positionA, Vec3 const& positionB);
int GetTaxicabDistance2D( IntVec2 const& pointA, IntVec2 const& pointB);
int GetTaxicabDistance3D( IntVec3 const& pointA, IntVec3 const& pointB);
float GetProjectedLength2D( Vec2 const& vectorToProject, Vec2 const& vectorToProjectOnto );
Vec2 const GetProjectedOnto2D(Vec2 const& vectorToProject, Vec2 const& vectorToProjectOnto );


//Geometric queries
bool IsPointInsideDisc2D(Vec2 const& point, Vec2 const& discCenter, float discRadius);
bool IsPointInsideOrientedSector2D( Vec2 const& point, Vec2 const& sectorTip, float sectorForwardDegrees
								  , float sectorApertureDegrees, float sectorRadius );
bool IsPointInsideDirectedSector2D( Vec2 const& point, Vec2 const& sectorTip, Vec2 const& sectorForwardNormal
								  , float sectorApertureDegrees, float sectorRadius	);
bool IsPointInsideAABB2D(Vec2 const& point, AABB2 const& box);
bool IsPointInsideOBB2D(Vec2 const& point, OBB2 const& box);
bool IsPointInsideCapsule(Vec2 const& point, Vec2 const& boneStart, Vec2 const& boneEnd, float radius);
bool IsPointInsideTriangle2D(Vec2 const& point, Vec2 const& triCCW0, Vec2 const& triCCW1, Vec2 const& triCCW2);

bool IsPointInsideSphere3D(Vec3 const& point, Vec3 sphereCenter, float radius);

bool DoDiscsOverlap (Vec2 const& centerA, float radiusA, Vec2 const& centerB, float radiusB);

bool DoSpheresOverlap3D(Vec3 const& centerA, float radiusA, Vec3 const& centerB, float radiusB);
bool DoSphereAndAABBOverlap3D(Vec3 sphereCenter, float sphereRadius, AABB3 box);
bool DoSphereAndPlaneOverlap3D(Vec3 const& sphereCenter, float const& sphereRadius, Plane3 const& plane);
bool DoSphereAndOBBOverlap3D(Vec3 const& sphereCenter, float const& sphereRadius, OBB3 const& box);
bool DoAABBsOverlap3D(AABB3 const& first, AABB3 const& second);
bool DoAABBAndPlaneOverlap3D(AABB3 const& box, Plane3 const& plane);
bool DoZCylindersOverlap3D(Vec2 cylinder1CenterXY, float cylinder1Radius, FloatRange cylinder1MinMaxZ, Vec2 cylinder2CenterXY, float cylinder2Radius, FloatRange cylinder2MinMaxZ);
bool DoZCylinderAndAABBOverlap3D(Vec2 cylinderCenterXY, float cylinderRadius, FloatRange cylinderMinMaxZ, AABB3 box);
bool DoZCylinderAndSphereOverlap3D(Vec2 cylinderCenterXY, float cylinderRadius, FloatRange cylinderMinMaxZ, Vec3 sphereCenter, float sphereRadius);
bool DoOBBAndPlaneOverlap3D(OBB3 const& box, Plane3 const& plane);

Vec2 GetNearestPointOnDisc2D(Vec2 const& referencePosition, Vec2 const& discCenter, float discRadius);
Vec2 GetNearestPointOnAABB2D(Vec2 const& referencePosition, AABB2 const& alignedBox);
Vec2 GetNearestPointOnOBB2D(Vec2 const& referencePosition, OBB2 const& orientedBox);
Vec2 GetNearestPointOnInfiniteLine2D(Vec2 const& referencePosition, Vec2 const& pointOnLine, Vec2 const& anotherPointOnline);
Vec2 GetNearestPointOnLineSegment2D(Vec2 const& referencePosition, Vec2 const& start, Vec2 const& end);
Vec2 GetNearestPointOnCapsule2D(Vec2 const& referencePosition, Vec2 const& boneStart, Vec2 const& boneEnd, float radius);
Vec2 GetNearestPointOnTriangle2D(Vec2 const& referencePosition, Vec2 const& triCCW0, Vec2 const& triCCW1, Vec2 const& triCCW2);

Vec3 GetNearestPointOnAABB3D(Vec3 const& referencePos, AABB3 box);
Vec3 GetNearestPointOnCylinderZ(Vec3 const& referencePos, Vec2 const& centerXY, FloatRange const& minMaxZ, float radiusXY);
Vec3 GetNearestPointOnSphere(Vec3 const& referencePos, Vec3 const& sphereCenter, float const& sphereRadius);
Vec3 GetNearestPointOnOBB3D(Vec3 const& referencePos, OBB3 const& box);
Vec3 GetNearestPointOnPlane3D(Vec3 const& referencePos, Plane3 const& plane);

float DistanceSquaredPointToSegment(const Vec3& p, const Vec3& a, const Vec3& b);
float DistanceToTriangle(const Vec3& p, const Vec3& a, 
						   const Vec3& b, const Vec3& c);
float DistanceSquaredToTriangle(const Vec3& p, const Vec3& a,
	const Vec3& b, const Vec3& c);

bool PushDiscOutOfFixedPoint2D(Vec2& mobileDiscCenter, float discRadius, Vec2 const& fixedPoint);
bool PushDiscOutOfFixedDisc2D(Vec2& mobileDiscCenter, float mobileDiscRadius, Vec2 const& fixedDiscCenter, float fixedDiscRadius);
bool PushDiscsOutOfEachOther2D(Vec2& aCenter, float aRadius, Vec2& bCenter, float bRadius);
bool PushDiscOutOfFixedAABB2D(Vec2& mobileDiscCenter, float discRadius, AABB2 const& fixedBox);
bool PushDiscOutOfFixedOBB2D(OBB2 const& obb, Vec2& mobileDiscCenter, float discRadius);

//Transformation utilities
void TransformPosition2D(Vec2& posToTransform, float uniformscale,
						 float rotationDegrees, Vec2 const& translation);
void TransformPosition2D(Vec2& posToTransform, Vec2 const& iBasis,
						 Vec2 const& jBasis, Vec2 const& translation);

void TransformPositionXY3D(Vec3& positionToTransform, float scaleXY,
						   float zRotationDegrees, Vec2 const& translationXY);
void TransformPositionXY3D(Vec3& positionToTransform, Vec2 const& iBasis,
						   Vec2 const& jBasis, Vec2 const& translationXY);
void TransformPositionXYZ3D(Vec3& positionToTransform, Vec3 const& iBasis,
	Vec3 const& jBasis, Vec3 const& kBasis, Vec3 const& translation);
 
//Clamp and Lerps
float GetClamped(float value, float minValue, float maxValue);
int GetClampedInt(int value, int minValue, int maxValue);
float GetClampedZeroToOne(float value);
float Interpolate(float start, float end, float fractionTowardEnd);
Vec2 InterpolateVec2(Vec2 start, Vec2 end, float fractionTowardEnd);
Vec3  InterpolateVec3(Vec3 start, Vec3 end, float fractionTowardEnd);
float GetFractionWithinRange(float value, float rangeStart, float rangeEnd);
float RangeMap(float inValue, float inStart, float inEnd, float outStart, float outEnd);
float RangeMapClamped(float inValue, float inStart, float inEnd, float outStart, float outEnd);

int   RoundDownToInt(float value);
std::string RoundToOneDecimalString(float value);
std::string RoundToTwoDecimalsString(float value);

//Dot and Cross
float DotProduct2D(Vec2 const& a, Vec2 const& b);
float DotProduct3D(Vec3 const& a, Vec3 const& b);
float DotProduct4D(Vec4 const& a, Vec4 const& b);
float CrossProduct2D(Vec2 const& a, Vec2 const& b);
Vec3  CrossProduct3D(Vec3 const& a, Vec3 const& b);

float NormalizeByte(unsigned char byteValue);
unsigned char DenormalizeByte(float normalizedValue);

//RaycastResult2D Utilities
RaycastResult2D RaycastVsDisc2D(Vec2 startPos, Vec2 fwdNormal, float maxDist, Vec2 discCenter, float discRadius);
RaycastResult2D RaycastVsLineSegment2D(RaycastResult2D ray, Vec2 const& start, Vec2 const& end);
RaycastResult2D RaycastVsAABB2D(RaycastResult2D ray, const AABB2 aabb);

//RaycastResult3D Utilities
RaycastResult3D RaycastVsAABB3D(Vec3 rayStart, Vec3 rayForwardNoral, float rayLength, AABB3 box);
RaycastResult3D RaycastVsSphere3D(Vec3 rayStart, Vec3 rayForwardNoral, float rayLength, Vec3 sphereCenter, float sphereRadius);
RaycastResult3D RaycastVsCylinderZ3D(Vec3 rayStart, Vec3 rayForwardNoral, float rayLength, Vec2 const& centerXY, FloatRange const& minMaxZ, float radiusXY);
RaycastResult3D RaycastVsOBB3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, OBB3 const& box);
RaycastResult3D RaycastVsPlane3D(Vec3 rayStart, Vec3 rayForwardNormal, float rayLength, Plane3 const& plane);

//Functions for billboard
Mat44 GetBillboardMatrix(BillboardType billboardType, Mat44 const& targetMatrix,
	const Vec3& billboardPosition, const Vec2& billboardScale = Vec2::ONE);  //create a billboard matrix.

//Easing
float ComputeCubicBezier1D(float A, float B, float C, float D, float t);
float ComputeQuinticBezier1D(float A, float B, float C, float D, float E, float F, float t);
float Identity(float t);
float SmoothStart2(float t);
float SmoothStart3(float t);
float SmoothStart4(float t);
float SmoothStart5(float t);
float SmoothStart6(float t);
float SmoothStop2(float t);
float SmoothStop3(float t);
float SmoothStop4(float t);
float SmoothStop5(float t);
float SmoothStop6(float t);
float SmoothStep3(float t);
float SmoothStep5(float t);
float Hesitate3(float t);
float Hesitate5(float t);
float CustomFunkyEasing(float t);

//Signed Distance Fields (2D primitives)
float GetDistanceSDF_Disc2D(Vec2 const& p, Vec2 const& center, float radius);
float GetDistanceSDF_AABB2D(Vec2 const& p, AABB2 const& box);
float GetDistanceSDF_AABB2D(Vec2 const& p, Vec2 const& center, Vec2 const& halfDims);
float GetDistanceSDF_OBB2D(Vec2 const& p, Vec2 const& center, Vec2 const& iBasis, Vec2 const& halfDims);
float GetDistanceSDF_OBB2D(Vec2 const& p, Vec2 const& center, Vec2 const& halfDims, float rotationDegrees);
float GetDistanceSDF_LineSegment2D(Vec2 const& p, Vec2 const& a, Vec2 const& b, float thickness);
float GetDistanceSDF_Capsule2D(Vec2 const& p, Vec2 const& boneStart, Vec2 const& boneEnd, float radius);
float GetDistanceSDF_Plane2D(Vec2 const& p, Vec2 const& unitNormal, float distFromOrigin);
float GetDistanceSDF_HexagonRegular2D(Vec2 const& p, Vec2 const& center, float radius);

//SDF combine operations (scalar -> scalar)
float SDFUnion(float a, float b);
float SDFIntersection(float a, float b);
float SDFSubtract(float a, float b);
float SDFSmoothUnion(float a, float b, float k);
float SDFSmoothIntersection(float a, float b, float k);
float SDFSmoothSubtract(float a, float b, float k);

//Bounce
bool BounceDiscOffFixedDisc2D(Vec2& aCenter, Vec2& bCenter, float aRadius, float bRadius, Vec2& bVel, float elasticity);
bool BounceDiscOffEachOther(Vec2& aCenter, Vec2& bCenter, float aRadius, float bRadius, Vec2& aVel, Vec2& bVel, float elasticity);
bool BounceDiscOffPoint(Vec2& discPos, float discRadius, Vec2& discVel, Vec2 const& point, float elasticity);

//GetRandomDirection
Vec3 GetRandomDirectionInSphere();

//Others
Vec3 GetPerpendicularUnitVector(Vec3 const& normal);

//Data
size_t AlignUp(size_t value, size_t alignment);

int FloorDivision(int a, int b);
int FloorToInt(float v);

int GetEuclideanMod(int a, int b);

//Comparison & numbers
float MinF(float a, float b);
float MaxF(float a, float b);
float SignF(float x);
int MinI(int a, int b);
int MaxI(int a, int b);
uint32_t MinUint32(uint32_t a, uint32_t b);
uint32_t MaxUint32(uint32_t a, uint32_t b);
float DiminishingAdd(float a, float b);
float DiminishingAdd(float a, float b, float c);
Vec3 DiminishingAdd(const Vec3& a, const Vec3& b);
