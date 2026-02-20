#include "Checkbox.h"
#include "Canvas.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Input/InputSystem.hpp"

Checkbox::Checkbox(Canvas* canvas, AABB2 const& bounds,
                   bool isChecked, Rgba8 uncheckedColor,
                   Rgba8 checkedColor, Rgba8 hoverColor)
    : m_canvas(canvas)
    , m_isChecked(isChecked)
    , m_uncheckedColor(uncheckedColor)
    , m_checkedColor(checkedColor)
{
    m_bound = bounds;
    m_type = CHECKBOX;
    m_originalColor = uncheckedColor;
    m_hoveredColor = hoverColor;
    m_renderedColor = isChecked ? checkedColor : uncheckedColor;
    
    if (m_canvas)
    {
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

Checkbox::~Checkbox()
{
    ShutDown();
}

void Checkbox::StartUp()
{
    UpdateVerts();
}

void Checkbox::ShutDown()
{
    m_boxVerts.clear();
    m_checkVerts.clear();
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->ShutDown();
            delete child;
        }
    }
    m_children.clear();
}

void Checkbox::Update()
{
    if (!IsEnabled())
    {
        return;
    }
    
    HandleInput();
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Update();
        }
    }
}

void Checkbox::Render() const
{
    if (!IsEnabled())
    {
        return;
    }
    
    if (!m_canvas)
    {
        return;
    }
    
    Renderer* renderer = m_canvas->GetSystemRenderer();
    Camera* camera = m_canvas->GetCamera();
    
    renderer->BeginCamera(*camera);
    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
    renderer->SetBlendMode(BlendMode::ALPHA);
    
    if (!m_boxVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_boxVerts.size(), m_boxVerts.data());
    }
    
    if (m_isChecked && !m_checkVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_checkVerts.size(), m_checkVerts.data());
    }
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Render();
        }
    }
    
    renderer->EndCamera(*camera);
}

void Checkbox::SetChecked(bool checked)
{
    if (m_isChecked != checked)
    {
        m_isChecked = checked;
        m_renderedColor = m_isChecked ? m_checkedColor : m_uncheckedColor;
        UpdateVerts();
        m_onCheckChangedEvent.Invoke();
    }
}

void Checkbox::Toggle()
{
    SetChecked(!m_isChecked);
}

void Checkbox::SetUncheckedColor(Rgba8 color)
{
    m_uncheckedColor = color;
    if (!m_isChecked) {
        m_originalColor = color;
        m_renderedColor = color;
        UpdateVerts();
    }
}

void Checkbox::SetCheckedColor(Rgba8 color)
{
    m_checkedColor = color;
    if (m_isChecked) {
        m_renderedColor = color;
        UpdateVerts();
    }
}

void Checkbox::SetHoverColor(Rgba8 color)
{
    m_hoveredColor = color;
}

size_t Checkbox::OnCheckChangedEvent(UICallbackFunctionPointer callback)
{
    return m_onCheckChangedEvent.AddListener(callback);
}

void Checkbox::RemoveCheckChangedListener(size_t id) {
    m_onCheckChangedEvent.RemoveListener(id);
}

void Checkbox::UpdateVerts()
{
    m_boxVerts.clear();
    AddVertsForAABB2D(m_boxVerts, m_bound, m_renderedColor);
    
    // 创建勾选标记（X 或 勾）
    if (m_isChecked)
    {
        m_checkVerts.clear();
        
        // 在框内画一个 X
        float padding = m_bound.GetWidth() * 0.2f;
        Vec2 topLeft(m_bound.m_mins.x + padding, m_bound.m_maxs.y - padding);
        Vec2 topRight(m_bound.m_maxs.x - padding, m_bound.m_maxs.y - padding);
        Vec2 bottomLeft(m_bound.m_mins.x + padding, m_bound.m_mins.y + padding);
        Vec2 bottomRight(m_bound.m_maxs.x - padding, m_bound.m_mins.y + padding);
        
        // 对角线 1
        AddVertsForLineSegment2D(m_checkVerts, topLeft, bottomRight, 3.0f, Rgba8::BLACK);
        // 对角线 2
        AddVertsForLineSegment2D(m_checkVerts, topRight, bottomLeft, 3.0f, Rgba8::BLACK);
    }
}

void Checkbox::HandleInput()
{
    if (!m_canvas)
    {
        return;
    }
    
    InputSystem* input = m_canvas->GetSystemInputSystem();
    if (!input)
    {
        return;
    }
    
    Vec2 mousePos = input->GetCursorClientPosition();
    bool isHovered = m_bound.IsPointInside(mousePos);
    
    if (isHovered != m_wasHoveredLastFrame)
    {
        if (isHovered)
        {
            m_renderedColor = m_hoveredColor;
        }
        else
        {
            m_renderedColor = m_isChecked ? m_checkedColor : m_uncheckedColor;
        }
        UpdateVerts();
        m_wasHoveredLastFrame = isHovered;
    }
    
    if (isHovered && input->WasKeyJustPressed(KEYCODE_LEFT_MOUSE))
    {
        Toggle();
    }
}