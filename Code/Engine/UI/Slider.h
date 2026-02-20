#pragma once
#include "UIElement.h"
#include "UIEvent.h"
#include "UISystem.h"

class Canvas;

class Slider : public UIElement
{
public:
    Slider(Canvas* canvas, AABB2 const& trackBounds,
           float minValue = 0.0f, float maxValue = 100.0f,
           float currentValue = 50.0f,
           Rgba8 trackColor = Rgba8(128, 128, 128),
           Rgba8 handleColor = Rgba8::WHITE,
           Rgba8 fillColor = Rgba8::GREEN);
    virtual ~Slider();
    
    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;
    
    // 值操作
    void SetValue(float value);
    void SetValueNormalized(float normalizedValue);  // 0.0 - 1.0
    float GetValue() const { return m_currentValue; }
    float GetValueNormalized() const;
    
    void SetMinValue(float minValue);
    void SetMaxValue(float maxValue);
    float GetMinValue() const { return m_minValue; }
    float GetMaxValue() const { return m_maxValue; }
    
    // 样式
    void SetTrackColor(Rgba8 color);
    void SetHandleColor(Rgba8 color);
    void SetFillColor(Rgba8 color);
    void SetHandleSize(float size);
    
    // 事件
    size_t OnValueChangedEvent(UICallbackFunctionPointer callback);
    void RemoveValueChangedListener(size_t id);
    
protected:
    Canvas* m_canvas = nullptr;
    
    float m_minValue = 0.0f;
    float m_maxValue = 100.0f;
    float m_currentValue = 50.0f;
    
    Rgba8 m_trackColor = Rgba8(128, 128, 128);
    Rgba8 m_handleColor = Rgba8::WHITE;
    Rgba8 m_fillColor = Rgba8::GREEN;
    
    float m_handleSize = 20.0f;
    bool m_isDragging = false;
    
    AABB2 m_trackBounds;
    AABB2 m_handleBounds;
    
    std::vector<Vertex_PCU> m_trackVerts;
    std::vector<Vertex_PCU> m_fillVerts;
    std::vector<Vertex_PCU> m_handleVerts;
    
    UIEvent m_onValueChangedEvent;
    
private:
    void UpdateVerts();
    void UpdateHandlePosition();
    void HandleInput();
    Vec2 GetHandleCenter() const;
};
