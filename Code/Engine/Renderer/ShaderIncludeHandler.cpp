#include "ShaderIncludeHandler.h"
#include <fstream>
#include <algorithm>
#include <filesystem>  // C++17
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"

ShaderIncludeHandler::ShaderIncludeHandler(const std::string& baseDirectory)
    : m_baseDirectory(NormalizePath(baseDirectory))
{
    m_includePaths.push_back(m_baseDirectory);
}

void ShaderIncludeHandler::AddIncludePath(const std::string& path)
{
    m_includePaths.push_back(NormalizePath(path));
}

std::string ShaderIncludeHandler::NormalizePath(const std::string& path)
{
    std::string normalized = path;
    // 统一使用正斜杠
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    // 移除尾部斜杠
    while (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

std::string ShaderIncludeHandler::GetDirectoryFromPath(const std::string& filePath)
{
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos)
    {
        return filePath.substr(0, lastSlash);
    }
    return "";
}

HRESULT ShaderIncludeHandler::Open(D3D_INCLUDE_TYPE includeType, LPCSTR pFileName, 
                                   LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
{
    UNUSED(pParentData);
    
    std::string fileName = NormalizePath(pFileName);
    std::string fullPath;
    bool found = false;
    
    // 策略1: 对于LOCAL include，先尝试相对于当前文件的路径
    if (includeType == D3D_INCLUDE_LOCAL && !m_currentFileStack.empty())
    {
        std::string currentDir = GetDirectoryFromPath(m_currentFileStack.top());
        std::string relativePath = currentDir + "/" + fileName;
        
        std::ifstream testFile(relativePath);
        if (testFile.is_open())
        {
            testFile.close();
            fullPath = relativePath;
            found = true;
        }
    }
    
    // 策略2: 搜索所有include路径
    if (!found)
    {
        for (const auto& includePath : m_includePaths)
        {
            std::string testPath = includePath + "/" + fileName;
            std::ifstream testFile(testPath);
            if (testFile.is_open())
            {
                testFile.close();
                fullPath = testPath;
                found = true;
                break;
            }
        }
    }
    
    // 策略3: 尝试作为绝对路径或相对于工作目录的路径
    if (!found)
    {
        std::ifstream testFile(fileName);
        if (testFile.is_open())
        {
            testFile.close();
            fullPath = fileName;
            found = true;
        }
    }
    
    if (!found)
    {
        DebuggerPrintf("Failed to find include file: %s\n", pFileName);
        DebuggerPrintf("  Searched paths:\n");
        for (const auto& path : m_includePaths)
        {
            DebuggerPrintf("    - %s\n", path.c_str());
        }
        return E_FAIL;
    }
    
    // 读取文件
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        DebuggerPrintf("Failed to open include file: %s\n", fullPath.c_str());
        return E_FAIL;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    char* buffer = new char[size];
    if (!file.read(buffer, size))
    {
        delete[] buffer;
        return E_FAIL;
    }

    *ppData = buffer;
    *pBytes = (UINT)size;
    
    // 记录当前文件路径，支持嵌套include
    m_currentFileStack.push(fullPath);

    //DebuggerPrintf("Included: %s -> %s\n", pFileName, fullPath.c_str());
    
    return S_OK;
}

HRESULT ShaderIncludeHandler::Close(LPCVOID pData)
{
    // 弹出文件路径栈
    if (!m_currentFileStack.empty())
    {
        m_currentFileStack.pop();
    }
    
    delete[] static_cast<const char*>(pData);
    return S_OK;
}