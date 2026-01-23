#pragma once
//-----------------------------------------------------------------------------------------------
#include <string>
#include <vector>

//-----------------------------------------------------------------------------------------------
const std::string Stringf( char const* format, ... );
const std::string Stringf( int maxLength, char const* format, ... );

const std::string LowerAll(std::string const& str);
bool EndsWith(const std::string& str, const std::string& suffix);

typedef std::vector< std::string >	Strings;
Strings SplitStringOnDelimiter(std::string const& originalString, char delimiterToSplitOn, bool skipEmpty);
Strings SplitStringOnDelimiter(std::string const& originalString, char delimiterToSplitOn);

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to);
void EraseEmptyStrings(Strings& tokens);

std::string JoinStrings(const Strings& tokens, size_t startIndex, const std::string& delimiter = " ");


