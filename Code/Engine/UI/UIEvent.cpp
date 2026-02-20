#include "UIEvent.h"

UICallbackFunction::UICallbackFunction(UICallbackFunctionPointer callback, size_t id)
    : m_callbackPtr(callback), m_callbackID(id)
{
}


bool UICallbackFunction::operator==(UICallbackFunction const& compare)
{ 
    return m_callbackID == compare.m_callbackID; 
}

size_t UIEvent::AddListener(UICallbackFunctionPointer const& callback)
{
    // 寻找空位
    for (size_t i = 0; i < m_callbackList.size(); i++)
    {
        if (!m_callbackList[i])
        {
            m_callbackList[i] = new UICallbackFunction(callback, i);
            return i;
        }
    }

    // 没有空位，添加新的
    m_callbackList.push_back(new UICallbackFunction(callback, m_callbackList.size()));
    return m_callbackList.size() - 1;
}

void UIEvent::RemoveListener(size_t callbackID)
{
    for (size_t i = 0; i < m_callbackList.size(); i++)
    {
        if (m_callbackList[i] && m_callbackList[i]->m_callbackID == callbackID)
        {
            delete m_callbackList[i];
            m_callbackList[i] = nullptr;
            return;
        }
    }
}

void UIEvent::Invoke()
{
    for (size_t i = 0; i < m_callbackList.size(); i++)
    {
        if (m_callbackList[i])
        {
            m_callbackList[i]->m_callbackPtr();
        }
    }
}

void UIEvent::Clear()
{
    for (auto* callback : m_callbackList)
    {
        delete callback;
    }
    m_callbackList.clear();
}

bool UIEvent::HasListeners() const
{
    for (const auto* callback : m_callbackList)
    {
        if (callback) return true;
    }
    return false;
}

UIEvent::~UIEvent()
{
    Clear();
}
