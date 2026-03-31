#include "HashedCaseInsensitiveString.hpp"
#include <cctype>
#include <cstring>

HashedCaseInsensitiveString::HashedCaseInsensitiveString(HashedCaseInsensitiveString const& copyFrom)
	: m_caseIntactText(copyFrom.m_caseIntactText)
	, m_lowerCaseHash(copyFrom.m_lowerCaseHash)
{
}

HashedCaseInsensitiveString::HashedCaseInsensitiveString(char const* text)
	: m_caseIntactText(text)
{
	CalcHash();
}

HashedCaseInsensitiveString::HashedCaseInsensitiveString(std::string const& text)
	: m_caseIntactText(text)
{
	CalcHash();
}

void HashedCaseInsensitiveString::CalcHash()
{
	m_lowerCaseHash = 0;
	for (char c : m_caseIntactText)
	{
		m_lowerCaseHash *= 31;
		m_lowerCaseHash += (unsigned int)tolower((unsigned char)c);
	}
}

bool HashedCaseInsensitiveString::operator<(HashedCaseInsensitiveString const& compare) const
{
	if (m_lowerCaseHash != compare.m_lowerCaseHash)
		return m_lowerCaseHash < compare.m_lowerCaseHash;

	return _stricmp(m_caseIntactText.c_str(), compare.m_caseIntactText.c_str()) < 0;
}

bool HashedCaseInsensitiveString::operator==(HashedCaseInsensitiveString const& compare) const
{
	if (m_lowerCaseHash != compare.m_lowerCaseHash)
		return false;

	return _stricmp(m_caseIntactText.c_str(), compare.m_caseIntactText.c_str()) == 0;
}

bool HashedCaseInsensitiveString::operator!=(HashedCaseInsensitiveString const& compare) const
{
	return !(*this == compare);
}

bool HashedCaseInsensitiveString::operator==(char const* text) const
{
	return *this == HashedCaseInsensitiveString(text);
}

bool HashedCaseInsensitiveString::operator!=(char const* text) const
{
	return !(*this == text);
}

bool HashedCaseInsensitiveString::operator==(std::string const& text) const
{
	return *this == HashedCaseInsensitiveString(text);
}

bool HashedCaseInsensitiveString::operator!=(std::string const& text) const
{
	return !(*this == text);
}

void HashedCaseInsensitiveString::operator=(HashedCaseInsensitiveString const& assignFrom)
{
	m_caseIntactText = assignFrom.m_caseIntactText;
	m_lowerCaseHash = assignFrom.m_lowerCaseHash;
}

void HashedCaseInsensitiveString::operator=(char const* text)
{
	m_caseIntactText = text;
	CalcHash();
}

void HashedCaseInsensitiveString::operator=(std::string const& text)
{
	m_caseIntactText = text;
	CalcHash();
}
