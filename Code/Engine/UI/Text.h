#pragma once
#include "UIElement.h"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Math/Vec2.hpp"
#include <string>

class Canvas;
class BitmapFont;

struct TextSetting
{
    TextSetting() = default;
    TextSetting(std::string const& text, float height = 20.f, 
                Rgba8 color = Rgba8::WHITE, Vec2 alignment = Vec2(0.5f, 0.5f))
        : m_text(text), m_height(height), m_color(color), m_alignment(alignment) {}
    
    std::string m_text;
    float m_height = 20.f;
    Rgba8 m_color = Rgba8::WHITE;
    Vec2 m_alignment = Vec2(0.5f, 0.5f);
};

class Text : public UIElement
{
public:
    Text(Canvas* canvas, Vec2 const& position, TextSetting const& setting);
    virtual ~Text();

    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;

    void SetText(std::string const& text);
    void SetPosition(Vec2 const& position);
    void SetHeight(float height);
    void SetColor(Rgba8 color);

    Rgba8 const GetColor() const;

    std::string GetText() const { return m_textSetting.m_text; }

protected:
    void UpdateTextVerts();
    
    Canvas* m_canvas = nullptr;
    BitmapFont* m_font = nullptr;
    
    Vec2 m_position;
    TextSetting m_textSetting;
};