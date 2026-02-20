#pragma once
#include "UIElement.h"

class Canvas;

class Panel : public UIElement
{
public:
    Panel(Canvas* canvas, AABB2 const& bounds, 
          Rgba8 backgroundColor = Rgba8::GREY,
          Texture* texture = nullptr,
          bool hasBorder = false,
          Rgba8 borderColor = Rgba8::BLACK, AABB2 const& uv = AABB2::ZERO_TO_ONE);

    void InitializeVerts();
    
    virtual ~Panel();

    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;

    void SetBounds(AABB2 const& bounds);
    void SetBackgroundColor(Rgba8 color);
    void SetTexture(Texture* texture);
    void SetBorder(bool hasBorder, Rgba8 borderColor = Rgba8::BLACK);

protected:
    Canvas* m_canvas = nullptr;
    AABB2 m_uv;
    Texture* m_texture = nullptr;
    bool m_hasBorder = false;
    Rgba8 m_backgroundColor = Rgba8::GREY;
    Rgba8 m_borderColor = Rgba8::BLACK;
    
    std::vector<Vertex_PCU> m_backgroundVerts;
    std::vector<Vertex_PCU> m_borderVerts;
};