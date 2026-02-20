#pragma once

#include "Engine/Renderer/SpriteSheet.hpp"
#include "Engine/Renderer/SpriteDefinition.hpp"
#include "Engine/Math/AABB2.hpp"
#include <map>
#include <string>

// ============================================================================
//   UISpriteAtlas atlas(iconsTexture, IntVec2(1, 1));  // 不使用网格
//   atlas.DefineSprite("heart_full", 52, 0, 9, 9);
//   atlas.DefineSprite("heart_half", 61, 0, 9, 9);
//   AABB2 uvs = atlas.GetSpriteUVs("heart_full");
// ============================================================================
class UISpriteAtlas : public SpriteSheet
{
public:
    UISpriteAtlas(Texture& texture, IntVec2 const& gridLayout = IntVec2(1, 1));
    
    void DefineSprite(std::string const& name, int pixelX, int pixelY, int width, int height);
    
    void DefineSpriteUV(std::string const& name, Vec2 const& uvMins, Vec2 const& uvMaxs);
    
    AABB2 GetSpriteUVsByName(std::string const& name) const;
    bool HasSprite(std::string const& name) const;
    
    // 获取纹理尺寸（用于像素到UV转换）
    IntVec2 GetTextureDimensions() const { return m_textureDimensions; }
    
private:
    IntVec2 m_textureDimensions;
    std::map<std::string, AABB2> m_namedSprites;  // 名称 -> UV 坐标
};

