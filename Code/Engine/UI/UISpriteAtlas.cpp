#include "UISpriteAtlas.h"

UISpriteAtlas::UISpriteAtlas(Texture& texture, IntVec2 const& gridLayout)
    : SpriteSheet(texture, gridLayout)
{
    m_textureDimensions = texture.GetDimensions();
}

void UISpriteAtlas::DefineSprite(std::string const& name, int pixelX, int pixelY, int width, int height)
{
    float texW = (float)m_textureDimensions.x;
    float texH = (float)m_textureDimensions.y;
    
    // 像素坐标转 UV（注意 Y 轴方向）
    // 假设纹理左下角是 (0,0)，图片文件左上角是像素 (0,0)
    // 所以 pixelY 需要翻转
    float u0 = (float)pixelX / texW;
    float v0 = 1.0f - (float)(pixelY + height) / texH;  // 翻转 Y
    float u1 = (float)(pixelX + width) / texW;
    float v1 = 1.0f - (float)pixelY / texH;             // 翻转 Y
    
    m_namedSprites[name] = AABB2(Vec2(u0, v0), Vec2(u1, v1));
}

void UISpriteAtlas::DefineSpriteUV(std::string const& name, Vec2 const& uvMins, Vec2 const& uvMaxs)
{
    m_namedSprites[name] = AABB2(uvMins, uvMaxs);
}

AABB2 UISpriteAtlas::GetSpriteUVsByName(std::string const& name) const
{
    auto it = m_namedSprites.find(name);
    if (it != m_namedSprites.end())
    {
        return it->second;
    }
    // 找不到则返回整张纹理
    return AABB2(Vec2::ZERO, Vec2::ONE);
}

bool UISpriteAtlas::HasSprite(std::string const& name) const
{
    return m_namedSprites.find(name) != m_namedSprites.end();
}