#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <type_traits>

#include "Engine/Core/NamedProperties.hpp"
#include "Engine/Core/HashedCaseInsensitiveString.hpp"
#include "Engine/Core/Delegate.hpp"

class NamedProperties;

typedef NamedProperties EventArgs;
typedef bool (*EventCallBackFunction)(EventArgs& args);
typedef uint64_t SubscriptionHandle;

//-----------------------------------------------------------------------------------------------
// Subscription types (polymorphic base kept for backward compat with old unsubscribe-by-pointer API)
//-----------------------------------------------------------------------------------------------
class EventSubscriptionBase
{
public:
	virtual ~EventSubscriptionBase() = default;
	virtual bool Execute(EventArgs& args) = 0;
	virtual bool MatchesFunctionPtr(EventCallBackFunction /*func*/) const { return false; }
	virtual void* GetSubscriberObject() const { return nullptr; }
};

class FunctionEventSubscription : public EventSubscriptionBase
{
public:
	FunctionEventSubscription(EventCallBackFunction funcPtr) : m_funcPtr(funcPtr) {}
	bool Execute(EventArgs& args) override { return m_funcPtr(args); }
	bool MatchesFunctionPtr(EventCallBackFunction func) const override { return m_funcPtr == func; }

private:
	EventCallBackFunction m_funcPtr = nullptr;
};

template<typename OBJ_TYPE>
class MethodEventSubscription : public EventSubscriptionBase
{
public:
	typedef bool (OBJ_TYPE::*MethodPtr)(EventArgs&);

	MethodEventSubscription(OBJ_TYPE* obj, MethodPtr method)
		: m_object(obj), m_method(method) {}

	bool Execute(EventArgs& args) override { return (m_object->*m_method)(args); }
	void* GetSubscriberObject() const override { return (void*)m_object; }

	OBJ_TYPE* m_object = nullptr;
	MethodPtr m_method = nullptr;
};

// Lambda / Delegate subscription
class DelegateEventSubscription : public EventSubscriptionBase
{
public:
	Delegate<bool(EventArgs&)> m_delegate;
	void* m_objectPtr = nullptr;

	template<typename Callable>
	explicit DelegateEventSubscription(Callable&& c, void* obj = nullptr)
		: m_objectPtr(obj)
	{
		m_delegate.Bind(std::forward<Callable>(c));
	}

	bool Execute(EventArgs& args) override { return m_delegate.Execute(args); }
	void* GetSubscriberObject() const override { return m_objectPtr; }
};

//-----------------------------------------------------------------------------------------------
// Subscription entry wrapper
//-----------------------------------------------------------------------------------------------
struct EventSubscriptionEntry
{
	SubscriptionHandle handle = 0;
	std::unique_ptr<EventSubscriptionBase> subscription;
	bool pendingRemoval = false;
};

typedef std::vector<EventSubscriptionEntry> SubscriptionList;

struct EventSystemConfig
{
};

//-----------------------------------------------------------------------------------------------
class EventSystem
{
public:
	EventSystem(EventSystemConfig const& config);
	~EventSystem();

	void StartUp();
	void Shutdown();
	void BeginFrame();
	void EndFrame();

	//--- Old API (backward compat, same signatures as before) ---
	void SubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);
	void UnsubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);

	template<typename OBJ_TYPE>
	void SubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&));

	template<typename OBJ_TYPE>
	void UnsubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&));

	void UnsubscribeAllForObject(void* objectPtr);

	//--- New API (handle-based, supports lambdas) ---
	SubscriptionHandle Subscribe(std::string const& eventName, EventCallBackFunction functionPtr);

	template<typename OBJ_TYPE>
	SubscriptionHandle Subscribe(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&));

	// Lambda / functor: Subscribe("event", [this](EventArgs& a) { ... return false; });
	template<typename Callable, typename = std::enable_if_t<
		std::is_invocable_r_v<bool, std::decay_t<Callable>, EventArgs&>
		&& !std::is_same_v<std::decay_t<Callable>, EventCallBackFunction>>>
	SubscriptionHandle Subscribe(std::string const& eventName, Callable&& callback);

	void Unsubscribe(SubscriptionHandle handle);

	//--- Fire ---
	void FireEvent(std::string const& eventName, EventArgs& args);
	void FireEvent(std::string const& eventName);

	//--- Deferred event queue ---
	void QueueEvent(std::string const& eventName, EventArgs args);
	void DispatchQueuedEvents();

	Strings GetAllRegisteredCommands();

protected:
	SubscriptionHandle GenerateHandle() { return m_nextHandle++; }
	void CleanupPendingRemovals();

	EventSystemConfig m_config;

	std::unordered_map<HashedCaseInsensitiveString, SubscriptionList> m_subscriptionListByEventName;
	// O(1) handle → event lookup, avoids full scan in Unsubscribe
	std::unordered_map<SubscriptionHandle, HashedCaseInsensitiveString> m_handleToEvent;

	SubscriptionHandle m_nextHandle = 1;
	int m_firingDepth = 0;

	struct QueuedEvent
	{
		HashedCaseInsensitiveString eventName;
		EventArgs args;
	};
	std::vector<QueuedEvent> m_eventQueue;
};

//-----------------------------------------------------------------------------------------------
// Template implementations
//-----------------------------------------------------------------------------------------------
template<typename OBJ_TYPE>
void EventSystem::SubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&))
{
	HashedCaseInsensitiveString key(eventName);
	EventSubscriptionEntry entry;
	entry.handle = GenerateHandle();
	entry.subscription = std::make_unique<MethodEventSubscription<OBJ_TYPE>>(obj, method);
	m_handleToEvent[entry.handle] = key;
	m_subscriptionListByEventName[key].push_back(std::move(entry));
}

template<typename OBJ_TYPE>
void EventSystem::UnsubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&))
{
	HashedCaseInsensitiveString key(eventName);
	auto mapIt = m_subscriptionListByEventName.find(key);
	if (mapIt != m_subscriptionListByEventName.end())
	{
		for (auto& entry : mapIt->second)
		{
			if (entry.pendingRemoval) continue;
			auto* methodSub = dynamic_cast<MethodEventSubscription<OBJ_TYPE>*>(entry.subscription.get());
			if (methodSub && methodSub->m_object == obj && methodSub->m_method == method)
			{
				entry.pendingRemoval = true;
				m_handleToEvent.erase(entry.handle);
				break;
			}
		}
		if (m_firingDepth == 0)
			CleanupPendingRemovals();
	}
}

template<typename OBJ_TYPE>
SubscriptionHandle EventSystem::Subscribe(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&))
{
	HashedCaseInsensitiveString key(eventName);
	EventSubscriptionEntry entry;
	entry.handle = GenerateHandle();
	entry.subscription = std::make_unique<MethodEventSubscription<OBJ_TYPE>>(obj, method);
	SubscriptionHandle h = entry.handle;
	m_handleToEvent[h] = key;
	m_subscriptionListByEventName[key].push_back(std::move(entry));
	return h;
}

template<typename Callable, typename>
SubscriptionHandle EventSystem::Subscribe(std::string const& eventName, Callable&& callback)
{
	HashedCaseInsensitiveString key(eventName);
	EventSubscriptionEntry entry;
	entry.handle = GenerateHandle();
	entry.subscription = std::make_unique<DelegateEventSubscription>(std::forward<Callable>(callback));
	SubscriptionHandle h = entry.handle;
	m_handleToEvent[h] = key;
	m_subscriptionListByEventName[key].push_back(std::move(entry));
	return h;
}

//-----------------------------------------------------------------------------------------------
class EventRecipient
{
public:
	virtual ~EventRecipient();
};

//-----------------------------------------------------------------------------------------------
// Standalone global helper functions
//-----------------------------------------------------------------------------------------------
void SubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);
void UnsubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);
void FireEvent(std::string const& eventName, EventArgs& args);
void FireEvent(std::string const& eventName);
