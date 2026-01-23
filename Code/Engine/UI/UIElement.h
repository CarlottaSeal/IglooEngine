#pragma once
#include "UISystem.h"

class Widget;

class UIElement
{
public:
    UIElement();
    virtual ~UIElement() = default;

    virtual void StartUp();
    virtual void ShutDown();
    virtual void Update();
    virtual void Render() const;

    bool AddChild(UIElement* child);
    bool RemoveChild(UIElement* child);
    void SetParent(UIElement* parent);

    void SetActive(bool active) { SetEnabled(active); }
    bool IsActive() const { return m_isEnabled && m_isInteractive; }

    bool IsOtherElementHavingFocus() const;

    const AABB2& GetBounds() const { return m_bound; }
    const ElementType GetType() const { return m_type; }

    virtual void SetEnabled(bool enabled);
    void SetFocused(bool focused);
    void SetInteractive(bool isInteractive);
    bool IsEnabled();
    bool IsEnabled() const;
    bool IsInteractive();
    bool HasFocus();
    void ResetStatus(); //except enabled

    void InitializeVerts();

public:
    std::vector<UIElement*> m_children;
    
protected:
    UIElement* m_parent;
    ElementType m_type;

    AABB2 m_bound;

    std::vector<Vertex_PCU> m_verts;
    std::vector<Vertex_PCU> m_textVerts;

    Rgba8 m_renderedColor;
    Rgba8 m_originalColor = Rgba8::GREY;
    Rgba8 m_hoveredColor = Rgba8::GREY;

    bool m_isInteractive = true;
    bool m_isEnabled = true;
    bool m_isFocused = false;
    bool m_isHovered = false;
};
