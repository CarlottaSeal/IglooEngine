// Checkbox.h - 完整实现（适配你的 GitHub 代码风格）
#pragma once
#include "UIElement.h"
#include "UIEvent.h"
#include "UISystem.h"

class Canvas;

class Checkbox : public UIElement {
public:
    Checkbox(Canvas* canvas, AABB2 const& bounds,
             bool isChecked = false,
             Rgba8 uncheckedColor = Rgba8::WHITE,
             Rgba8 checkedColor = Rgba8::GREEN,
             Rgba8 hoverColor = Rgba8(200, 200, 200));
    virtual ~Checkbox();
    
    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;
    
    // 状态
    void SetChecked(bool checked);
    bool IsChecked() const { return m_isChecked; }
    void Toggle();
    
    // 样式
    void SetUncheckedColor(Rgba8 color);
    void SetCheckedColor(Rgba8 color);
    void SetHoverColor(Rgba8 color);
    
    // 事件
    size_t OnCheckChangedEvent(UICallbackFunctionPointer callback);
    void RemoveCheckChangedListener(size_t id);
    
protected:
    Canvas* m_canvas = nullptr;
    
    bool m_isChecked = false;
    bool m_wasHoveredLastFrame = false;
    
    Rgba8 m_uncheckedColor = Rgba8::WHITE;
    Rgba8 m_checkedColor = Rgba8::GREEN;
    
    std::vector<Vertex_PCU> m_boxVerts;      // 外框
    std::vector<Vertex_PCU> m_checkVerts;    // 勾选标记
    
    UIEvent m_onCheckChangedEvent;
    
private:
    void UpdateVerts();
    void HandleInput();
};
