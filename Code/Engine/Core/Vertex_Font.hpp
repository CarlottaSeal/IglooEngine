#pragma once
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/Vec3.hpp"
#include "Engine/Core/Rgba8.hpp"

#pragma pack(push, 1)

struct Vertex_Font
{
	Vec3 m_position;
	Rgba8 m_color;
	Vec2 m_uv;
	Vec2 m_glyphPosition;	// [0,1] where this vert is within its glyph quad
	Vec2 m_textPosition;	// [0,1] where this vert is within the overall text block
	int m_characterIndex;	// which character in the string this vert belongs to
	float m_weight;			// font weight offset: 0 = normal, <0 = thinner, >0 = bolder

	Vertex_Font() = default;
	Vertex_Font(Vec3 const& pos, Rgba8 const& color, Vec2 const& uv,
		Vec2 const& glyphPos, Vec2 const& textPos, int charIndex, float weight = 0.f);
};

#pragma pack(pop)
