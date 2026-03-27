#pragma once
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Core/Vertex_PCU.hpp"
#include "Engine/Core/Vertex_Font.hpp"
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/AABB2.hpp"
#include <string>
#include <vector>
#include <map>

class Texture;
class Renderer;
class DX11Renderer;

struct BMFontGlyph
{
	int id = 0;
	float u0 = 0.f, v0 = 0.f, u1 = 0.f, v1 = 0.f;	// UV coords in texture
	float width = 0.f, height = 0.f;					// pixel size on sheet
	float xOffset = 0.f, yOffset = 0.f;				// offset from cursor to top-left of glyph
	float xAdvance = 0.f;								// how far to advance cursor after this glyph
	int page = 0;
};

class BMFont
{
	friend class Renderer;
	friend class DX11Renderer;

private:
	BMFont(char const* fntFilePath, Renderer* renderer);

public:
	Texture& GetTexture() const;

	// Tier 3: standard Vertex_PCU text generation
	void AddVertsForText2D(std::vector<Vertex_PCU>& verts, Vec2 const& textMins,
		float cellHeight, std::string const& text, Rgba8 const& tint = Rgba8::WHITE) const;

	void AddVertsForTextInBox2D(std::vector<Vertex_PCU>& verts, std::string const& text,
		AABB2 const& box, float cellHeight, Rgba8 const& tint = Rgba8::WHITE,
		Vec2 const& alignment = Vec2(0.5f, 0.5f)) const;

	// Tier 5: custom Vertex_Font text generation
	void AddVertsForText2D(std::vector<Vertex_Font>& verts, Vec2 const& textMins,
		float cellHeight, std::string const& text, Rgba8 const& tint = Rgba8::WHITE,
		float weight = 0.f) const;

	float GetTextWidth(float cellHeight, std::string const& text) const;

	// Kerning lookup
	float GetKerning(int first, int second) const;

private:
	void ParseFntFile(std::string const& filePath);
	BMFontGlyph const* GetGlyph(int charId) const;

	std::string m_fntFilePath;
	std::string m_fontName;
	int m_lineHeight = 0;		// line height in pixels (from font file)
	int m_base = 0;			// baseline offset in pixels
	int m_scaleW = 0, m_scaleH = 0;	// texture atlas size
	int m_padding[4] = {};		// top, right, bottom, left

	std::map<int, BMFontGlyph> m_glyphs;
	std::map<uint64_t, float> m_kerning;	// key = (first<<32)|second

	std::vector<Texture*> m_textures;	// one per page
};
