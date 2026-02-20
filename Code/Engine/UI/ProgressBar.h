#pragma once
#include "UIElement.h"

class Canvas;

enum class ProgressBarOrientation
{
    HORIZONTAL,
    VERTICAL
};

class ProgressBar : public UIElement
{
public:
    ProgressBar(Canvas* canvas, AABB2 const& bounds, 
                float minValue, float maxValue,
                Rgba8 backgroundColor = Rgba8(64, 64, 64),
                Rgba8 fillColor = Rgba8(0, 200, 0),
                Rgba8 borderColor = Rgba8::BLACK,
                bool hasBorder = true);
    virtual ~ProgressBar();
    
    void StartUp() override;
    void ShutDown() override;
    void Update() override;
    void Render() const override;
    

    void SetBounds(AABB2 const& bounds);
    void SetValue(float value);
    void SetValueNormalized(float normalizedValue);  // 0.0 - 1.0
    void SetMinValue(float minValue);
    void SetMaxValue(float maxValue);
    void SetBackgroundColor(Rgba8 color);
    void SetFillColor(Rgba8 color);
    void SetBorderColor(Rgba8 color);
    void SetOrientation(ProgressBarOrientation orientation);
    

    float GetValue() const { return m_currentValue; }
    float GetValueNormalized() const;
    float GetMinValue() const { return m_minValue; }
    float GetMaxValue() const { return m_maxValue; }
    
protected:
    Canvas* m_canvas = nullptr;
    
    float m_minValue = 0.0f;
    float m_maxValue = 100.0f;
    float m_currentValue = 0.0f;
    
    Rgba8 m_backgroundColor = Rgba8(64, 64, 64);
    Rgba8 m_fillColor = Rgba8(0, 200, 0);
    Rgba8 m_borderColor = Rgba8::BLACK;
    bool m_hasBorder = true;
    
    ProgressBarOrientation m_orientation = ProgressBarOrientation::HORIZONTAL;
    
    std::vector<Vertex_PCU> m_backgroundVerts;
    std::vector<Vertex_PCU> m_fillVerts;
    std::vector<Vertex_PCU> m_borderVerts;
    
private:
    void UpdateVerts();
    void UpdateFillVerts();
};