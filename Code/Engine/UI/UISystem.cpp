#include "UISystem.h"
#include "UIElement.h"
#include "UIManager.h"
#include "Engine/Core/EngineCommon.hpp"

UISystem* g_theUISystem = nullptr;

UISystem::UISystem(UIConfig config)
    : m_config(config)
{
    m_window = config.m_window;
    m_renderer = config.m_renderer;
    m_inputSystem = config.m_inputSystem;
    //m_bitmapFont = config.m_bitmapFont;
}

void UISystem::Startup()
{
    m_theUIManager = new UIManager(this);
    m_bitmapFont = m_renderer->CreateOrGetBitmapFont(("Data/Fonts/" + m_config.m_bitmapFontName).c_str());
    for (UIElement* uiElement : m_uiElements)
    {
        uiElement->StartUp();
    }
}

void UISystem::Shutdown()
{
}

void UISystem::BeginFrame()
{
    for (UIElement* u : m_widgetsToEnableInputNextFrame)
    {
        if (u)
        {
            u->SetInteractive(true);
        }
    }
    m_widgetsToEnableInputNextFrame.clear();
}

void UISystem::EndFrame()
{
}

void UISystem::SetCamera(Camera camera)
{
    m_currentCamera = camera;
}

void UISystem::QueueEnableInputNextFrame(UIElement* widget) //这个函数应该只适用于widget（即canvas）
{
    if (widget != nullptr)
    {
        if (std::find(m_widgetsToEnableInputNextFrame.begin(), m_widgetsToEnableInputNextFrame.end(), widget) == m_widgetsToEnableInputNextFrame.end())
        {
            m_widgetsToEnableInputNextFrame.push_back(widget);
        }
    }
}

bool UISystem::HasWidgetOpened()
{
    for (UIElement* element : m_uiElements)
    {
        if (element->GetType() == WIDGET)
        {
            if (element->IsEnabled())
                return true;
        }
    }
    return false;
}

Window* UISystem::GetWindow()
{
    return m_window;
}

Renderer* UISystem::GetRenderer()
{
    return m_renderer;
}

InputSystem* UISystem::GetInputSystem()
{
    return m_inputSystem;
}

BitmapFont* UISystem::GetBitmapFont()
{
    return m_bitmapFont;
}

Camera UISystem::GetCamera()
{
    return m_currentCamera;
}
