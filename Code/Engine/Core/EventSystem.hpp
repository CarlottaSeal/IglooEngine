#pragma once
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <algorithm>

#include "Engine/Core/NamedProperties.hpp"
#include "Engine/Core/HashedCaseInsensitiveString.hpp"

class NamedProperties;

typedef NamedProperties EventArgs;
typedef bool (*EventCallBackFunction)(EventArgs& args);

//-----------------------------------------------------------------------------------------------
// Polymorphic subscription base - supports both free functions and member functions
//-----------------------------------------------------------------------------------------------
class EventSubscriptionBase
{
public:
	virtual ~EventSubscriptionBase() = default;
	virtual bool Execute(EventArgs& args) = 0;
	virtual bool MatchesFunctionPtr(EventCallBackFunction /*func*/) const { return false; }
	virtual void* GetSubscriberObject() const { return nullptr; }
};

//-----------------------------------------------------------------------------------------------
class FunctionEventSubscription : public EventSubscriptionBase
{
public:
	FunctionEventSubscription(EventCallBackFunction funcPtr) : m_funcPtr(funcPtr) {}

	bool Execute(EventArgs& args) override { return m_funcPtr(args); }
	bool MatchesFunctionPtr(EventCallBackFunction func) const override { return m_funcPtr == func; }

private:
	EventCallBackFunction m_funcPtr = nullptr;
};

//-----------------------------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------------------------
typedef std::vector<std::shared_ptr<EventSubscriptionBase>> SubscriptionList;

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

	void SubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);
	void UnsubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr);

	template<typename OBJ_TYPE>
	void SubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&));

	template<typename OBJ_TYPE>
	void UnsubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&));

	void UnsubscribeAllForObject(void* objectPtr);

	void FireEvent(std::string const& eventName, EventArgs& args);
	void FireEvent(std::string const& eventName);

	Strings GetAllRegisteredCommands();

protected:
	EventSystemConfig m_config;
	mutable std::recursive_mutex m_mutex;
	std::map<HashedCaseInsensitiveString, SubscriptionList> m_subscriptionListByEventName;
};

//-----------------------------------------------------------------------------------------------
// Template implementations
//-----------------------------------------------------------------------------------------------
template<typename OBJ_TYPE>
void EventSystem::SubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&))
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	HashedCaseInsensitiveString key(eventName);
	m_subscriptionListByEventName[key].push_back(
		std::make_shared<MethodEventSubscription<OBJ_TYPE>>(obj, method)
	);
}

template<typename OBJ_TYPE>
void EventSystem::UnsubscribeEventCallbackObjectMethod(std::string const& eventName, OBJ_TYPE* obj, bool (OBJ_TYPE::*method)(EventArgs&))
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	HashedCaseInsensitiveString key(eventName);
	auto mapIt = m_subscriptionListByEventName.find(key);
	if (mapIt != m_subscriptionListByEventName.end())
	{
		SubscriptionList& subs = mapIt->second;
		subs.erase(std::remove_if(subs.begin(), subs.end(),
			[obj, method](std::shared_ptr<EventSubscriptionBase> const& sub)
			{
				auto* methodSub = dynamic_cast<MethodEventSubscription<OBJ_TYPE>*>(sub.get());
				if (!methodSub)
					return false;
				return methodSub->m_object == obj && methodSub->m_method == method;
			}), subs.end());
	}
}

//-----------------------------------------------------------------------------------------------
// EventRecipient: derive from this to auto-unsubscribe all member function callbacks on destruction
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
