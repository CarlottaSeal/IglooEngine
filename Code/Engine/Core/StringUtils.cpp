#include "Engine/Core/StringUtils.hpp"
#include <stdarg.h>
#include <queue>
#include <algorithm>

//-----------------------------------------------------------------------------------------------
constexpr int STRINGF_STACK_LOCAL_TEMP_LENGTH = 2048;


//-----------------------------------------------------------------------------------------------
const std::string Stringf( char const* format, ... )
{
	char textLiteral[ STRINGF_STACK_LOCAL_TEMP_LENGTH ];
	va_list variableArgumentList;
	va_start( variableArgumentList, format );
	vsnprintf_s( textLiteral, STRINGF_STACK_LOCAL_TEMP_LENGTH, _TRUNCATE, format, variableArgumentList );	
	va_end( variableArgumentList );
	textLiteral[ STRINGF_STACK_LOCAL_TEMP_LENGTH - 1 ] = '\0'; // In case vsnprintf overran (doesn't auto-terminate)

	return std::string( textLiteral );
}


//-----------------------------------------------------------------------------------------------
const std::string Stringf( int maxLength, char const* format, ... )
{
	char textLiteralSmall[ STRINGF_STACK_LOCAL_TEMP_LENGTH ];
	char* textLiteral = textLiteralSmall;
	if( maxLength > STRINGF_STACK_LOCAL_TEMP_LENGTH )
		textLiteral = new char[ maxLength ];

	va_list variableArgumentList;
	va_start( variableArgumentList, format );
	vsnprintf_s( textLiteral, maxLength, _TRUNCATE, format, variableArgumentList );	
	va_end( variableArgumentList );
	textLiteral[ maxLength - 1 ] = '\0'; // In case vsnprintf overran (doesn't auto-terminate)

	std::string returnValue( textLiteral );
	if( maxLength > STRINGF_STACK_LOCAL_TEMP_LENGTH )
		delete[] textLiteral;

	return returnValue;
}

const std::string LowerAll(std::string const& str)
{
	std::string lower;
	lower.reserve(str.size()); 

	for (char c : str)
	{
		lower.push_back((char)tolower(c)); 
	}

	return lower;
}

bool EndsWith(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() &&
		   str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Strings SplitStringOnDelimiter(std::string const& originalString, char delimiterToSplitOn, bool skipEmpty /*= false*/)
{
	std::string clean = originalString;

	std::replace(clean.begin(), clean.end(), '\t', ' ');

	if (!clean.empty() && clean.back() == '\r') 
	{
		clean.pop_back();
	}

	Strings result;
	size_t start = 0;
	size_t end = 0;

	while ((end = clean.find(delimiterToSplitOn, start)) != std::string::npos) 
	{
		std::string token = clean.substr(start, end - start);
		if (!(skipEmpty && token.empty())) 
		{
			result.emplace_back(std::move(token));
		}
		start = end + 1;
	}

	std::string token = clean.substr(start);
	if (!(skipEmpty && token.empty())) 
	{
		result.emplace_back(std::move(token));
	}

	return result;
}

Strings SplitStringOnDelimiter(std::string const& originalString, char delimiterToSplitOn)
{
	return SplitStringOnDelimiter(originalString, delimiterToSplitOn, false);
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to)
{
	size_t startPos = 0;
	while ((startPos = str.find(from, startPos)) != std::string::npos)
	{
		str.replace(startPos, from.length(), to);
		startPos += to.length();
	}
	return str;
}

void EraseEmptyStrings(Strings& tokens)
{
	tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const std::string& s) {
		return s.empty();
	}), tokens.end());
}

std::string JoinStrings(const Strings& tokens, size_t startIndex, const std::string& delimiter)
{
	if (startIndex >= tokens.size()) return "";
    
	std::string result = tokens[startIndex];
	for (size_t i = startIndex + 1; i < tokens.size(); ++i)
	{
		result += delimiter + tokens[i];
	}
	return result;
}


