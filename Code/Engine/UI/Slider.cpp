#include "Slider.h"
#include "Canvas.hpp"
#include "UIEvent.h"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Math/MathUtils.hpp"

Slider::Slider(Canvas* canvas, AABB2 const& trackBounds,
               float minValue, float maxValue, float currentValue,
               Rgba8 trackColor, Rgba8 handleColor, Rgba8 fillColor)
    : m_canvas(canvas)
    , m_trackBounds(trackBounds)
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_currentValue(currentValue)
    , m_trackColor(trackColor)
    , m_handleColor(handleColor)
    , m_fillColor(fillColor)
{
    m_bound = trackBounds;
    m_type = SLIDER;
    
    m_handleSize = trackBounds.GetHeight() * 1.5f;
    
    if (m_canvas)
    {
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

Slider::~Slider()
{
    ShutDown();
}

void Slider::StartUp()
{
    UpdateHandlePosition();
    UpdateVerts();
}

void Slider::ShutDown()
{
    m_trackVerts.clear();
    m_fillVerts.clear();
    m_handleVerts.clear();
    
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

void Slider::Update()
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

void Slider::Render() const
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
    
    if (!m_trackVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_trackVerts.size(), m_trackVerts.data());
    }
    if (!m_fillVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_fillVerts.size(), m_fillVerts.data());
    }
    
    if (!m_handleVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_handleVerts.size(), m_handleVerts.data());
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

void Slider::SetValue(float value)
{
    float newValue = GetClamped(value, m_minValue, m_maxValue);

    m_currentValue = newValue;
    UpdateHandlePosition();
    UpdateVerts();
    m_onValueChangedEvent.Invoke();
}

void Slider::SetValueNormalized(float normalizedValue)
{
    float value = RangeMapClamped(normalizedValue, 0.0f, 1.0f, m_minValue, m_maxValue);
    SetValue(value);
}

float Slider::GetValueNormalized() const
{
    if (m_maxValue == m_minValue)
    {
        return 0.0f;
    }
    return (m_currentValue - m_minValue) / (m_maxValue - m_minValue);
}

void Slider::SetMinValue(float minValue)
{
    m_minValue = minValue;
    SetValue(m_currentValue);
}

void Slider::SetMaxValue(float maxValue)
{
    m_maxValue = maxValue;
    SetValue(m_currentValue);
}

void Slider::SetTrackColor(Rgba8 color)
{
    m_trackColor = color;
    UpdateVerts();
}

void Slider::SetHandleColor(Rgba8 color)
{
    m_handleColor = color;
    UpdateVerts();
}

void Slider::SetFillColor(Rgba8 color)
{
    m_fillColor = color;
    UpdateVerts();
}

void Slider::SetHandleSize(float size)
{
    m_handleSize = size;
    UpdateHandlePosition();
    UpdateVerts();
}

size_t Slider::OnValueChangedEvent(UICallbackFunctionPointer callback)
{
    return m_onValueChangedEvent.AddListener(callback);
}

void Slider::RemoveValueChangedListener(size_t id)
{
    m_onValueChangedEvent.RemoveListener(id);
}

void Slider::UpdateVerts()
{
    m_trackVerts.clear();
    AddVertsForAABB2D(m_trackVerts, m_trackBounds, m_trackColor);
    
    m_fillVerts.clear();
    float normalized = GetValueNormalized();
    float fillWidth = (m_trackBounds.m_maxs.x - m_trackBounds.m_mins.x) * normalized;
    AABB2 fillBounds(
        m_trackBounds.m_mins.x,
        m_trackBounds.m_mins.y,
        m_trackBounds.m_mins.x + fillWidth,
        m_trackBounds.m_maxs.y
    );
    AddVertsForAABB2D(m_fillVerts, fillBounds, m_fillColor);
    
    m_handleVerts.clear();
    Vec2 center = GetHandleCenter();
    
    AddVertsForDisc2D(m_handleVerts, center, m_handleSize * 0.5f, m_handleColor, 16);
    
    // 或者使用方形滑块（注释掉圆形，使用这个）
    // m_handleBounds = AABB2::MakeFromCenter(center, m_handleSize * 0.5f, m_handleSize * 0.5f);
    // AddVertsForAABB2D(m_handleVerts, m_handleBounds, m_handleColor);
}

void Slider::UpdateHandlePosition()
{
    Vec2 center = GetHandleCenter();
    Vec2 mins = center - Vec2(m_handleSize * 0.5f, m_handleSize * 0.5f);
    Vec2 maxs = center + Vec2(m_handleSize * 0.5f, m_handleSize * 0.5f);    
    m_handleBounds = AABB2(mins, maxs);
}

Vec2 Slider::GetHandleCenter() const
{
    float normalized = GetValueNormalized();
    float x = RangeMap(normalized, 0.0f, 1.0f, 
                       m_trackBounds.m_mins.x, m_trackBounds.m_maxs.x);
    float y = m_trackBounds.GetCenter().y;
    return Vec2(x, y);
}

void Slider::HandleInput()
{
    if (!m_canvas)
    {
        return;
    }
    
    InputSystem* input = m_canvas->GetSystemInputSystem();
    if (!input) {
        return;
    }
    
    Vec2 mousePos = input->GetCursorClientPosition();
    
    // 检查是否点击滑块
    if (input->WasKeyJustPressed(KEYCODE_LEFT_MOUSE))
    {
        if (m_handleBounds.IsPointInside(mousePos))
        {
            m_isDragging = true;
        }
    }
    
    if (input->WasKeyJustPressed(KEYCODE_RIGHT_MOUSE))
    {
        m_isDragging = false;
    }
    
    if (m_isDragging)
    {
        float trackWidth = m_trackBounds.m_maxs.x - m_trackBounds.m_mins.x;
        float mouseX = GetClamped(mousePos.x, m_trackBounds.m_mins.x, m_trackBounds.m_maxs.x);
        float normalized = (mouseX - m_trackBounds.m_mins.x) / trackWidth;
        SetValueNormalized(normalized);
    }
    
    // 点击轨道直接跳转
    if (input->WasKeyJustPressed(KEYCODE_LEFT_MOUSE) && !m_isDragging)
    {
        if (m_trackBounds.IsPointInside(mousePos))
        {
            float trackWidth = m_trackBounds.m_maxs.x - m_trackBounds.m_mins.x;
            float normalized = (mousePos.x - m_trackBounds.m_mins.x) / trackWidth;
            SetValueNormalized(normalized);
        }
    }
}