#pragma once
#include "Engine/Core/HashedCaseInsensitiveString.hpp"
#include "Engine/Core/Rgba8.hpp"
#include "Engine/Math/Vec2.hpp"
#include "Engine/Math/IntVec2.hpp"
#include "Engine/Core/XmlUtils.hpp"
#include <map>
#include <string>
#include <memory>

//-----------------------------------------------------------------------------------------------
// Type-erased base for storing arbitrary property values
//-----------------------------------------------------------------------------------------------
class NamedPropertyBase
{
public:
	virtual ~NamedPropertyBase() = default;
	virtual std::unique_ptr<NamedPropertyBase> Clone() const = 0;
};

template<typename T>
class TypedNamedProperty : public NamedPropertyBase
{
public:
	explicit TypedNamedProperty(T const& val) : m_value(val) {}
	T m_value;

	std::unique_ptr<NamedPropertyBase> Clone() const override
	{
		return std::make_unique<TypedNamedProperty<T>>(m_value);
	}
};

//-----------------------------------------------------------------------------------------------
// NamedProperties: strongly-typed mixed-type key-value container
// Superset of NamedStrings - all NamedStrings operations work here too
//-----------------------------------------------------------------------------------------------
class NamedProperties
{
public:
	NamedProperties() = default;
	NamedProperties(NamedProperties const& copyFrom);
	NamedProperties(NamedProperties&& moveFrom) = default;
	NamedProperties& operator=(NamedProperties const& copyFrom);
	NamedProperties& operator=(NamedProperties&& moveFrom) = default;
	~NamedProperties() = default;

	//-----------------------------------------------------------------------------------------------
	// Explicit overloads for string-related types (NamedStrings backward compat)
	//-----------------------------------------------------------------------------------------------
	void SetValue(std::string const& keyName, std::string const& value);
	void SetValue(std::string const& keyName, char const* value);

	std::string GetValue(std::string const& keyName, std::string const& defaultValue) const;
	std::string GetValue(std::string const& keyName, char const* defaultValue) const;
	bool        GetValue(std::string const& keyName, bool defaultValue) const;
	int         GetValue(std::string const& keyName, int defaultValue) const;
	float       GetValue(std::string const& keyName, float defaultValue) const;
	Rgba8       GetValue(std::string const& keyName, Rgba8 const& defaultValue) const;
	Vec2        GetValue(std::string const& keyName, Vec2 const& defaultValue) const;
	IntVec2     GetValue(std::string const& keyName, IntVec2 const& defaultValue) const;

	//-----------------------------------------------------------------------------------------------
	// Template versions for arbitrary types (game-specific types, nested NamedProperties, etc.)
	//-----------------------------------------------------------------------------------------------
	template<typename T> void SetValue(std::string const& keyName, T const& value);
	template<typename T> T    GetValue(std::string const& keyName, T const& defaultValue) const;

	void PopulateFromXmlElementAttributes(XmlElement const& element);

	void DebugPrintContents() const;
	std::string AppendToString() const;
	bool IsEmpty() const { return m_properties.empty(); }
	bool Has(std::string const& keyName) const;

private:
	std::map<HashedCaseInsensitiveString, std::unique_ptr<NamedPropertyBase>> m_properties;
};

//-----------------------------------------------------------------------------------------------
// Template implementations
//-----------------------------------------------------------------------------------------------
template<typename T>
void NamedProperties::SetValue(std::string const& keyName, T const& value)
{
	HashedCaseInsensitiveString hcisKey(keyName);
	m_properties[hcisKey] = std::make_unique<TypedNamedProperty<T>>(value);
}

template<typename T>
T NamedProperties::GetValue(std::string const& keyName, T const& defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	// Exact type match
	TypedNamedProperty<T>* typed = dynamic_cast<TypedNamedProperty<T>*>(found->second.get());
	if (typed)
		return typed->m_value;

	return defaultValue;
}
