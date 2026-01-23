#pragma once
#include "Engine/Math/Vec2.hpp"

struct AABB2
{
public:
	Vec2 m_mins;
	Vec2 m_maxs;

public:
	AABB2();					
	~AABB2();					
	AABB2(AABB2 const& copyFrom);	
	explicit AABB2(float minX, float minY, float maxX, float maxY);     
	explicit AABB2(Vec2 const& mins, Vec2 const& maxs);					

	//Accessors (const methods)
	bool IsPointInside(Vec2 const& point) const;
	Vec2 const GetCenter() const;
	Vec2 const GetDimensions() const;
	float const GetWidth() const;
	float const GetHeight() const;
	Vec2 const GetBottomCenter() const;
	Vec2 const GetBottomRight() const;
	Vec2 const GetTopLeft() const;
	Vec2 const GetNearestPoint(Vec2 const& uv) const;
	Vec2 const GetPointAtUV(Vec2 const& uv) const;			//uv=(0.0) is at mins; uv=(1,1) is at maxs		
	Vec2 const GetUVForPoint(Vec2 const& point) const;		//uv=(.5,.5) at the center; u or v outside [0,1] extrapolated

	static const AABB2 ZERO_TO_ONE;

	//Mutators (non-const methods)
	void Translate(Vec2 const& translationToApply);
	void SetCenter(Vec2 const& newCenter);
	void SetDimensions(Vec2 const& newDimensions);
	void StretchToIncludePoint(Vec2 const& point);			//does minimal stretching required (none if already on point)
	void ClampWithin(AABB2 const& clampedAABB);
	void ReduceToAspect(float newAspect);
	void EnlargeToAspect(float newAspect);
	AABB2 GetNormalizedAABB2(AABB2 const& standardBounds);

	AABB2 ChopOffTop(float percentOfOriginalToChop, float extraHeightOfOriginalToChop = 0.f);
	AABB2 ChopOffBottom(float percentOfOriginalToChop, float extraHeightOfOriginalToChop = 0.f);
	AABB2 ChopOffLeft(float percentOfOriginalToChop, float extraHeightOfOriginalToChop = 0.f);
	AABB2 ChopOffRight(float percentOfOriginalToChop, float extraHeightOfOriginalToChop = 0.f);
	AABB2 GetBoxWithUV(Vec2 uvMin, Vec2 uvMax) const;
	AABB2 GetBoxWithinIndex(int index, int count, bool horizontal) const;
	AABB2 AddPadding(float paddingX, float paddingY) const;
};