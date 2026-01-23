#pragma once
#include "UIElement.h"

class SpriteSheet;
class UISpriteAtlas;
class Canvas;
class Texture;

class Sprite : public UIElement
{
public:
    Sprite(Canvas* canvas, AABB2 const& bounds, Texture* texture = nullptr, AABB2 const& uv = AABB2::ZERO_TO_ONE);
    virtual ~Sprite();

    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;

    void SetTexture(Texture* texture);
    void SetBounds(AABB2 const& bounds);
    void SetColor(Rgba8 color);

    void SetUVs(Vec2 const& uvMins, Vec2 const& uvMaxs);
    void SetUVs(AABB2 const& uvs);
    // 方式2：使用精灵表 + 名称
    void SetSprite(UISpriteAtlas* atlas, std::string const& spriteName);
    // 方式3：使用精灵表 + 索引（兼容原有 SpriteSheet）
    void SetSprite(SpriteSheet* spriteSheet, int spriteIndex);
    
    void ResetUVs();

protected:
    Canvas* m_canvas = nullptr;
    Texture* m_texture = nullptr;

    Vec2 m_uvMins = Vec2(0.f, 0.f);
    Vec2 m_uvMaxs = Vec2(1.f, 1.f);
    
    void InitializeVerts();
};