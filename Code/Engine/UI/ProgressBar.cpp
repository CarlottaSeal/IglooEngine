#include "ProgressBar.h"
#include "Canvas.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Math/MathUtils.hpp"

ProgressBar::ProgressBar(Canvas* canvas, AABB2 const& bounds,
                         float minValue, float maxValue,
                         Rgba8 backgroundColor, Rgba8 fillColor,
                         Rgba8 borderColor, bool hasBorder)
    : m_canvas(canvas)
    , m_minValue(minValue)
    , m_maxValue(maxValue)
    , m_backgroundColor(backgroundColor)
    , m_fillColor(fillColor)
    , m_borderColor(borderColor)
    , m_hasBorder(hasBorder)
{
    m_bound = bounds;
    m_type = PROGRESS_BAR;
    m_currentValue = minValue;
    
    if (m_canvas)
    {
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

ProgressBar::~ProgressBar()
{
    ShutDown();
}

void ProgressBar::StartUp()
{
    UpdateVerts();
}

void ProgressBar::ShutDown()
{
    m_backgroundVerts.clear();
    m_fillVerts.clear();
    m_borderVerts.clear();
    
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

void ProgressBar::Update()
{
    if (!IsEnabled())
    {
        return;
    }
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Update();
        }
    }
}

void ProgressBar::Render() const
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
    
    if (!m_backgroundVerts.empty())
    {
        //renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_backgroundVerts.size(), m_backgroundVerts.data());
    }
    
    if (!m_fillVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_fillVerts.size(), m_fillVerts.data());
    }
    
    if (m_hasBorder && !m_borderVerts.empty())
    {
        renderer->BindTexture(nullptr);
        renderer->DrawVertexArray((int)m_borderVerts.size(), m_borderVerts.data());
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

void ProgressBar::SetBounds(AABB2 const& bounds)
{
    m_bound = bounds;
    UpdateVerts();
}

void ProgressBar::SetValue(float value)
{
    float newValue = GetClamped(value, m_minValue, m_maxValue);

    m_currentValue = newValue;
    UpdateFillVerts();
}

void ProgressBar::SetValueNormalized(float normalizedValue)
{
    float value = RangeMapClamped(normalizedValue, 0.0f, 1.0f, m_minValue, m_maxValue);
    SetValue(value);
}

void ProgressBar::SetMinValue(float minValue)
{
    m_minValue = minValue;
    SetValue(m_currentValue);
}

void ProgressBar::SetMaxValue(float maxValue)
{
    m_maxValue = maxValue;
    SetValue(m_currentValue);
}

void ProgressBar::SetBackgroundColor(Rgba8 color)
{
    if (m_backgroundColor != color)
    {
        m_backgroundColor = color;
        UpdateVerts();
    }
}

void ProgressBar::SetFillColor(Rgba8 color)
{
    if (m_fillColor != color)
    {
        m_fillColor = color;
        UpdateFillVerts();
    }
}

void ProgressBar::SetBorderColor(Rgba8 color)
{
    if (m_borderColor != color)
    {
        m_borderColor = color;
        UpdateVerts();
    }
}

void ProgressBar::SetOrientation(ProgressBarOrientation orientation)
{
    if (m_orientation != orientation)
    {
        m_orientation = orientation;
        UpdateFillVerts();
    }
}

float ProgressBar::GetValueNormalized() const
{
    if (m_maxValue == m_minValue)
    {
        return 0.0f;
    }
    return (m_currentValue - m_minValue) / (m_maxValue - m_minValue);
}

void ProgressBar::UpdateVerts()
{
    // 创建背景
    m_backgroundVerts.clear();
    AddVertsForAABB2D(m_backgroundVerts, m_bound, m_backgroundColor);
    
    // // 创建边框 TODO
    // if (m_hasBorder) {
    //     m_borderVerts.clear();
    //     AddVertsForAABB2DOutline(m_borderVerts, m_bound, m_borderColor, 2.0f);
    // }
    
    UpdateFillVerts();
}

void ProgressBar::UpdateFillVerts()
{
    m_fillVerts.clear();
    
    float normalized = GetValueNormalized();
    
    if (normalized <= 0.0f)
    {
        return;
    }
    
    AABB2 fillBound;
    
    if (m_orientation == ProgressBarOrientation::HORIZONTAL)
    {
        // 水平（从左到右）
        float fillWidth = (m_bound.m_maxs.x - m_bound.m_mins.x) * normalized;
        fillBound = AABB2(
            m_bound.m_mins.x,
            m_bound.m_mins.y,
            m_bound.m_mins.x + fillWidth,
            m_bound.m_maxs.y
        );
    }
    else
    {
        // 垂直（从下到上）
        float fillHeight = (m_bound.m_maxs.y - m_bound.m_mins.y) * normalized;
        fillBound = AABB2(
            m_bound.m_mins.x,
            m_bound.m_mins.y,
            m_bound.m_maxs.x,
            m_bound.m_mins.y + fillHeight
        );
    }
    
    AddVertsForAABB2D(m_fillVerts, fillBound, m_fillColor);
}