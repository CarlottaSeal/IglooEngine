#include "Engine/Core/Event/EventSystem.hpp"
#include "Engine/Core/EngineCommon.hpp"

EventSystem* g_theEventSystem = nullptr;

EventSystem::EventSystem(EventSystemConfig const& config)
	: m_config(config)
{
}

EventSystem::~EventSystem()
{
}

void EventSystem::StartUp()
{
}

void EventSystem::Shutdown()
{
	m_subscriptionListByEventName.clear();
	m_handleToEvent.clear();
	m_eventQueue.clear();
}

void EventSystem::BeginFrame()
{
	DispatchQueuedEvents();
}

void EventSystem::EndFrame()
{
}

// Old API (backward compat)
void EventSystem::SubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr)
{
	HashedCaseInsensitiveString key(eventName);
	EventSubscriptionEntry entry;
	entry.handle = GenerateHandle();
	entry.subscription = std::make_unique<FunctionEventSubscription>(functionPtr);
	m_handleToEvent[entry.handle] = key;
	m_subscriptionListByEventName[key].push_back(std::move(entry));
}

void EventSystem::UnsubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr)
{
	HashedCaseInsensitiveString key(eventName);
	auto it = m_subscriptionListByEventName.find(key);
	if (it != m_subscriptionListByEventName.end())
	{
		for (auto& entry : it->second)
		{
			if (!entry.pendingRemoval && entry.subscription->MatchesFunctionPtr(functionPtr))
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

void EventSystem::UnsubscribeAllForObject(void* objectPtr)
{
	for (auto& pair : m_subscriptionListByEventName)
	{
		for (auto& entry : pair.second)
		{
			if (!entry.pendingRemoval && entry.subscription->GetSubscriberObject() == objectPtr)
			{
				entry.pendingRemoval = true;
				m_handleToEvent.erase(entry.handle);
			}
		}
	}
	if (m_firingDepth == 0)
		CleanupPendingRemovals();
}

//-----------------------------------------------------------------------------------------------
// New API (handle-based)
SubscriptionHandle EventSystem::Subscribe(std::string const& eventName, EventCallBackFunction functionPtr)
{
	HashedCaseInsensitiveString key(eventName);
	EventSubscriptionEntry entry;
	entry.handle = GenerateHandle();
	entry.subscription = std::make_unique<FunctionEventSubscription>(functionPtr);
	SubscriptionHandle h = entry.handle;
	m_handleToEvent[h] = key;
	m_subscriptionListByEventName[key].push_back(std::move(entry));
	return h;
}

void EventSystem::Unsubscribe(SubscriptionHandle handle)
{
	// O(1) event lookup via reverse map
	auto mapIt = m_handleToEvent.find(handle);
	if (mapIt == m_handleToEvent.end())
		return;

	HashedCaseInsensitiveString key = mapIt->second;
	m_handleToEvent.erase(mapIt);

	auto listIt = m_subscriptionListByEventName.find(key);
	if (listIt != m_subscriptionListByEventName.end())
	{
		for (auto& entry : listIt->second)
		{
			if (entry.handle == handle)
			{
				entry.pendingRemoval = true;
				break;
			}
		}
	}

	if (m_firingDepth == 0)
		CleanupPendingRemovals();
}

// Fire
void EventSystem::FireEvent(std::string const& eventName, EventArgs& args)
{
	HashedCaseInsensitiveString key(eventName);

	auto it = m_subscriptionListByEventName.find(key);
	if (it == m_subscriptionListByEventName.end())
	{
		if (g_theDevConsole && eventName != "Warning")
		{
			EventArgs warningArgs;
			warningArgs.SetValue("warningEvent", eventName);
			FireEvent("Warning", warningArgs);
		}
		return;
	}

	++m_firingDepth;
	SubscriptionList& subs = it->second;
	for (auto& entry : subs)
	{
		if (entry.pendingRemoval) continue;
		bool consumed = entry.subscription->Execute(args);
		if (consumed)
			break;
	}
	--m_firingDepth;

	if (m_firingDepth == 0)
		CleanupPendingRemovals();
}

void EventSystem::FireEvent(std::string const& eventName)
{
	EventArgs emptyArgs;
	FireEvent(eventName, emptyArgs);
}

// Deferred event queue
void EventSystem::QueueEvent(std::string const& eventName, EventArgs args)
{
	QueuedEvent qe;
	qe.eventName = HashedCaseInsensitiveString(eventName);
	qe.args = std::move(args);
	m_eventQueue.push_back(std::move(qe));
}

void EventSystem::DispatchQueuedEvents()
{
	// Swap out so events queued during dispatch go to next frame, not infinite recursion
	std::vector<QueuedEvent> currentQueue;
	currentQueue.swap(m_eventQueue);

	for (auto& qe : currentQueue)
	{
		FireEvent(qe.eventName.GetOriginalString(), qe.args);
	}
}

Strings EventSystem::GetAllRegisteredCommands()
{
	Strings commands;
	for (auto const& pair : m_subscriptionListByEventName)
	{
		bool hasActive = false;
		for (auto const& entry : pair.second)
		{
			if (!entry.pendingRemoval)
			{
				hasActive = true;
				break;
			}
		}
		if (hasActive)
			commands.push_back(pair.first.GetOriginalString());
	}
	return commands;
}

void EventSystem::CleanupPendingRemovals()
{
	for (auto mapIt = m_subscriptionListByEventName.begin(); mapIt != m_subscriptionListByEventName.end(); )
	{
		SubscriptionList& subs = mapIt->second;
		subs.erase(std::remove_if(subs.begin(), subs.end(),
			[](EventSubscriptionEntry const& e) { return e.pendingRemoval; }),
			subs.end());

		if (subs.empty())
			mapIt = m_subscriptionListByEventName.erase(mapIt);
		else
			++mapIt;
	}
}

EventRecipient::~EventRecipient()
{
	if (g_theEventSystem)
	{
		g_theEventSystem->UnsubscribeAllForObject(this);
	}
}

// Global helper functions
void SubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr)
{
	if (g_theEventSystem)
		g_theEventSystem->SubscribeEventCallBackFunction(eventName, functionPtr);
}

void UnsubscribeEventCallBackFunction(std::string const& eventName, EventCallBackFunction functionPtr)
{
	if (g_theEventSystem)
		g_theEventSystem->UnsubscribeEventCallBackFunction(eventName, functionPtr);
}

void FireEvent(std::string const& eventName, EventArgs& args)
{
	if (g_theEventSystem)
		g_theEventSystem->FireEvent(eventName, args);
}

void FireEvent(std::string const& eventName)
{
	if (g_theEventSystem)
		g_theEventSystem->FireEvent(eventName);
}
