#include "Button.h"

#include "Canvas.hpp"
#include "Widget.h"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/EventSystem.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Renderer/BitmapFont.hpp"

extern Renderer* g_theRenderer;

Button::Button()
{
}

Button::Button(Widget* parent, AABB2 bound, Rgba8 hoveredColor, Rgba8 textColor, std::string onClickEvent, 
               std::string text, Vec2 textAlignment, std::string texturePath)
{
    m_type = BUTTON;
    m_parentWidget = parent;
    m_bound = bound;
    m_textColor = textColor;
    m_hoveredColor = hoveredColor;
    
    // 保持原有的 EventSystem 接口
    SetOnClickEvent(onClickEvent);

    AddVertsForAABB2D(m_verts, bound, m_originalColor);
    g_theUISystem->GetBitmapFont()->AddVertsForTextInBox2D(m_textVerts, text, bound, bound.GetHeight()*0.25f,
        m_textColor, 0.7f, textAlignment, TextBoxMode::OVERRUN);

    if (texturePath != "")
        m_texture = g_theRenderer->CreateOrGetTextureFromFile(texturePath.c_str());
}

Button::Button(Canvas* canvas, AABB2 bound, Rgba8 normalColor, Rgba8 hoveredColor, Rgba8 textColor,
               std::string text, Vec2 textAlignment, std::string texturePath, AABB2 uvs)
{
    m_type = BUTTON;
    m_uv = uvs;
    m_parentCanvas = canvas;
    m_bound = bound;
    m_originalColor = normalColor;
    m_renderedColor = normalColor;
    m_hoveredColor = hoveredColor;
    m_textColor = textColor;
    m_displayText = text;
    
    AddVertsForAABB2D(m_verts, bound, m_renderedColor);
    
    g_theUISystem->GetBitmapFont()->AddVertsForTextInBox2D(m_textVerts, text, bound, bound.GetHeight()*0.25f,
        m_textColor, 0.7f, textAlignment, TextBoxMode::OVERRUN);
    
    if (texturePath != "")
        m_texture = g_theRenderer->CreateOrGetTextureFromFile(texturePath.c_str());

    if (m_parentCanvas)
    {
        m_parentCanvas->AddElementToCanvas(this);
    }
}

Button::~Button()
{
    // UIEvent 会自动清理监听器
}

void Button::Update()
{
    bool parentEnabled = true;
    if (m_parentWidget)
    {
        parentEnabled = m_parentWidget->IsEnabled();
    }
    else if (m_parentCanvas)
    {
        parentEnabled = m_parentCanvas->IsEnabled();
    }
    else if (m_parent)
    {
        parentEnabled = m_parent->IsEnabled();
    }
    if (parentEnabled)
    {
        UpdateIfClicked();
        UpdateHoveredColor();
    }
}

void Button::Render() const
{
    bool parentEnabled = true;
    Camera* renderCamera = nullptr;
    if (m_parentWidget)
    {
        parentEnabled = m_parentWidget->IsEnabled();
        renderCamera = &m_parentWidget->m_renderCamera;
    }
    else if (m_parentCanvas)
    {
        parentEnabled = m_parentCanvas->IsEnabled();
        renderCamera = m_parentCanvas->GetCamera();
    }
    else if (m_parent)
    {
        parentEnabled = m_parent->IsEnabled();
    }
    if (parentEnabled && renderCamera)
    {
        g_theUISystem->GetRenderer()->BeginCamera(*renderCamera);
        g_theUISystem->GetRenderer()->SetBlendMode(BlendMode::ALPHA);
        g_theUISystem->GetRenderer()->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
        g_theUISystem->GetRenderer()->SetDepthMode(DepthMode::DISABLED);
        g_theUISystem->GetRenderer()->BindShader(nullptr);
#ifdef ENGINE_DX11_RENDERER
        g_theUISystem->GetRenderer()->BindTexture(m_texture); 
#endif
#ifdef ENGINE_DX12_RENDERER
        g_theUISystem->GetRenderer()->SetMaterialConstants(m_texture);
#endif
        g_theUISystem->GetRenderer()->SetModelConstants(Mat44(), m_renderedColor);
        g_theUISystem->GetRenderer()->DrawVertexArray(m_verts);
        g_theUISystem->GetRenderer()->SetModelConstants();
        g_theUISystem->GetRenderer()->BindTexture(&g_theUISystem->GetBitmapFont()->GetTexture());
        g_theUISystem->GetRenderer()->DrawVertexArray(m_textVerts);
    }
}

bool Button::UpdateIfClicked()
{
    bool parentEnabled = true;
    Camera* renderCamera = nullptr;
    if (m_parentWidget)
    {
        parentEnabled = m_parentWidget->IsEnabled();
        renderCamera = &m_parentWidget->m_renderCamera;
    }
    else if (m_parentCanvas)
    {
        parentEnabled = m_parentCanvas->IsEnabled();
        renderCamera = m_parentCanvas->GetCamera();
    }
    else if (m_parent)
    {
        parentEnabled = m_parent->IsEnabled();
    }
    if (parentEnabled && renderCamera)
    {
        Vec2 mousePos = renderCamera->GetOrthographicBounds().GetPointAtUV(
            g_theUISystem->GetInputSystem()->GetCursorNormalizedPosition());
        
        if (IsPointInsideAABB2D(mousePos, m_bound))
        {
            if (!m_isHovered)
            {
                SetHovered(true);
                m_onHoverUIEvent.Invoke();
            }
            if (g_theUISystem->GetInputSystem()->IsKeyDown(KEYCODE_LEFT_MOUSE))
            {
                m_onPressedUIEvent.Invoke();
            }
            if (g_theUISystem->GetInputSystem()->WasKeyJustReleased(KEYCODE_LEFT_MOUSE))
            {
                OnClick();
                return true;
            }
        }
        else
        {
            if (m_isHovered)
            {
                m_onUnhoverUIEvent.Invoke();
            }
            SetHovered(false);
            return false;
        }
    }
    return false;
}

void Button::SetBackgroundColor(const Rgba8& color)
{
    m_originalColor = color;
    m_renderedColor = color;
    
    // 自动生成悬停色（亮一点）
    m_hoveredColor = Rgba8(
        (unsigned char)MinI(255, (int)color.r + 40),
        (unsigned char)MinI(255, (int)color.g + 40),
        (unsigned char)MinI(255, (int)color.b + 40),
        color.a
    );
    
    m_verts.clear();
    AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
}

void Button::SetBackgroundColor(Rgba8 const& normal, Rgba8 const& hovered)
{
    m_originalColor = normal;
    m_hoveredColor = hovered;
    m_renderedColor = normal;
    
    m_verts.clear();
    AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
}

void Button::SetNormalColor(Rgba8 const& color)
{
    m_originalColor = color;
    if (!m_isHovered && !m_isSelected)
    {
        m_renderedColor = color;
        m_verts.clear();
        AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
    }
}

void Button::SetHoveredColor(Rgba8 const& color)
{
    m_hoveredColor = color;
    if (m_isHovered)
    {
        m_renderedColor = color;
        m_verts.clear();
        AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
    }
}

void Button::SetSelected(bool selected)
{
    m_isSelected = selected;
    
    if (m_isSelected)
    {
        m_renderedColor = m_selectedColor;
    }
    else
    {
        m_renderedColor = m_isHovered ? m_hoveredColor : m_originalColor;
    }
    m_verts.clear();
    AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
}

void Button::SetSelectedColor(Rgba8 const& color)
{
    m_selectedColor = color;
    if (m_isSelected)
    {
        m_renderedColor = color;
        m_verts.clear();
        AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
    }
}

void Button::SetOnClickEvent(std::string const& eventName)
{
    m_onClickEventName = eventName;
}

void Button::OnClick()
{
    // 触发原有的 EventSystem（向后兼容）
    if (!m_onClickEventName.empty())
    {
        EventArgs eventArgs;
        eventArgs.SetValue("value", m_onClickEventName);
        g_theEventSystem->FireEvent(m_onClickEventName, eventArgs);
    }
    // 触发新的 UIEvent
    m_onClickUIEvent.Invoke();
}

void Button::UpdateHoveredColor()
{
    if (m_isSelected)
    {
        m_renderedColor = m_selectedColor;
    }
    else if (m_isHovered)
    {
        m_renderedColor = m_hoveredColor;
    }
    else
    {
        m_renderedColor = m_originalColor;
    }
    m_verts.clear();
    AddVertsForAABB2D(m_verts, m_bound, m_renderedColor);
}

void Button::SetHovered(bool hovered)
{
    m_isHovered = hovered;
}

void Button::SetText(std::string text, Vec2 const& alignment)
{
    UNUSED(alignment);
    m_displayText = text;
}

void Button::SetBound(AABB2 bound)
{
    m_bound = bound;
}

size_t Button::OnClickEvent(UICallbackFunctionPointer const& callback)
{
    return m_onClickUIEvent.AddListener(callback);
}

size_t Button::OnHoverEvent(UICallbackFunctionPointer const& callback)
{
    return m_onHoverUIEvent.AddListener(callback);
}

size_t Button::OnUnhoverEvent(UICallbackFunctionPointer const& callback)
{
    return m_onUnhoverUIEvent.AddListener(callback);
}

size_t Button::OnPressedEvent(UICallbackFunctionPointer const& callback)
{
    return m_onPressedUIEvent.AddListener(callback);
}