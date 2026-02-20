#include "BitmapFont.hpp"
#include "SpriteSheet.hpp"
#include "SpriteDefinition.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/StringUtils.hpp"

BitmapFont::BitmapFont(char const* fontFilePathNameWithNoExtension, Texture& fontTexture)
    :m_fontFilePathNameWithNoExtension(fontFilePathNameWithNoExtension)
    , m_fontGlyphsSpriteSheet(fontTexture, IntVec2(16, 16))
{
}

Texture& BitmapFont::GetTexture()
{
    return m_fontGlyphsSpriteSheet.GetTexture();
}

void BitmapFont::AddVertsForText2D(std::vector<Vertex_PCU>& vertexArray, Vec2 const& textMins, float cellHeight, std::string const& text, Rgba8 const& tint, float cellAspect)
{
	Vec2 currentPosition = textMins;

	for (char c : text)
	{
		int glyphIndex = static_cast<int>(c);

		if (glyphIndex < 0 || glyphIndex >= m_fontGlyphsSpriteSheet.GetNumSprites())
		{
			continue;
		}

		AABB2 glyphUVs = m_fontGlyphsSpriteSheet.GetSpriteUVs(glyphIndex);
		Vec2 glyphSize(cellHeight * cellAspect, cellHeight);
		AABB2 glyphBounds(currentPosition, currentPosition + glyphSize);

		AddVertsForAABB2D(vertexArray, glyphBounds, tint, glyphUVs);

		currentPosition.x += glyphSize.x;
	}
}

void BitmapFont::AddVertsForText3DAtOriginXForward(std::vector<Vertex_PCU>& verts, float cellHeight, std::string const& text, Rgba8 const& tint, float cellAspect, Vec2 const& alignment, int maxGlyphsToDraw)
{
	AddVertsForTextInBox2D(verts, text, AABB2(Vec2::ZERO, Vec2(1.f, cellHeight)), cellHeight, tint, cellAspect, alignment, OVERRUN, maxGlyphsToDraw);
	
	AABB2 box = GetVertexBounds2D(verts);
	float boxW = box.m_maxs.x - box.m_mins.x;
	float boxH = box.m_maxs.y - box.m_mins.y;
	Mat44 translateMat;
	translateMat.SetTranslation3D(Vec3(-alignment.x * boxW, -alignment.y * boxH, 0.f));
	TransformVertexArray3D(verts, translateMat);

	Mat44 mat;
	mat.SetIJK3D(Vec3(0.f, 1.f,0.f), Vec3(0.f, 0.f, 1.f), Vec3(1.f, 0.f, 0.f)); //x->y, y->z, z->x
	TransformVertexArray3D(verts, mat);
}

void BitmapFont::AddVertsForTextInBox2D(std::vector<Vertex_PCU>& vertexArray, std::string const& text, AABB2 const& box, float cellHeight, Rgba8 const& tint, float cellAspectScale, Vec2 const& alignment, TextBoxMode mode, int maxGlyphsToDraw)
{
	Strings lines = SplitStringOnDelimiter(text,'\n');

	float maxLineWidth = 0.f;
	for (const std::string& line : lines)
	{
		float lineWidth = static_cast<float>(line.length()) * cellHeight * cellAspectScale;
		if (lineWidth > maxLineWidth)
		{
			maxLineWidth = lineWidth;
		}
	}
	float totalHeight = cellHeight * lines.size();

	float scale = 1.f;
	if (mode == TextBoxMode::SHRINK_TO_FIT)
	{
		if (maxLineWidth > box.GetWidth())
		{
			scale = box.GetWidth() / maxLineWidth;
		}
		if (totalHeight > box.GetHeight())
		{
			if (box.GetHeight() / totalHeight < scale)
			{
				scale = box.GetHeight() / totalHeight;
			}
		}
	}
	float glyphWidth = cellHeight * cellAspectScale * scale;
	float lineHeight = cellHeight * scale;

	Vec2 boxMins = box.m_mins;
	Vec2 boxMaxs = box.m_maxs;
	Vec2 boxSize = box.GetDimensions();

	Vec2 currentPosition;
	int lineIndex = 0;
	int lineSize = static_cast<int>(lines.size());

	int totalGlyphsDrawn = 0;

	for (int lIndex = 0; lIndex<lines.size();lIndex++)
	{
		/*if (lineIndex >= maxGlyphsToDraw)
		{
			return;
		}*/
		if (totalGlyphsDrawn >= maxGlyphsToDraw)
		{
			return;
		}

		float lineWidth = static_cast<float>(lines[lineSize-1- lIndex].length()) * glyphWidth;

		float startX = boxMins.x + (boxSize.x - lineWidth) * alignment.x;
		float startY = boxMins.y + (boxSize.y - lines.size() * lineHeight) * alignment.y;
		currentPosition = Vec2(startX, startY + lineIndex * lineHeight);

		for (char c : lines[lineSize - 1 - lIndex])
		{
			if (totalGlyphsDrawn >= maxGlyphsToDraw)
			{
				return;
			}

			int glyphIndex = static_cast<int>(c);
			if (glyphIndex < 0 || glyphIndex >= m_fontGlyphsSpriteSheet.GetNumSprites())
			{
				continue;
			}

			AABB2 glyphUVs = m_fontGlyphsSpriteSheet.GetSpriteUVs(glyphIndex);

			AABB2 glyphBounds(currentPosition, currentPosition + Vec2(glyphWidth, lineHeight));

			AddVertsForAABB2D(vertexArray, glyphBounds, tint, glyphUVs);

			currentPosition.x += glyphWidth;
		}

		lineIndex++;

		totalGlyphsDrawn++;
	}
}

float BitmapFont::GetTextWidth(float cellHeight, std::string const& text, float cellAspect)
{
	return static_cast<float>(text.length()) * cellHeight * cellAspect;
}

float BitmapFont::GetGlyphAspect(int glyphUnicode) const
{
	UNUSED(glyphUnicode);
	return 1.0f;
	//if (glyphUnicode == 'W' || glyphUnicode == 'M' || glyphUnicode == 'Q' ||
	//	(glyphUnicode >= '0' && glyphUnicode <= '9'))
	//{
	//	return 0.8f;
	//}
	//// 窄字符（i, l, t, f, 逗号等）
	//else if (glyphUnicode == 'i' || glyphUnicode == 'l' || glyphUnicode == 't' ||
	//	glyphUnicode == 'f' || glyphUnicode == ',' || glyphUnicode == '.')
	//{
	//	return 0.4f;
	//}
	//
	//return 0.6f;
}

float GetTextWidth(float cellHeight, std::string const& text, float cellAspect)
{
	return static_cast<float>(text.length()) * cellHeight * cellAspect;
}