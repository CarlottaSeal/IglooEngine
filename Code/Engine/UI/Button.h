#pragma once
#include "UIElement.h"
#include "UIEvent.h"

class Canvas;

class Button : public UIElement
{
public:
    Button();
    Button(Widget* parent, AABB2 bound, Rgba8 hoveredColor = Rgba8::MAGENTA, Rgba8 textColor = Rgba8::TEAL, 
           std::string onClickEvent = "", std::string text = "NO TEXT", Vec2 textAlignment = Vec2(0.5f, 0.5f), 
           std::string texturePath = "");
    
    Button(Canvas* canvas, AABB2 bound, Rgba8 normalColor, Rgba8 hoveredColor, Rgba8 textColor,
         std::string text, Vec2 textAlignment = Vec2(0.5f, 0.5f), std::string texturePath = "", AABB2 uvs = AABB2::ZERO_TO_ONE);
    

    virtual ~Button() override;

    virtual void Update() override;
    virtual void Render() const override;

    bool UpdateIfClicked();
    
    void SetOnClickEvent(std::string const& eventName);
    void OnClick();  // 会同时触发 EventSystem 和 UIEvent

    void SetBackgroundColor(const Rgba8& color); //直接设置背景颜色，覆盖 hovered 效果
    void SetBackgroundColor(Rgba8 const& normal, Rgba8 const& hovered);
    void SetNormalColor(Rgba8 const& color);
    void SetHoveredColor(Rgba8 const& color);
    
    void SetSelected(bool selected);
    bool IsSelected() const { return m_isSelected; }
    void SetSelectedColor(Rgba8 const& color);
    
    size_t OnClickEvent(UICallbackFunctionPointer const& callback);
    
    size_t OnHoverEvent(UICallbackFunctionPointer const& callback);
    size_t OnUnhoverEvent(UICallbackFunctionPointer const& callback);
    
    size_t OnPressedEvent(UICallbackFunctionPointer const& callback);
    
    void RemoveClickListener(size_t id) { m_onClickUIEvent.RemoveListener(id); }
    void RemoveHoverListener(size_t id) { m_onHoverUIEvent.RemoveListener(id); }
    void RemoveUnhoverListener(size_t id) { m_onUnhoverUIEvent.RemoveListener(id); }
    void RemovePressedListener(size_t id) { m_onPressedUIEvent.RemoveListener(id); }

    void UpdateHoveredColor();
    void SetHovered(bool hovered);
    void SetText(std::string text, Vec2 const& alignment);
    void SetBound(AABB2 bound);

public:
    Widget* m_parentWidget;

    Canvas* m_parentCanvas = nullptr;

    AABB2 m_uv;
    
    // 保留原有的 EventSystem 事件名称
    std::string m_onClickEventName;
    
    std::string m_displayText;
    Texture* m_texture;
    Rgba8 m_textColor;
    
    bool m_isSelected = false;
    Rgba8 m_selectedColor = Rgba8(100, 200, 100);  // 选中时的颜色

protected:
    UIEvent m_onClickUIEvent;
    UIEvent m_onHoverUIEvent;
    UIEvent m_onUnhoverUIEvent;
    UIEvent m_onPressedUIEvent;
};
