#pragma once
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Window/Window.hpp"

class UIManager;
class UIElement;

struct UIConfig
{
    Window* m_window = nullptr;
    Renderer* m_renderer = nullptr;
    InputSystem* m_inputSystem = nullptr;
    //BitmapFont* m_bitmapFont = nullptr;
    std::string m_bitmapFontName;
};

enum ElementType
{
    BUTTON,
   CHECKBOX,
   SLIDER,
   TEXT,
   SPRITE,
   PANEL,
   WIDGET,
   CANVAS,     
   PROGRESS_BAR, 
    UI_ELEMENT_COUNT
};

class UISystem
{
public:
    UISystem(UIConfig config);

    void Startup();
    void Shutdown();
    void BeginFrame();
    void EndFrame();

    void SetCamera(Camera camera);

    void QueueEnableInputNextFrame(UIElement* widget);
    bool HasWidgetOpened();

    Window* GetWindow();
    Renderer* GetRenderer();
    InputSystem* GetInputSystem();
    BitmapFont* GetBitmapFont();
    Camera GetCamera();

public:
    UIManager* m_theUIManager;

private:
    UIConfig m_config;
    Window* m_window = nullptr;
    InputSystem* m_inputSystem = nullptr;
    BitmapFont* m_bitmapFont = nullptr;
    Renderer* m_renderer = nullptr;
    Camera m_currentCamera;

    std::vector<UIElement*> m_uiElements;

    std::vector<UIElement*> m_widgetsToEnableInputNextFrame;
};
