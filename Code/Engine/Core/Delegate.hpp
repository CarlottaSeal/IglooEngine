#pragma once
#include <new>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <vector>
#include <algorithm>

//-----------------------------------------------------------------------------------------------
// Delegate<RetVal(Args...)>
// Type-safe callable wrapper with Small Buffer Optimization (no heap allocation).
// Supports free functions, member functions, lambdas, functors.
// Unlike std::function: zero heap alloc, and unlike raw function pointers: supports captures.
//-----------------------------------------------------------------------------------------------
template<typename Signature>
class Delegate;

template<typename RetVal, typename... Args>
class Delegate<RetVal(Args...)>
{
	static constexpr size_t BUFFER_SIZE = 48;
	alignas(std::max_align_t) unsigned char m_storage[BUFFER_SIZE] = {};

	using InvokerFn  = RetVal(*)(void*, Args...);
	using DestructFn = void(*)(void*);
	using CopyFn     = void(*)(void*, const void*);
	using MoveFn     = void(*)(void*, void*);

	InvokerFn  m_invoke   = nullptr;
	DestructFn m_destruct = nullptr;
	CopyFn     m_copy     = nullptr;
	MoveFn     m_move     = nullptr;

public:
	Delegate() = default;
	~Delegate() { Clear(); }

	Delegate(Delegate const& other)
		: m_invoke(other.m_invoke)
		, m_destruct(other.m_destruct)
		, m_copy(other.m_copy)
		, m_move(other.m_move)
	{
		if (m_copy)
			m_copy(m_storage, other.m_storage);
	}

	Delegate(Delegate&& other) noexcept
		: m_invoke(other.m_invoke)
		, m_destruct(other.m_destruct)
		, m_copy(other.m_copy)
		, m_move(other.m_move)
	{
		if (m_move)
		{
			m_move(m_storage, other.m_storage);
			other.m_invoke = nullptr;
			other.m_destruct = nullptr;
		}
	}

	Delegate& operator=(Delegate const& other)
	{
		if (this != &other)
		{
			Clear();
			m_invoke = other.m_invoke;
			m_destruct = other.m_destruct;
			m_copy = other.m_copy;
			m_move = other.m_move;
			if (m_copy)
				m_copy(m_storage, other.m_storage);
		}
		return *this;
	}

	Delegate& operator=(Delegate&& other) noexcept
	{
		if (this != &other)
		{
			Clear();
			m_invoke = other.m_invoke;
			m_destruct = other.m_destruct;
			m_copy = other.m_copy;
			m_move = other.m_move;
			if (m_move)
			{
				m_move(m_storage, other.m_storage);
				other.m_invoke = nullptr;
				other.m_destruct = nullptr;
			}
		}
		return *this;
	}

	// Bind any callable (free function, lambda, functor)
	template<typename Callable>
	void Bind(Callable&& callable)
	{
		using Stored = std::decay_t<Callable>;
		static_assert(sizeof(Stored) <= BUFFER_SIZE, "Callable too large for Delegate SBO");
		static_assert(alignof(Stored) <= alignof(std::max_align_t), "Callable alignment too strict");

		Clear();
		new (m_storage) Stored(std::forward<Callable>(callable));

		m_invoke = [](void* s, Args... args) -> RetVal {
			return (*reinterpret_cast<Stored*>(s))(args...);
		};
		m_destruct = [](void* s) {
			reinterpret_cast<Stored*>(s)->~Stored();
		};
		m_copy = [](void* dst, const void* src) {
			new (dst) Stored(*reinterpret_cast<const Stored*>(src));
		};
		m_move = [](void* dst, void* src) {
			new (dst) Stored(std::move(*reinterpret_cast<Stored*>(src)));
			reinterpret_cast<Stored*>(src)->~Stored();
		};
	}

	// Bind member function
	template<typename Obj>
	void Bind(Obj* obj, RetVal(Obj::*method)(Args...))
	{
		struct Wrapper
		{
			Obj* obj;
			RetVal(Obj::*method)(Args...);
			RetVal operator()(Args... args) { return (obj->*method)(args...); }
		};
		Bind(Wrapper{ obj, method });
	}

	RetVal Execute(Args... args)
	{
		return m_invoke(m_storage, args...);
	}

	bool IsBound() const { return m_invoke != nullptr; }
	explicit operator bool() const { return IsBound(); }

	void Clear()
	{
		if (m_destruct)
			m_destruct(m_storage);
		m_invoke = nullptr;
		m_destruct = nullptr;
		m_copy = nullptr;
		m_move = nullptr;
	}
};

//-----------------------------------------------------------------------------------------------
// MulticastDelegate<Args...>
// Multiple listeners, handle-based add/remove, safe broadcast during modification.
//-----------------------------------------------------------------------------------------------
template<typename... Args>
class MulticastDelegate
{
	using DelegateType = Delegate<void(Args...)>;

	struct Entry
	{
		uint64_t handle = 0;
		DelegateType callback;
		void* objectPtr = nullptr;
		bool pendingRemoval = false;
	};

	std::vector<Entry> m_entries;
	uint64_t m_nextHandle = 1;
	int m_broadcastDepth = 0;

	void CleanupPending()
	{
		m_entries.erase(
			std::remove_if(m_entries.begin(), m_entries.end(),
				[](Entry const& e) { return e.pendingRemoval; }),
			m_entries.end());
	}

public:
	template<typename Callable>
	uint64_t Add(Callable&& callable)
	{
		Entry e;
		e.handle = m_nextHandle++;
		e.callback.Bind(std::forward<Callable>(callable));
		m_entries.push_back(std::move(e));
		return m_entries.back().handle;
	}

	template<typename Obj>
	uint64_t Add(Obj* obj, void(Obj::*method)(Args...))
	{
		Entry e;
		e.handle = m_nextHandle++;
		e.callback.Bind(obj, method);
		e.objectPtr = (void*)obj;
		m_entries.push_back(std::move(e));
		return m_entries.back().handle;
	}

	void Remove(uint64_t handle)
	{
		for (auto& e : m_entries)
		{
			if (e.handle == handle)
			{
				e.pendingRemoval = true;
				break;
			}
		}
		if (m_broadcastDepth == 0)
			CleanupPending();
	}

	void RemoveAllForObject(void* obj)
	{
		for (auto& e : m_entries)
		{
			if (e.objectPtr == obj)
				e.pendingRemoval = true;
		}
		if (m_broadcastDepth == 0)
			CleanupPending();
	}

	void Broadcast(Args... args)
	{
		++m_broadcastDepth;
		for (auto& e : m_entries)
		{
			if (!e.pendingRemoval)
				e.callback.Execute(args...);
		}
		--m_broadcastDepth;
		if (m_broadcastDepth == 0)
			CleanupPending();
	}

	void RemoveAll()
	{
		if (m_broadcastDepth > 0)
		{
			for (auto& e : m_entries)
				e.pendingRemoval = true;
		}
		else
		{
			m_entries.clear();
		}
	}

	bool HasBindings() const
	{
		for (auto& e : m_entries)
			if (!e.pendingRemoval) return true;
		return false;
	}
};
