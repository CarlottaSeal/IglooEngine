#pragma once

#pragma once
#include "Engine/UI/Widget.h"
#include <string>

class Canvas;
class UISystem;

enum class UIScreenType
{
    UNKNOWN,
    MAIN_MENU,
    PAUSE_MENU,
    INVENTORY,
    HUD,
    CHEST,
    FURNACE,
    CRAFTING_TABLE,
    OPTIONS,
    WORLD_SELECT,
    SERVER_LIST,
    CUSTOM,
    COUNT
};

class UIScreen
{
public:
    UIScreen(UISystem* uiSystem, UIScreenType type,Camera& camera, bool blocksInput = true);
    virtual ~UIScreen();

    virtual void OnEnter();     
    virtual void OnExit();      
    virtual void OnPause();     
    virtual void OnResume();    
    
    virtual void Build() = 0;   
    
    virtual void Update(float deltaSeconds);
    virtual void Render() const;
    virtual void HandleInput();

    
    
    UIScreenType GetType() const { return m_type; }
    bool BlocksInput() const { return m_blocksInput; }
    bool IsActive() const { return m_isActive; }
    void SetActive(bool active);
    
    Canvas* GetCanvas() const { return m_canvas; }
    
protected:
    UISystem* m_uiSystem = nullptr;
    Camera* m_camera = nullptr;
    Canvas* m_canvas = nullptr;
    
    UIScreenType m_type = UIScreenType::UNKNOWN;
    bool m_blocksInput = true;  // 是否阻挡下层屏幕的输入
    bool m_isActive = false;
    
    //std::vector<UIElement*> m_elements; 
};

