#include "Vertex_Font.hpp"

Vertex_Font::Vertex_Font(Vec3 const& pos, Rgba8 const& color, Vec2 const& uv,
	Vec2 const& glyphPos, Vec2 const& textPos, int charIndex, float weight)
	: m_position(pos)
	, m_color(color)
	, m_uv(uv)
	, m_glyphPosition(glyphPos)
	, m_textPosition(textPos)
	, m_characterIndex(charIndex)
	, m_weight(weight)
{
}
