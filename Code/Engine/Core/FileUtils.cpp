#include "FileUtils.hpp"

#include <vector>
#include <string>
#include <stdio.h>

int FileReadToBuffer(std::vector<uint8_t>& outBuffer, const std::string& fileName)
{
	FILE* file = nullptr;
	errno_t err = fopen_s(&file, fileName.c_str(), "rb");
	if (err != 0 || file == nullptr)
	{
		return -1;
	}

	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);

	outBuffer.resize(fileSize);

	size_t bytesRead = fread(outBuffer.data(), 1, fileSize, file);
	fclose(file);

	if (bytesRead != static_cast<size_t>(fileSize))
	{
		return -1;
	}

	return static_cast<int>(bytesRead);
}

int FileReadToString(std::string& outString, const std::string& fileName)
{
	std::vector<uint8_t> buffer;
	int size = FileReadToBuffer(buffer, fileName);
	buffer.push_back('\0');
	if (size > 0)
	{
		const char* data = (const char*)buffer.data();
		outString = (char*)data;
	}

	return size;
}

bool FileWriteFromBuffer(const std::vector<uint8_t>& buffer, const std::string& fileName)
{
	FILE* file = nullptr;
	errno_t err = fopen_s(&file, fileName.c_str(), "wb");
	if (err != 0 || file == nullptr)
	{
		return false;
	}

	size_t bytesWritten = fwrite(buffer.data(), 1, buffer.size(), file);
	fclose(file);

	return bytesWritten == buffer.size();
}
