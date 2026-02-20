#pragma once
#include <functional>
#include <vector>

using UICallbackFunctionPointer = std::function<void()>;

struct UICallbackFunction
{
    UICallbackFunction(UICallbackFunctionPointer callback, size_t id);

    size_t m_callbackID;
    UICallbackFunctionPointer m_callbackPtr;

    bool operator==(UICallbackFunction const& compare);
};

class UIEvent
{
public:
    size_t AddListener(UICallbackFunctionPointer const& callback);
    void RemoveListener(size_t callbackID);

    void Invoke();
    
    void Clear();

    bool HasListeners() const;

    ~UIEvent();

public:
    std::vector<UICallbackFunction*> m_callbackList;
};