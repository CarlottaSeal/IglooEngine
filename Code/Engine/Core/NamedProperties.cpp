#include "NamedProperties.hpp"
#include <cstdlib>

NamedProperties::NamedProperties(NamedProperties const& copyFrom)
{
	for (auto const& pair : copyFrom.m_properties)
	{
		m_properties[pair.first] = pair.second->Clone();
	}
}

NamedProperties& NamedProperties::operator=(NamedProperties const& copyFrom)
{
	if (this == &copyFrom)
		return *this;

	m_properties.clear();
	for (auto const& pair : copyFrom.m_properties)
	{
		m_properties[pair.first] = pair.second->Clone();
	}
	return *this;
}

//-----------------------------------------------------------------------------------------------
// String SetValue: always stored as std::string internally
//-----------------------------------------------------------------------------------------------
void NamedProperties::SetValue(std::string const& keyName, std::string const& value)
{
	HashedCaseInsensitiveString hcisKey(keyName);
	m_properties[hcisKey] = std::make_unique<TypedNamedProperty<std::string>>(value);
}

void NamedProperties::SetValue(std::string const& keyName, char const* value)
{
	SetValue(keyName, std::string(value));
}

//-----------------------------------------------------------------------------------------------
// String GetValue
//-----------------------------------------------------------------------------------------------
std::string NamedProperties::GetValue(std::string const& keyName, std::string const& defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (typed)
		return typed->m_value;

	return defaultValue;
}

std::string NamedProperties::GetValue(std::string const& keyName, char const* defaultValue) const
{
	return GetValue(keyName, std::string(defaultValue));
}

//-----------------------------------------------------------------------------------------------
// Primitive type GetValue: tries exact type match first, then string conversion fallback
//-----------------------------------------------------------------------------------------------
bool NamedProperties::GetValue(std::string const& keyName, bool defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<bool>*>(found->second.get());
	if (typed)
		return typed->m_value;

	// Backward compat: try string conversion
	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
		return (strProp->m_value == "true" || strProp->m_value == "1");

	return defaultValue;
}

int NamedProperties::GetValue(std::string const& keyName, int defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<int>*>(found->second.get());
	if (typed)
		return typed->m_value;

	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
		return atoi(strProp->m_value.c_str());

	return defaultValue;
}

float NamedProperties::GetValue(std::string const& keyName, float defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<float>*>(found->second.get());
	if (typed)
		return typed->m_value;

	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
		return static_cast<float>(atof(strProp->m_value.c_str()));

	return defaultValue;
}

//-----------------------------------------------------------------------------------------------
// Engine type GetValue: exact type match + string conversion via SetFromText
//-----------------------------------------------------------------------------------------------
Rgba8 NamedProperties::GetValue(std::string const& keyName, Rgba8 const& defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<Rgba8>*>(found->second.get());
	if (typed)
		return typed->m_value;

	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
	{
		Rgba8 result;
		result.SetFromText(strProp->m_value.c_str());
		return result;
	}
	return defaultValue;
}

Vec2 NamedProperties::GetValue(std::string const& keyName, Vec2 const& defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<Vec2>*>(found->second.get());
	if (typed)
		return typed->m_value;

	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
	{
		Vec2 result;
		result.SetFromText(strProp->m_value.c_str());
		return result;
	}
	return defaultValue;
}

IntVec2 NamedProperties::GetValue(std::string const& keyName, IntVec2 const& defaultValue) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	auto found = m_properties.find(hcisKey);
	if (found == m_properties.end())
		return defaultValue;

	auto* typed = dynamic_cast<TypedNamedProperty<IntVec2>*>(found->second.get());
	if (typed)
		return typed->m_value;

	auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(found->second.get());
	if (strProp)
	{
		IntVec2 result;
		result.SetFromText(strProp->m_value.c_str());
		return result;
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------------------------
void NamedProperties::PopulateFromXmlElementAttributes(XmlElement const& element)
{
	const XmlAttribute* attribute = element.FirstAttribute();
	while (attribute)
	{
		std::string keyName = attribute->Name();
		std::string value = attribute->Value();
		SetValue(keyName, value); // stored as std::string
		attribute = attribute->Next();
	}
}

void NamedProperties::DebugPrintContents() const
{
	for (auto const& pair : m_properties)
	{
		auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(pair.second.get());
		if (strProp)
			printf("%s = %s\n", pair.first.c_str(), strProp->m_value.c_str());
		else
			printf("%s = <non-string typed value>\n", pair.first.c_str());
	}
}

std::string NamedProperties::AppendToString() const
{
	std::string result;
	for (auto const& pair : m_properties)
	{
		auto* strProp = dynamic_cast<TypedNamedProperty<std::string>*>(pair.second.get());
		if (!strProp)
			continue;

		if (!result.empty())
			result += " ";

		result += pair.first.GetOriginalString() + "=";
		const std::string& value = strProp->m_value;
		bool needsQuotes = value.find(' ') != std::string::npos;
		if (needsQuotes)
			result += "\"" + value + "\"";
		else
			result += value;
	}
	return result;
}

bool NamedProperties::Has(std::string const& keyName) const
{
	HashedCaseInsensitiveString hcisKey(keyName);
	return m_properties.find(hcisKey) != m_properties.end();
}
