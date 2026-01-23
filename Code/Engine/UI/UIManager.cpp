#include "UIManager.h"

UIManager::UIManager(UISystem* uiSystem)
    : m_uiSystem(uiSystem)
{
}

UIManager::~UIManager()
{
    m_screenStack.clear();
}

void UIManager::Update(float deltaSeconds)
{
    // 从底层到顶层更新所有激活的屏幕
    for (auto* screen : m_screenStack)
    {
        if (screen && screen->IsActive())
        {
            screen->Update(deltaSeconds);
            
            // 如果这个屏幕阻挡输入，停止更新下层屏幕
            if (screen->BlocksInput())
            {
                break;
            }
        }
    }
}

void UIManager::Render() const
{
    for (auto* screen : m_screenStack)
    {
        if (screen)
        {
            screen->Render();
        }
    }
}

void UIManager::PushScreen(UIScreen* screen)
{
    if (!screen)
    {
        return;
    }
    
    // 暂停当前顶层屏幕
    if (!m_screenStack.empty())
    {
        UIScreen* topScreen = m_screenStack.back();
        if (topScreen)
        {
            topScreen->OnPause();
        }
    }
    
    m_screenStack.push_back(screen);
    screen->OnEnter();
}

void UIManager::PopScreen()
{
    if (m_screenStack.empty())
    {
        return;
    }
    
    UIScreen* topScreen = m_screenStack.back();
    m_screenStack.pop_back();
    
    if (topScreen)
    {
        topScreen->OnExit();
    }
    
    if (!m_screenStack.empty())
    {
        UIScreen* newTop = m_screenStack.back();
        if (newTop)
        {
            newTop->OnEnter();
        }
    }
}

void UIManager::PopAllScreens()
{
    while (!m_screenStack.empty())
    {
        PopScreen();
    }
}

void UIManager::ReplaceScreen(UIScreen* screen)
{
    if (!screen)
    {
        return;
    }
    
    // 先弹出当前屏幕
    if (!m_screenStack.empty())
    {
        PopScreen();
    }
    
    // 再压入新屏幕
    PushScreen(screen);
}

UIScreen* UIManager::GetTopScreen() const
{
    if (m_screenStack.empty())
    {
        return nullptr;
    }
    return m_screenStack.back();
}

UIScreen* UIManager::GetScreenByType(UIScreenType type) const
{
    for (auto* screen : m_screenStack)
    {
        if (screen && screen->GetType() == type)
        {
            return screen;
        }
    }
    return nullptr;
}

bool UIManager::HasScreenType(UIScreenType type) const
{
    return GetScreenByType(type) != nullptr;
}

void UIManager::Reset()
{
}

bool UIManager::IsInputBlocked() const
{
    // 如果顶层屏幕阻挡输入，则输入被阻挡
    UIScreen* topScreen = GetTopScreen();
    if (topScreen)
    {
        return topScreen->BlocksInput();
    }
    return false;
}
