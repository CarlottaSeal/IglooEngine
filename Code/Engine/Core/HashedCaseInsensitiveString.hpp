#pragma once
#include <string>

class HashedCaseInsensitiveString
{
public:
	HashedCaseInsensitiveString() = default;
	HashedCaseInsensitiveString(HashedCaseInsensitiveString const& copyFrom);
	HashedCaseInsensitiveString(char const* text);
	HashedCaseInsensitiveString(std::string const& text);

	unsigned int GetHash() const { return m_lowerCaseHash; }
	std::string const& GetOriginalString() const { return m_caseIntactText; }
	char const* c_str() const { return m_caseIntactText.c_str(); }

	bool operator<(HashedCaseInsensitiveString const& compare) const;
	bool operator==(HashedCaseInsensitiveString const& compare) const;
	bool operator!=(HashedCaseInsensitiveString const& compare) const;
	bool operator==(char const* text) const;
	bool operator!=(char const* text) const;
	bool operator==(std::string const& text) const;
	bool operator!=(std::string const& text) const;

	void operator=(HashedCaseInsensitiveString const& assignFrom);
	void operator=(char const* text);
	void operator=(std::string const& text);

private:
	void CalcHash();

	std::string  m_caseIntactText;
	unsigned int m_lowerCaseHash = 0;
};

typedef HashedCaseInsensitiveString HCIString;
