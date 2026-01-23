#include "Sprite.h"
#include "Canvas.hpp"
#include "UISpriteAtlas.h"
#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/SpriteSheet.hpp"
#include "Engine/Renderer/Texture.hpp"

Sprite::Sprite(Canvas* canvas, AABB2 const& bounds, Texture* texture, AABB2 const& uv)
    : m_canvas(canvas)
    , m_texture(texture)
{
    m_uvMaxs = uv.m_maxs;
    m_uvMins = uv.m_mins;
    m_bound = bounds;
    m_type = SPRITE;
    
    if (m_canvas)
    {
        m_canvas->AddElementToCanvas(this);
    }
    
    StartUp();
}

Sprite::~Sprite()
{
    ShutDown();
}

void Sprite::StartUp()
{
    InitializeVerts();
}

void Sprite::ShutDown()
{
    m_verts.clear();
    
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

void Sprite::Update()
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

void Sprite::Render() const
{
    if (!IsEnabled())
    {
        return;
    }
    
    if (!m_canvas)
    {
        return;
    }
    
    if (m_verts.empty())
    {
        return;
    }
    
    Renderer* renderer = m_canvas->GetSystemRenderer();
    Camera* camera = m_canvas->GetCamera();
    
    renderer->BeginCamera(*camera);
    renderer->SetRasterizerMode(RasterizerMode::SOLID_CULL_NONE);
    renderer->SetBlendMode(BlendMode::ALPHA);
    
    //renderer->BindTexture(m_texture);
    renderer->DrawVertexArray((int)m_verts.size(), m_verts.data());
    
    for (auto* child : m_children)
    {
        if (child)
        {
            child->Render();
        }
    }
    
    renderer->EndCamera(*camera);
}

void Sprite::SetTexture(Texture* texture)
{
    m_texture = texture;
}

void Sprite::SetBounds(AABB2 const& bounds)
{
    m_bound = bounds;
    InitializeVerts();
}

void Sprite::SetColor(Rgba8 color)
{
    if (m_originalColor != color)
    {
        m_originalColor = color;
        m_renderedColor = color;
        InitializeVerts();
    }
}

void Sprite::SetUVs(Vec2 const& uvMins, Vec2 const& uvMaxs)
{
    m_uvMins = uvMins;
    m_uvMaxs = uvMaxs;
    InitializeVerts();
}

void Sprite::SetUVs(AABB2 const& uvs)
{
    m_uvMins = uvs.m_mins;
    m_uvMaxs = uvs.m_maxs;
    InitializeVerts();
}

void Sprite::SetSprite(UISpriteAtlas* atlas, std::string const& spriteName)
{
    if (atlas)
    {
        m_texture = &atlas->GetTexture();
        AABB2 uvs = atlas->GetSpriteUVsByName(spriteName);
        m_uvMins = uvs.m_mins;
        m_uvMaxs = uvs.m_maxs;
        InitializeVerts();
    }
}

void Sprite::SetSprite(SpriteSheet* spriteSheet, int spriteIndex)
{
    if (spriteSheet)
    {
        m_texture = &spriteSheet->GetTexture();
        AABB2 uvs = spriteSheet->GetSpriteUVs(spriteIndex);
        m_uvMins = uvs.m_mins;
        m_uvMaxs = uvs.m_maxs;
        InitializeVerts();
    }
}

void Sprite::ResetUVs()
{
    // m_uvMins = Vec2(0.f, 0.f);
    // m_uvMaxs = Vec2(1.f, 1.f);
    InitializeVerts();
}

void Sprite::InitializeVerts()
{
    m_verts.clear();
    AddVertsForAABB2D(m_verts, m_bound, m_renderedColor, AABB2(m_uvMins, m_uvMaxs));
}
