#include "Panel.h"
#include "Canvas.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"

Panel::Panel(Canvas* canvas, AABB2 const& bounds, 
             Rgba8 backgroundColor, Texture* texture,
             bool hasBorder, Rgba8 borderColor, AABB2 const& uv)
    : m_canvas(canvas)
    , m_backgroundColor(backgroundColor)
    , m_texture(texture)
    , m_hasBorder(hasBorder)
    , m_borderColor(borderColor)
    , m_uv(uv)
{
    m_bound = bounds;
    m_type = PANEL;
    m_originalColor = backgroundColor;
    m_renderedColor = backgroundColor;
    
    if (m_canvas)
    {
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

void Panel::InitializeVerts()
{
    m_backgroundVerts.clear();
    AddVertsForAABB2D(m_backgroundVerts, m_bound, m_backgroundColor, m_uv);
    
    if (m_hasBorder)
    {
        m_borderVerts.clear();
        
        float borderWidth = 2.0f;
        
        AABB2 top(m_bound.m_mins.x, m_bound.m_maxs.y - borderWidth,
                  m_bound.m_maxs.x, m_bound.m_maxs.y);
        AABB2 bottom(m_bound.m_mins.x, m_bound.m_mins.y,
                     m_bound.m_maxs.x, m_bound.m_mins.y + borderWidth);
        AABB2 left(m_bound.m_mins.x, m_bound.m_mins.y,
                   m_bound.m_mins.x + borderWidth, m_bound.m_maxs.y);
        AABB2 right(m_bound.m_maxs.x - borderWidth, m_bound.m_mins.y,
                    m_bound.m_maxs.x, m_bound.m_maxs.y);
        AddVertsForAABB2D(m_borderVerts, top, m_borderColor);
        AddVertsForAABB2D(m_borderVerts, bottom, m_borderColor);
        AddVertsForAABB2D(m_borderVerts, left, m_borderColor);
        AddVertsForAABB2D(m_borderVerts, right, m_borderColor);
    }
}

Panel::~Panel()
{
    ShutDown();
}

void Panel::StartUp()
{
    InitializeVerts();
}

void Panel::ShutDown()
{
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

void Panel::Update()
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

void Panel::Render() const
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
        //renderer->BindTexture(m_texture);
        renderer->DrawVertexArray((int)m_backgroundVerts.size(), m_backgroundVerts.data());
    }
    

    if (m_hasBorder && !m_borderVerts.empty())
    {
        //renderer->BindTexture(nullptr);
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

void Panel::SetBounds(AABB2 const& bounds)
{
    m_bound = bounds;
    InitializeVerts();
}

void Panel::SetBackgroundColor(Rgba8 color)
{
    m_backgroundColor = color;
    m_originalColor = color;
    m_renderedColor = color;
    InitializeVerts();
}

void Panel::SetTexture(Texture* texture)
{
    m_texture = texture;
}

void Panel::SetBorder(bool hasBorder, Rgba8 borderColor)
{
    m_hasBorder = hasBorder;
    m_borderColor = borderColor;
    InitializeVerts();
}