#pragma once
#include <map>
#include <string>
#include <vector>

#include "SaveCommon.h"
#include "Engine/Core/XmlUtils.hpp"

class ISerializable;

struct SaveConfig
{
    SaveFormat m_defaultSaveFormat = SaveFormat::BINARY;
    std::string m_saveDirectory = "Saves";
};

class SaveSystem
{
public:
    SaveSystem(SaveConfig config);
    ~SaveSystem();

    static bool EnsureDirectory(const std::string& path);
    static bool EnsureDirectoryForFile(const std::string& filepath);
    static bool DirectoryExists(const std::string& path);
    
    SaveFormat GetDefaultFormat() const { return m_defaultFormat; }
    
    bool Save(const std::string& filename, const ISerializable* object, SaveFormat format = SaveFormat::BINARY, bool createIfNeeded= true);
    bool Load(const std::string& filename, ISerializable* object, SaveFormat format = SaveFormat::BINARY);
    
    bool SaveBinary(const std::string& filename, const std::vector<uint8_t>& data, bool createDirectoryIfNeeded = true);
    bool LoadBinary(const std::string& filename, std::vector<uint8_t>& data);
    bool LoadBinaryObject(const std::string& filename, ISerializable* object);
    
    bool SaveXML(const std::string& filename, XmlDocument& doc);
    bool LoadXML(const std::string& filename, XmlDocument& doc);
    bool SaveObjectToXML(const std::string& filename, const ISerializable* object);
    bool LoadXMLObject(const std::string& filename, ISerializable* object);
    
    bool SaveText(const std::string& filename, const std::string& text);
    bool LoadText(const std::string& filename, std::string& text);
    bool SaveLines(const std::string& filename, const std::vector<std::string>& lines);
    bool LoadLines(const std::string& filename, std::vector<std::string>& lines);
    bool LoadTextObject(const std::string& filename, ISerializable* object);
    
    bool SaveKeyValues(const std::string& filename, const std::map<std::string, std::string>& kvp);
    bool LoadKeyValues(const std::string& filename, std::map<std::string, std::string>& kvp);
    bool LoadJSONObject(const std::string& filename, ISerializable* object);
    
    bool SaveCSV(const std::string& filename, const std::vector<std::vector<std::string>>& data);
    bool LoadCSV(const std::string& filename, std::vector<std::vector<std::string>>& data);

    SaveFormat DetectFormatFromFile(const std::string& filename);
    void SetSaveDirectory(const std::string& directory);
    bool FileExists(const std::string& filename) const;
    bool DeleteSavedFile(const std::string& filename);
    std::vector<std::string> GetAllSavedFiles(const std::string& extension = "*") const;

    void ForceCreateDefaultSaveFolder(const std::string& folderPath = "Saves");
    bool ForceDeleteFolder(const std::string& folderPath = "Saves");
    
    bool QuickSave(int slot, const ISerializable* object, SaveFormat format = SaveFormat::BINARY);
    bool QuickLoad(int slot, ISerializable* object, SaveFormat format = SaveFormat::BINARY);
    
    // Save context management for subdirectories
    void PushSaveContext(const std::string& subdirectory);
    void PopSaveContext();
    std::string GetCurrentSaveDirectory() const;
    size_t GetStackSize() const { return m_directoryStack.size(); }
    void DumpContextStack() const;
    
private:
    std::string GetFullPath(const std::string& filename) const;
    std::string GetExtensionForFormat(SaveFormat format) const;
    
private:
    SaveConfig m_config;
    std::string m_saveDirectory = "Saves";
    SaveFormat m_defaultFormat = SaveFormat::BINARY;
    std::vector<std::string> m_directoryStack;  // For context management
};