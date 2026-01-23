#pragma once
#include <d3dcommon.h>
#include <string>
#include <vector>
#include <stack>

class ShaderIncludeHandler : public ID3DInclude
{
public:
    ShaderIncludeHandler(const std::string& baseDirectory);
    
    void AddIncludePath(const std::string& path);

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData,
        LPCVOID* ppData, UINT* pBytes) override;

    HRESULT __stdcall Close(LPCVOID pData) override;

private:
    std::string NormalizePath(const std::string& path);
    std::string GetDirectoryFromPath(const std::string& filePath);
    
    std::string m_baseDirectory;
    std::vector<std::string> m_includePaths;
    
    std::stack<std::string> m_currentFileStack;
};