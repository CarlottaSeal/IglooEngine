#pragma once
#include "Texture.hpp"
#include "Renderer.hpp"
#include "SpriteSheet.hpp"
//#include "Engine/Core/Rgba8.hpp"
#include <vector>

enum TextBoxMode
{
	SHRINK_TO_FIT,
	OVERRUN,
};

class BitmapFont
{
	friend class Renderer; // Only the Renderer can create new BitmapFont objects!
	friend class DX11Renderer;
	friend class DX12Renderer;
	friend class VulkanRenderer;

private:
	BitmapFont(char const* fontFilePathNameWithNoExtension, Texture& fontTexture, bool autoWidth = false);

public:
	Texture& GetTexture();
	void AddVertsForText2D(std::vector<Vertex_PCU>& vertexArray, Vec2 const& textMins,
		float cellHeight, std::string const& text, Rgba8 const& tint = Rgba8::WHITE, float cellAspect = 0.7f);

	void AddVertsForText3DAtOriginXForward(std::vector<Vertex_PCU>& verts, float cellHeight,
		std::string const& text, Rgba8 const& tint = Rgba8::WHITE, float cellAspect = 1.f,
		Vec2 const& alignment = Vec2(0.5f, 0.5f), int maxxGlyphsToDraw = 99999999);

	void AddVertsForTextInBox2D(std::vector<Vertex_PCU>& vertexArray, std::string const& text,
		AABB2 const& box, float cellHeight, Rgba8 const& tint = Rgba8::WHITE, float cellAspectScale = 1.f,
		Vec2 const& alignment = Vec2(0.5f, 0.5f), TextBoxMode mode = TextBoxMode::SHRINK_TO_FIT,
		int maxGlyphsToDraw = 99999999);
	float GetTextWidth(float cellHeight, std::string const& text, float cellAspect = 1.f);
	bool IsProportional() const { return m_isProportional; }
	void ComputeProportionalWidths();

protected:
	float GetGlyphAspect(int glyphUnicode) const;
	void ComputeGlyphWidthsFromImage(char const* imageFilePath);

protected:
	std::string m_fontFilePathNameWithNoExtension;
	SpriteSheet m_fontGlyphsSpriteSheet;
	bool m_isProportional = false;
	float m_glyphAspects[256] = {};	// per-glyph aspect ratios (width/height)
};

float GetTextWidth(float cellHeight, std::string const& text, float cellAspect);

