#pragma once
#include "UIScreen.h"
#include <vector>

class UIManager
{
public:
    UIManager(UISystem* uiSystem);
    ~UIManager();
    
    void Update(float deltaSeconds);
    void Render() const;
    
    void PushScreen(UIScreen* screen);      
    void PopScreen();                       
    void PopAllScreens();                   
    void ReplaceScreen(UIScreen* screen);   
    
    UIScreen* GetTopScreen() const;
    UIScreen* GetScreenByType(UIScreenType type) const;
    int GetScreenStackSize() const { return (int)m_screenStack.size(); }
    bool HasScreenType(UIScreenType type) const;

    virtual void Reset();
    bool IsInputBlocked() const;  
    
private:
    UISystem* m_uiSystem = nullptr;
    std::vector<UIScreen*> m_screenStack;
};
