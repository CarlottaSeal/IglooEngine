#include "Text.h"
#include "Canvas.hpp"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/BitmapFont.hpp"

Text::Text(Canvas* canvas, Vec2 const& position, TextSetting const& setting)
    : m_canvas(canvas)
    , m_position(position)
    , m_textSetting(setting)
{
    
    m_type = TEXT;
    
    if (m_canvas)
    {
        m_font = m_canvas->GetSystemFont();
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

Text::~Text()
{
    ShutDown();
}

void Text::StartUp()
{
    UpdateTextVerts();
}

void Text::ShutDown()
{
    m_textVerts.clear();
    
    // 清理子元素
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

void Text::Update()
{
    if (!IsEnabled())
    {
        return;
    }
    
    // 更新子元素
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Update();
        }
    }
}

void Text::Render() const
{
    if (!IsEnabled())
    {
        return;
    }
    
    if (!m_canvas || !m_font)
    {
        return;
    }
    
    if (m_textVerts.empty())
    {
        return;
    }
    
    Renderer* renderer = m_canvas->GetSystemRenderer();
    Camera* camera = m_canvas->GetCamera();
    
    renderer->BeginCamera(*camera);
    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
    renderer->SetBlendMode(BlendMode::ALPHA);
    
    renderer->BindTexture(&m_font->GetTexture());
    renderer->DrawVertexArray((int)m_textVerts.size(), m_textVerts.data());
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Render();
        }
    }
    
    renderer->EndCamera(*camera);
}

void Text::SetText(std::string const& text)
{
    if (m_textSetting.m_text != text)
    {
        m_textSetting.m_text = text;
        UpdateTextVerts();
    }
}

void Text::SetPosition(Vec2 const& position)
{
    if (m_position != position)
    {
        m_position = position;
        UpdateTextVerts();
    }
}

void Text::SetHeight(float height)
{
    if (m_textSetting.m_height != height)
    {
        m_textSetting.m_height = height;
        UpdateTextVerts();
    }
}

void Text::SetColor(Rgba8 color)
{
    if (m_textSetting.m_color != color)
    {
        m_textSetting.m_color = color;
        UpdateTextVerts();
    }
}

Rgba8 const Text::GetColor() const
{
    return m_textSetting.m_color;
}
void Text::UpdateTextVerts()
{
    m_textVerts.clear();
    
    if (!m_font || m_textSetting.m_text.empty())
    {
        return;
    }
    
    std::vector<Vertex_PCU> tempVerts;
    m_font->AddVertsForText2D(
        tempVerts,
        Vec2(0.0f, 0.0f),  
        m_textSetting.m_height,
        m_textSetting.m_text,
        m_textSetting.m_color,
        1.0f
    );
    
    if (tempVerts.empty())
    {
        return;
    }
    
    Vec2 textMins(999999.0f, 999999.0f);
    Vec2 textMaxs(-999999.0f, -999999.0f);
    
    for (const auto& vert : tempVerts)
    {
        if (vert.m_position.x < textMins.x) textMins.x = vert.m_position.x;
        if (vert.m_position.y < textMins.y) textMins.y = vert.m_position.y;
        if (vert.m_position.x > textMaxs.x) textMaxs.x = vert.m_position.x;
        if (vert.m_position.y > textMaxs.y) textMaxs.y = vert.m_position.y;
    }
    
    Vec2 textSize = textMaxs - textMins;
    
    Vec2 alignmentOffset = -textSize * m_textSetting.m_alignment;
    
    m_textVerts.reserve(tempVerts.size());
    for (const auto& vert : tempVerts)
    {
        Vertex_PCU newVert = vert;
        newVert.m_position.x += m_position.x + alignmentOffset.x;
        newVert.m_position.y += m_position.y + alignmentOffset.y;
        m_textVerts.push_back(newVert);
    }
}