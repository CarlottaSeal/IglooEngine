#pragma once
#include <vector>
#include <string>


int FileReadToBuffer(std::vector<uint8_t>& outBuffer, const std::string& fileName);
int FileReadToString(std::string& outString, const std::string& fileName);
bool FileWriteFromBuffer(const std::vector<uint8_t>& buffer, const std::string& fileName);