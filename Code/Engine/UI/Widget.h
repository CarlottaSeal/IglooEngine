#pragma once
#include <vector>
#include "UIElement.h"
#include "UIEvent.h"  
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Math/Vec2.hpp"
#include "Engine/Renderer/Shader.hpp"
#include "Engine/Renderer/Texture.hpp"

class Canvas;
class Timer;

class Widget : public UIElement
{
public:
    Widget();
    Widget(Camera renderCamera, AABB2 bounds, std::string onClickEvent,
        Rgba8 originalColor = Rgba8::WHITE, std::string text = "", AABB2 textBox = AABB2::ZERO_TO_ONE);
    Widget(UIElement* myParent, Camera renderCamera, AABB2 bounds, std::string onClickEvent,
        Rgba8 originalColor = Rgba8::WHITE, std::string text = "", AABB2 textBox = AABB2::ZERO_TO_ONE);
    ~Widget() override;

    // 模式 2（新）：作为复合组件（依附于 Canvas）
    Widget(Canvas* canvas, AABB2 const& bounds, std::string const& name = "");

    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;
    
    void SetEnabled(bool enabled) override;
    Widget& SetBounds(const AABB2& localBounds);
    Widget& SetBounds(const Vec2& mins, const Vec2& maxs);
    Widget& SetText(const char* text, BitmapFont* font, const Rgba8& color = Rgba8::WHITE, float height = 0.f);
    void Reset();
    void AddChild(UIElement& uiElement);

    void SetOnClickEvent(std::string onClickEvent);
    void OnClick();
    void UpdateIfClicked();

    // ===== 新增：UIEvent 接口（可选使用）=====
    // 添加点击事件监听器
    size_t OnClickEvent(UICallbackFunctionPointer const& callback);
    
    // 添加启用事件监听器
    size_t OnEnabledEvent(UICallbackFunctionPointer const& callback);
    
    // 添加禁用事件监听器
    size_t OnDisabledEvent(UICallbackFunctionPointer const& callback);
    
    // 移除事件监听器
    void RemoveClickListener(size_t id) { m_onClickUIEvent.RemoveListener(id); }
    void RemoveEnabledListener(size_t id) { m_onEnabledUIEvent.RemoveListener(id); }
    void RemoveDisabledListener(size_t id) { m_onDisabledUIEvent.RemoveListener(id); }

    void Build();

    // 子元素管理
    void AddChildWidget(UIElement* child);
    void RemoveChildWidget(UIElement* child);

    bool IsStandaloneMode() const { return m_isStandaloneMode; }
    bool IsComponentMode() const { return !m_isStandaloneMode; }
    
private:
    void InitializeComponentMode(Canvas* canvas);

public:
    // General
    std::string m_name;
    AABB2 m_bounds = AABB2::ZERO_TO_ONE;
    AABB2 m_textBox = AABB2::ZERO_TO_ONE;

    Camera m_renderCamera;
    std::string m_onClickEvent;
    Timer* m_popTextTimer;

    // Background and border
    bool m_hasBackground = false;
    Rgba8 m_backgroundColor = Rgba8::BLACK;
    Rgba8 m_backgroundBorderColor = Rgba8::WHITE;
    float m_backgroundBorderWidth = 0.0f;
    std::vector<Vertex_PCU> m_backgroundVerts;

    // Textures
    bool m_hasTexture = false;
    Texture* m_texture = nullptr;
    Shader* m_shader = nullptr;
    Rgba8 m_textureColor = Rgba8::WHITE;
    std::vector<Vertex_PCU> m_textureVerts;

    // Text
    bool m_hasText = false;
    std::string m_text;
    BitmapFont* m_font = nullptr;
    Rgba8 m_textColor = Rgba8::WHITE;
    float m_textHeight = 1.0f;
    float m_textAspect = 1.0f;
    Vec2 m_textAlignment = Vec2::ZERO;
    std::vector<Vertex_PCU> m_textVerts;

protected:
    bool m_isStandaloneMode = false;
    Canvas* m_parentCanvas = nullptr; 
    UIEvent m_onClickUIEvent;
    UIEvent m_onEnabledUIEvent;
    UIEvent m_onDisabledUIEvent;
};