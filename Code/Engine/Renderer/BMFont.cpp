#include "BMFont.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Texture.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/StringUtils.hpp"
#include "Engine/Core/FileUtils.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include <sstream>

BMFont::BMFont(char const* fntFilePath, Renderer* renderer)
	: m_fntFilePath(fntFilePath)
{
	ParseFntFile(fntFilePath);

	// Load textures for each page
	// Derive directory from fnt file path
	std::string fntPath(fntFilePath);
	std::string dir;
	size_t lastSlash = fntPath.find_last_of("/\\");
	if (lastSlash != std::string::npos)
		dir = fntPath.substr(0, lastSlash + 1);

	// We need to load m_textures based on page file names stored during parsing
	// Page filenames are stored temporarily - we'll load them here
	// Re-parse just the page lines to get filenames
	std::string fileContents;
	FileReadToString(fileContents, fntFilePath);
	std::istringstream stream(fileContents);
	std::string line;
	while (std::getline(stream, line))
	{
		if (line.substr(0, 4) != "page")
			continue;

		// Find file="filename.png"
		size_t filePos = line.find("file=\"");
		if (filePos == std::string::npos)
			continue;
		filePos += 6; // skip file="
		size_t endQuote = line.find('\"', filePos);
		if (endQuote == std::string::npos)
			continue;
		std::string pageFile = dir + line.substr(filePos, endQuote - filePos);
		Texture* tex = renderer->CreateOrGetTextureFromFile(pageFile.c_str());
		m_textures.push_back(tex);
	}
}

Texture& BMFont::GetTexture() const
{
	return *m_textures[0];
}

static int ParseIntValue(std::string const& token)
{
	size_t eq = token.find('=');
	if (eq == std::string::npos) return 0;
	return atoi(token.substr(eq + 1).c_str());
}

void BMFont::ParseFntFile(std::string const& filePath)
{
	std::string fileContents;
	int result = FileReadToString(fileContents, filePath);
	GUARANTEE_OR_DIE(result >= 0, Stringf("Failed to load BMFont file: %s", filePath.c_str()));

	std::istringstream stream(fileContents);
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.empty()) continue;

		std::istringstream lineStream(line);
		std::string tag;
		lineStream >> tag;

		if (tag == "info")
		{
			// Parse padding
			std::string token;
			while (lineStream >> token)
			{
				if (token.substr(0, 8) == "padding=")
				{
					std::string vals = token.substr(8);
					Strings parts = SplitStringOnDelimiter(vals, ',');
					if (parts.size() >= 4)
					{
						m_padding[0] = atoi(parts[0].c_str());
						m_padding[1] = atoi(parts[1].c_str());
						m_padding[2] = atoi(parts[2].c_str());
						m_padding[3] = atoi(parts[3].c_str());
					}
				}
				else if (token.substr(0, 5) == "face=")
				{
					// Extract font name (may be quoted)
					size_t eq = token.find('=');
					std::string name = token.substr(eq + 1);
					if (!name.empty() && name[0] == '\"')
					{
						name = name.substr(1);
						if (!name.empty() && name.back() == '\"')
							name.pop_back();
					}
					m_fontName = name;
				}
			}
		}
		else if (tag == "common")
		{
			std::string token;
			while (lineStream >> token)
			{
				if (token.substr(0, 11) == "lineHeight=")
					m_lineHeight = ParseIntValue(token);
				else if (token.substr(0, 5) == "base=")
					m_base = ParseIntValue(token);
				else if (token.substr(0, 7) == "scaleW=")
					m_scaleW = ParseIntValue(token);
				else if (token.substr(0, 7) == "scaleH=")
					m_scaleH = ParseIntValue(token);
			}
		}
		else if (tag == "char")
		{
			BMFontGlyph g;
			int x = 0, y = 0, w = 0, h = 0;
			std::string token;
			while (lineStream >> token)
			{
				if (token.substr(0, 3) == "id=")
					g.id = ParseIntValue(token);
				else if (token.substr(0, 2) == "x=")
					x = ParseIntValue(token);
				else if (token.substr(0, 2) == "y=")
					y = ParseIntValue(token);
				else if (token.substr(0, 6) == "width=")
					w = ParseIntValue(token);
				else if (token.substr(0, 7) == "height=")
					h = ParseIntValue(token);
				else if (token.substr(0, 8) == "xoffset=")
					g.xOffset = (float)ParseIntValue(token);
				else if (token.substr(0, 8) == "yoffset=")
					g.yOffset = (float)ParseIntValue(token);
				else if (token.substr(0, 9) == "xadvance=")
					g.xAdvance = (float)ParseIntValue(token);
				else if (token.substr(0, 5) == "page=")
					g.page = ParseIntValue(token);
			}

			g.width = (float)w;
			g.height = (float)h;

			// Convert pixel coords to UV
			float invW = 1.f / (float)m_scaleW;
			float invH = 1.f / (float)m_scaleH;
			g.u0 = (float)x * invW;
			g.v0 = (float)y * invH;
			g.u1 = (float)(x + w) * invW;
			g.v1 = (float)(y + h) * invH;

			m_glyphs[g.id] = g;
		}
		else if (tag == "kerning")
		{
			int first = 0, second = 0, amount = 0;
			std::string token;
			while (lineStream >> token)
			{
				if (token.substr(0, 6) == "first=")
					first = ParseIntValue(token);
				else if (token.substr(0, 7) == "second=")
					second = ParseIntValue(token);
				else if (token.substr(0, 7) == "amount=")
					amount = ParseIntValue(token);
			}
			uint64_t key = ((uint64_t)first << 32) | (uint64_t)(unsigned int)second;
			m_kerning[key] = (float)amount;
		}
	}
}

BMFontGlyph const* BMFont::GetGlyph(int charId) const
{
	auto it = m_glyphs.find(charId);
	if (it != m_glyphs.end())
		return &it->second;
	return nullptr;
}

float BMFont::GetKerning(int first, int second) const
{
	uint64_t key = ((uint64_t)first << 32) | (uint64_t)(unsigned int)second;
	auto it = m_kerning.find(key);
	if (it != m_kerning.end())
		return it->second;
	return 0.f;
}

float BMFont::GetTextWidth(float cellHeight, std::string const& text) const
{
	if (m_lineHeight <= 0) return 0.f;
	float scale = cellHeight / (float)m_lineHeight;
	float cursorX = 0.f;

	for (int i = 0; i < (int)text.size(); i++)
	{
		int c = (unsigned char)text[i];
		BMFontGlyph const* g = GetGlyph(c);
		if (!g) continue;

		if (i > 0)
			cursorX += GetKerning((unsigned char)text[i - 1], c) * scale;

		cursorX += g->xAdvance * scale;
	}
	return cursorX;
}

void BMFont::AddVertsForText2D(std::vector<Vertex_PCU>& verts, Vec2 const& textMins,
	float cellHeight, std::string const& text, Rgba8 const& tint) const
{
	if (m_lineHeight <= 0) return;
	float scale = cellHeight / (float)m_lineHeight;
	float cursorX = textMins.x;
	float baseY = textMins.y;

	for (int i = 0; i < (int)text.size(); i++)
	{
		int c = (unsigned char)text[i];
		BMFontGlyph const* g = GetGlyph(c);
		if (!g) continue;

		if (i > 0)
			cursorX += GetKerning((unsigned char)text[i - 1], c) * scale;

		// BMFont yOffset is from top of line; we render from bottom-left
		float x0 = cursorX + g->xOffset * scale;
		float y0 = baseY + (m_lineHeight - g->yOffset - g->height) * scale;
		float x1 = x0 + g->width * scale;
		float y1 = y0 + g->height * scale;

		// Note: BMFont V is top-down, OpenGL/DX UV may need flipping
		// stb_image flips vertically on load, so v0 is bottom, v1 is top in our texture
		// But BMFont y=0 is top of texture. With stbi flip, y=0 becomes bottom.
		// So we need: v0_flipped = 1 - v1_original, v1_flipped = 1 - v0_original
		float vTop = 1.f - g->v0;
		float vBot = 1.f - g->v1;

		AABB2 quadBounds(Vec2(x0, y0), Vec2(x1, y1));
		AABB2 uvBounds(Vec2(g->u0, vBot), Vec2(g->u1, vTop));
		AddVertsForAABB2D(verts, quadBounds, tint, uvBounds);

		cursorX += g->xAdvance * scale;
	}
}

void BMFont::AddVertsForTextInBox2D(std::vector<Vertex_PCU>& verts, std::string const& text,
	AABB2 const& box, float cellHeight, Rgba8 const& tint, Vec2 const& alignment) const
{
	// Split into lines
	Strings lines = SplitStringOnDelimiter(text, '\n');

	float totalTextHeight = cellHeight * lines.size();

	// Find max line width for shrink-to-fit
	float maxLineWidth = 0.f;
	for (auto& line : lines)
	{
		float w = GetTextWidth(cellHeight, line);
		if (w > maxLineWidth) maxLineWidth = w;
	}

	// Compute scale for shrink-to-fit
	float fitScale = 1.f;
	if (maxLineWidth > box.GetWidth())
		fitScale = box.GetWidth() / maxLineWidth;
	if (totalTextHeight * fitScale > box.GetHeight())
		fitScale = box.GetHeight() / (cellHeight * lines.size());

	float actualHeight = cellHeight * fitScale;

	// Render each line (bottom-up, line 0 = bottom)
	for (int lineIdx = 0; lineIdx < (int)lines.size(); lineIdx++)
	{
		int reversedIdx = (int)lines.size() - 1 - lineIdx;
		std::string const& line = lines[reversedIdx];
		float lineWidth = GetTextWidth(actualHeight, line);

		float startX = box.m_mins.x + (box.GetWidth() - lineWidth) * alignment.x;
		float startY = box.m_mins.y + (box.GetHeight() - lines.size() * actualHeight) * alignment.y + lineIdx * actualHeight;

		AddVertsForText2D(verts, Vec2(startX, startY), actualHeight, line, tint);
	}
}

void BMFont::AddVertsForText2D(std::vector<Vertex_Font>& verts, Vec2 const& textMins,
	float cellHeight, std::string const& text, Rgba8 const& tint, float weight) const
{
	if (m_lineHeight <= 0) return;
	float scale = cellHeight / (float)m_lineHeight;
	float cursorX = textMins.x;
	float baseY = textMins.y;

	// Compute total width for textPosition normalization
	float totalWidth = GetTextWidth(cellHeight, text);
	if (totalWidth <= 0.f) totalWidth = 1.f;

	for (int i = 0; i < (int)text.size(); i++)
	{
		int c = (unsigned char)text[i];
		BMFontGlyph const* g = GetGlyph(c);
		if (!g) continue;

		if (i > 0)
			cursorX += GetKerning((unsigned char)text[i - 1], c) * scale;

		float x0 = cursorX + g->xOffset * scale;
		float y0 = baseY + (m_lineHeight - g->yOffset - g->height) * scale;
		float x1 = x0 + g->width * scale;
		float y1 = y0 + g->height * scale;

		float vTop = 1.f - g->v0;
		float vBot = 1.f - g->v1;

		// textPosition: normalized position within the entire text block
		float tx0 = (x0 - textMins.x) / totalWidth;
		float tx1 = (x1 - textMins.x) / totalWidth;
		float ty0 = 0.f;
		float ty1 = 1.f;

		// 6 verts per quad (2 triangles)
		// Triangle 1: BL, BR, TR
		verts.emplace_back(Vec3(x0, y0, 0.f), tint, Vec2(g->u0, vBot), Vec2(0.f, 0.f), Vec2(tx0, ty0), i, weight);
		verts.emplace_back(Vec3(x1, y0, 0.f), tint, Vec2(g->u1, vBot), Vec2(1.f, 0.f), Vec2(tx1, ty0), i, weight);
		verts.emplace_back(Vec3(x1, y1, 0.f), tint, Vec2(g->u1, vTop), Vec2(1.f, 1.f), Vec2(tx1, ty1), i, weight);
		// Triangle 2: BL, TR, TL
		verts.emplace_back(Vec3(x0, y0, 0.f), tint, Vec2(g->u0, vBot), Vec2(0.f, 0.f), Vec2(tx0, ty0), i, weight);
		verts.emplace_back(Vec3(x1, y1, 0.f), tint, Vec2(g->u1, vTop), Vec2(1.f, 1.f), Vec2(tx1, ty1), i, weight);
		verts.emplace_back(Vec3(x0, y1, 0.f), tint, Vec2(g->u0, vTop), Vec2(0.f, 1.f), Vec2(tx0, ty1), i, weight);

		cursorX += g->xAdvance * scale;
	}
}
