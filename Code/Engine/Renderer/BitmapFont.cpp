#include "BitmapFont.hpp"
#include "SpriteSheet.hpp"
#include "SpriteDefinition.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/StringUtils.hpp"
#include "Engine/Core/Image.hpp"

BitmapFont::BitmapFont(char const* fontFilePathNameWithNoExtension, Texture& fontTexture, bool autoWidth)
	:m_fontFilePathNameWithNoExtension(fontFilePathNameWithNoExtension)
	, m_fontGlyphsSpriteSheet(fontTexture, IntVec2(16, 16))
	, m_isProportional(autoWidth)
{
	// Initialize all aspects to 1.0
	for (int i = 0; i < 256; i++)
		m_glyphAspects[i] = 1.f;

	if (autoWidth)
	{
		std::string imagePath = std::string(fontFilePathNameWithNoExtension) + ".png";
		ComputeGlyphWidthsFromImage(imagePath.c_str());
	}
}

void BitmapFont::ComputeGlyphWidthsFromImage(char const* imageFilePath)
{
	Image image(imageFilePath);
	IntVec2 dims = image.GetDimensions();
	if (dims.x <= 0 || dims.y <= 0) return;

	int cellW = dims.x / 16;
	int cellH = dims.y / 16;
	if (cellW <= 0 || cellH <= 0) return;

	for (int glyphIndex = 0; glyphIndex < 256; glyphIndex++)
	{
		int gridX = glyphIndex % 16;
		int gridY = glyphIndex / 16;

		// Image is loaded with stbi_set_flip_vertically, so row 0 = bottom
		// Grid row 0 should be at the top of the original image = bottom after flip
		// Actually: grid position in original image: gridY from top
		// After vertical flip: originY = (15 - gridY) * cellH (bottom of the cell)
		int originX = gridX * cellW;
		int originY = (15 - gridY) * cellH; // flipped

		// Scan columns to find leftmost and rightmost non-transparent pixel
		int minCol = cellW;
		int maxCol = -1;

		for (int col = 0; col < cellW; col++)
		{
			for (int row = 0; row < cellH; row++)
			{
				int px = originX + col;
				int py = originY + row;
				if (px >= dims.x || py >= dims.y) continue;

				Rgba8 texel = image.GetTexelColor(IntVec2(px, py));
				if (texel.a > 10) // non-transparent threshold
				{
					if (col < minCol) minCol = col;
					if (col > maxCol) maxCol = col;
				}
			}
		}

		if (maxCol < minCol)
		{
			// Empty glyph (e.g. space) - use a reasonable default
			m_glyphAspects[glyphIndex] = 0.4f;
		}
		else
		{
			// Add 1 pixel of padding on each side
			float glyphWidth = (float)(maxCol - minCol + 1 + 2);
			m_glyphAspects[glyphIndex] = glyphWidth / (float)cellH;
		}
	}

	// Space character should be about 0.4 if empty
	if (m_glyphAspects[' '] < 0.1f)
		m_glyphAspects[' '] = 0.4f;
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
		int glyphIndex = (int)(unsigned char)c;
		if (glyphIndex < 0 || glyphIndex >= m_fontGlyphsSpriteSheet.GetNumSprites())
			continue;

		float aspect = m_isProportional ? m_glyphAspects[glyphIndex] : cellAspect;
		AABB2 glyphUVs = m_fontGlyphsSpriteSheet.GetSpriteUVs(glyphIndex);
		Vec2 glyphSize(cellHeight * aspect, cellHeight);
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
	mat.SetIJK3D(Vec3(0.f, 1.f, 0.f), Vec3(0.f, 0.f, 1.f), Vec3(1.f, 0.f, 0.f));
	TransformVertexArray3D(verts, mat);
}

void BitmapFont::AddVertsForTextInBox2D(std::vector<Vertex_PCU>& vertexArray, std::string const& text, AABB2 const& box, float cellHeight, Rgba8 const& tint, float cellAspectScale, Vec2 const& alignment, TextBoxMode mode, int maxGlyphsToDraw)
{
	Strings lines = SplitStringOnDelimiter(text, '\n');

	// Compute max line width
	float maxLineWidth = 0.f;
	for (const std::string& line : lines)
	{
		float lineWidth = 0.f;
		if (m_isProportional)
		{
			for (char c : line)
				lineWidth += cellHeight * m_glyphAspects[(unsigned char)c];
		}
		else
		{
			lineWidth = (float)line.length() * cellHeight * cellAspectScale;
		}
		if (lineWidth > maxLineWidth)
			maxLineWidth = lineWidth;
	}
	float totalHeight = cellHeight * lines.size();

	float scale = 1.f;
	if (mode == TextBoxMode::SHRINK_TO_FIT)
	{
		if (maxLineWidth > box.GetWidth())
			scale = box.GetWidth() / maxLineWidth;
		if (totalHeight > box.GetHeight())
			if (box.GetHeight() / totalHeight < scale)
				scale = box.GetHeight() / totalHeight;
	}
	float lineHeight = cellHeight * scale;
	Vec2 boxMins = box.m_mins;
	Vec2 boxSize = box.GetDimensions();

	int lineIndex = 0;
	int lineSize = (int)lines.size();
	int totalGlyphsDrawn = 0;

	for (int lIndex = 0; lIndex < (int)lines.size(); lIndex++)
	{
		if (totalGlyphsDrawn >= maxGlyphsToDraw)
			return;

		std::string const& curLine = lines[lineSize - 1 - lIndex];
		float lineWidth = 0.f;
		if (m_isProportional)
		{
			for (char c : curLine)
				lineWidth += lineHeight * m_glyphAspects[(unsigned char)c];
		}
		else
		{
			lineWidth = (float)curLine.length() * cellHeight * cellAspectScale * scale;
		}

		float startX = boxMins.x + (boxSize.x - lineWidth) * alignment.x;
		float startY = boxMins.y + (boxSize.y - lines.size() * lineHeight) * alignment.y;
		Vec2 currentPosition(startX, startY + lineIndex * lineHeight);

		for (char c : curLine)
		{
			if (totalGlyphsDrawn >= maxGlyphsToDraw)
				return;

			int glyphIndex = (int)(unsigned char)c;
			if (glyphIndex < 0 || glyphIndex >= m_fontGlyphsSpriteSheet.GetNumSprites())
				continue;

			//float aspect = m_isProportional ? m_glyphAspects[glyphIndex] : cellAspectScale * scale;
			float glyphWidth = m_isProportional ? lineHeight * m_glyphAspects[glyphIndex] : cellHeight * cellAspectScale * scale;

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
	if (m_isProportional)
	{
		float width = 0.f;
		for (char c : text)
			width += cellHeight * m_glyphAspects[(unsigned char)c];
		return width;
	}
	return (float)text.length() * cellHeight * cellAspect;
}

float BitmapFont::GetGlyphAspect(int glyphUnicode) const
{
	if (m_isProportional && glyphUnicode >= 0 && glyphUnicode < 256)
		return m_glyphAspects[glyphUnicode];
	return 1.0f;
}

float GetTextWidth(float cellHeight, std::string const& text, float cellAspect)
{
	return (float)text.length() * cellHeight * cellAspect;
}
