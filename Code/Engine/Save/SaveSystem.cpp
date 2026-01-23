#include "SaveSystem.h"
#include "ISerializable.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include "Engine/Core/XmlUtils.hpp"

extern SaveSystem* g_theSaveSystem;

SaveSystem::SaveSystem(SaveConfig config)
    : m_config(config)
{
    m_defaultFormat = config.m_defaultSaveFormat;

    m_saveDirectory = config.m_saveDirectory;
    
    ForceCreateDefaultSaveFolder(m_saveDirectory);
}

SaveSystem::~SaveSystem()
{
}

bool SaveSystem::EnsureDirectory(const std::string& path)
{
    try
    {
        if (std::filesystem::exists(path))
        {
            if (std::filesystem::is_directory(path))
            {
                return true;  
            }
            else
            {
                return false;  
            }
        }
        
        // 创建目录（包括所有父目录）
        bool created = std::filesystem::create_directories(path);
        // if (created)
        // {
        //     DebuggerPrintf("[SaveSystem] Created directory: %s\n", path.c_str());
        // }
        return created;
    }
    catch (const std::exception& e)
    {
        DebuggerPrintf("[SaveSystem] Failed to create directory '%s': %s\n", 
                       path.c_str(), e.what());
        return false;
    }
}

bool SaveSystem::EnsureDirectoryForFile(const std::string& filepath)
{
    std::filesystem::path p(filepath);
    if (!p.has_parent_path())
    {
        return true;
    }
    
    return EnsureDirectory(p.parent_path().string());
}

bool SaveSystem::DirectoryExists(const std::string& path)
{
    try
    {
        return std::filesystem::exists(path) && std::filesystem::is_directory(path);
    }
    catch (const std::exception& e)
    {
        DebuggerPrintf("[SaveSystem] Error checking directory '%s': %s\n", 
                       path.c_str(), e.what());
        return false;
    }
}

bool SaveSystem::Save(const std::string& filename, const ISerializable* object, SaveFormat format, bool createIfNeeded)
{
    if (!object)
        return false;
    
    switch (format)
    {
    case SaveFormat::BINARY:
        {
            std::vector<uint8_t> buffer;
            object->SaveToBinary(buffer);
            return SaveBinary(filename, buffer, createIfNeeded);
        }
    case SaveFormat::XML:
        return SaveObjectToXML(filename, object);
    case SaveFormat::TEXT:
        {
            std::string text = object->SaveToString();
            return SaveText(filename, text);
        }
    case SaveFormat::JSON:
        {
            auto kvp = object->SaveToKeyValues();
            return SaveKeyValues(filename, kvp);
        }
    default:
        return false;
    }
}

bool SaveSystem::Load(const std::string& filename, ISerializable* object, SaveFormat format)
{
    if (!object)
    {
        ERROR_RECOVERABLE("Load failed: null object");
        return false;
    }
    
    std::string fullPath = GetFullPath(filename);
    
    if (!std::filesystem::exists(fullPath))
    {
        return false;
    }

    format = DetectFormatFromFile(filename);
    if (format == SaveFormat::UNKNOWN)
    {
        format = m_defaultFormat;
    }

    switch (format)
    {
    case SaveFormat::BINARY:
        return LoadBinaryObject(filename, object);
        
    case SaveFormat::XML:
        return LoadXMLObject(filename, object);
        
    case SaveFormat::JSON:
        return LoadJSONObject(filename, object);
        
    case SaveFormat::TEXT:
        return LoadTextObject(filename, object);
        
    default:
        ERROR_RECOVERABLE(Stringf("Unknown save format for file: %s", filename.c_str()));
        return false;
    }
}

bool SaveSystem::SaveBinary(const std::string& filename, const std::vector<uint8_t>& data, bool createDirectoryIfNeeded)
{
    std::string fullPath = GetFullPath(filename);
    if (createDirectoryIfNeeded)
    {
        try
        {
            std::filesystem::path p(fullPath);
            if (p.has_parent_path())
            {
                std::filesystem::create_directories(p.parent_path());
            }
        }
        catch (const std::exception& e)
        {
            DebuggerPrintf("[SaveSystem] Failed to create directory for '%s': %s\n", 
                           fullPath.c_str(), e.what());
        }
    }
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open())
    {
        ERROR_RECOVERABLE(Stringf("Failed to open file for writing: %s", fullPath.c_str()));
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    bool success = file.good();
    file.close();
    
    if (!success)
    {
        ERROR_RECOVERABLE(Stringf("Failed to write file: %s", fullPath.c_str()));
    }
    return success;
}

bool SaveSystem::LoadBinary(const std::string& filename, std::vector<uint8_t>& data)
{
    std::string fullPath = GetFullPath(filename);
    
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open())
    {
        ERROR_RECOVERABLE(Stringf("Failed to open file for reading: %s", fullPath.c_str()));
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    data.resize(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    
    bool success = file.good();
    file.close();
    
    if (!success)
    {
        ERROR_RECOVERABLE(Stringf("Failed to read file: %s", fullPath.c_str()));
        data.clear();
    }
    return success;
}

bool SaveSystem::LoadBinaryObject(const std::string& filename, ISerializable* object)
{
    std::vector<uint8_t> buffer;
    if (!LoadBinary(filename, buffer))
        return false;
    
    size_t offset = 0;
    return object->LoadFromBinary(buffer, offset);
}

bool SaveSystem::SaveXML(const std::string& filename, XmlDocument& doc)
{
    std::string fullPath = GetFullPath(filename);
    return doc.SaveFile(fullPath.c_str()) == tinyxml2::XML_SUCCESS;
}

bool SaveSystem::LoadXML(const std::string& filename, XmlDocument& doc)
{
    std::string fullPath = GetFullPath(filename);
    return doc.LoadFile(fullPath.c_str()) == XmlResult::XML_SUCCESS;
}

bool SaveSystem::SaveObjectToXML(const std::string& filename, const ISerializable* object)
{
    XmlDocument doc;
    
    XmlElement* root = doc.NewElement("SaveData");
    doc.InsertFirstChild(root);
    
    root->SetAttribute("identifier", object->GetSaveIdentifier().c_str());
    object->SaveToXML(root);
    
    return SaveXML(filename, doc);
}

bool SaveSystem::LoadXMLObject(const std::string& filename, ISerializable* object)
{
    XmlDocument doc;
    if (!LoadXML(filename, doc))
        return false;
    
    XmlElement* root = doc.RootElement();
    if (!root)
    {
        ERROR_RECOVERABLE(Stringf("XML file has no root element: %s", filename.c_str()));
        return false;
    }
    
    const char* identifier = root->Attribute("identifier");
    if (identifier && strcmp(identifier, object->GetSaveIdentifier().c_str()) != 0)
    {
        ERROR_RECOVERABLE(Stringf("Identifier mismatch: expected '%s', got '%s'",
                                object->GetSaveIdentifier().c_str(), identifier));
        return false;
    }
    
    return object->LoadFromXML(root);
}

bool SaveSystem::SaveText(const std::string& filename, const std::string& text)
{
    std::string fullPath = GetFullPath(filename);
    std::ofstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    file << text;
    file.close();
    return true;
}

bool SaveSystem::LoadText(const std::string& filename, std::string& text)
{
    std::string fullPath = GetFullPath(filename);
    std::ifstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    file.close();
    return true;
}

bool SaveSystem::SaveLines(const std::string& filename, const std::vector<std::string>& lines)
{
    std::string fullPath = GetFullPath(filename);
    std::ofstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    for (const auto& line : lines)
    {
        file << line << "\n";
    }
    file.close();
    return true;
}

bool SaveSystem::LoadLines(const std::string& filename, std::vector<std::string>& lines)
{
    std::string fullPath = GetFullPath(filename);
    std::ifstream file(fullPath);
    
    if (!file.is_open())
        return false;
    lines.clear();
    std::string line;
    
    while (std::getline(file, line))
    {
        lines.push_back(line);
    }
    file.close();
    return true;
}

bool SaveSystem::LoadTextObject(const std::string& filename, ISerializable* object)
{
    std::string text;
    if (!LoadText(filename, text))
        return false;
    
    return object->LoadFromString(text);
}

bool SaveSystem::SaveKeyValues(const std::string& filename, const std::map<std::string, std::string>& kvp)
{
    std::string fullPath = GetFullPath(filename);
    std::ofstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    for (const auto& pair : kvp)
    {
        file << pair.first << "=" << pair.second << "\n";
    }
    
    file.close();
    return true;
}

bool SaveSystem::LoadKeyValues(const std::string& filename, std::map<std::string, std::string>& kvp)
{
    std::string fullPath = GetFullPath(filename);
    std::ifstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    kvp.clear();
    std::string line;
    
    while (std::getline(file, line))
    {
        size_t equalPos = line.find('=');
        if (equalPos != std::string::npos)
        {
            std::string key = line.substr(0, equalPos);
            std::string value = line.substr(equalPos + 1);
            kvp[key] = value;
        }
    }
    
    file.close();
    return true;
}

bool SaveSystem::LoadJSONObject(const std::string& filename, ISerializable* object)
{
    std::map<std::string, std::string> kvp;
    if (!LoadKeyValues(filename, kvp))
        return false;
    return object->LoadFromKeyValues(kvp);
}

bool SaveSystem::SaveCSV(const std::string& filename, const std::vector<std::vector<std::string>>& data)
{
    std::string fullPath = GetFullPath(filename);
    std::ofstream file(fullPath);
    
    if (!file.is_open())
        return false;
    
    for (const auto& row : data)
    {
        for (size_t i = 0; i < row.size(); i++)
        {
            std::string cell = row[i]; // 如果包含逗号或引号，需要用引号包围
            if (cell.find(',') != std::string::npos || cell.find('"') != std::string::npos)
            {
                // 转义引号
                size_t pos = 0;
                while ((pos = cell.find('"', pos)) != std::string::npos)
                {
                    cell.replace(pos, 1, "\"\"");
                    pos += 2;
                }
                cell = "\"" + cell + "\"";
            }
            
            file << cell;
            if (i < row.size() - 1)
                file << ",";
        }
        file << "\n";
    }
    
    file.close();
    return true;
}

bool SaveSystem::LoadCSV(const std::string& filename, std::vector<std::vector<std::string>>& data)
{
    std::string fullPath = GetFullPath(filename);
    
    std::ifstream file(fullPath);
    if (!file.is_open())
    {
        ERROR_RECOVERABLE(Stringf("Failed to open CSV file: %s", fullPath.c_str()));
        return false;
    }
    
    data.clear();
    std::string line;
    
    while (std::getline(file, line))
    {
        std::vector<std::string> row;
        std::stringstream lineStream(line);
        std::string cell;
        bool insideQuotes = false;
        std::string currentCell;
        
        for (char c : line)
        {
            if (c == '"')
            {
                if (insideQuotes && lineStream.peek() == '"')
                {
                    currentCell += '"'; // 双引号转义
                    lineStream.get(); // 跳过下一个引号
                }
                else
                {
                    insideQuotes = !insideQuotes;
                }
            }
            else if (c == ',' && !insideQuotes)
            {
                row.push_back(currentCell);
                currentCell.clear();
            }
            else
            {
                currentCell += c;
            }
        }
        
        row.push_back(currentCell); //添加最后一个单元格
        data.push_back(row);
    }
    
    file.close();
    return true;
}

SaveFormat SaveSystem::DetectFormatFromFile(const std::string& filename)
{
    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos)
        return SaveFormat::UNKNOWN;
    
    std::string ext = filename.substr(dotPos);
    
    for (char& c : ext)
        c = (char)std::tolower(c);
    
    if (ext == ".xml")
        return SaveFormat::XML;
    else if (ext == ".json" || ext == ".ini" || ext == ".cfg" || ext == ".config")
        return SaveFormat::JSON;
    else if (ext == ".txt" || ext == ".text" || ext == ".log")
        return SaveFormat::TEXT;
    else if (ext == ".bin" || ext == ".dat" || ext == ".chunk" || ext == ".save")
        return SaveFormat::BINARY;
    else if (ext == ".csv")
        return SaveFormat::CSV;
    
    return SaveFormat::UNKNOWN;
}

void SaveSystem::SetSaveDirectory(const std::string& directory)
{
    if (directory.empty())
    {
        DebuggerPrintf("SetSaveDirectory: Ignoring empty directory\n");
        return;  // 直接返回，不修改 m_saveDirectory
    }
    m_saveDirectory = directory;
    try
    {
        std::filesystem::create_directories(m_saveDirectory);
    }
    catch (const std::exception& e)
    {
        ERROR_RECOVERABLE(Stringf("Failed to create directory '%s': %s", 
            m_saveDirectory.c_str(), e.what()));
    }
}

bool SaveSystem::FileExists(const std::string& filename) const
{
    std::string fullPath = GetFullPath(filename);
    return std::filesystem::exists(fullPath);
}

bool SaveSystem::DeleteSavedFile(const std::string& filename)
{
    std::string fullPath = GetFullPath(filename);
    return std::filesystem::remove(fullPath);
}

std::vector<std::string> SaveSystem::GetAllSavedFiles(const std::string& extension) const
{
    std::vector<std::string> files;
    
    std::filesystem::path saveDir(m_saveDirectory);
    if (!std::filesystem::exists(saveDir))
        return files;
    
    for (const auto& entry : std::filesystem::directory_iterator(saveDir))
    {
        if (entry.is_regular_file())
        {
            std::string filename = entry.path().filename().string();
            
            if (extension == "*")
            {
                files.push_back(filename);
            }
            else
            {
                if (entry.path().extension() == extension)
                {
                    files.push_back(filename);
                }
            }
        }
    }
    return files;
}

void SaveSystem::ForceCreateDefaultSaveFolder(const std::string& folderPath)
{
    UNUSED(folderPath)
	if (!m_saveDirectory.empty())
	{
		std::filesystem::create_directories(m_saveDirectory);
	}
}

bool SaveSystem::ForceDeleteFolder(const std::string& folderPath)
{
	namespace fs = std::filesystem;
	try 
    {
		if (fs::exists(folderPath)) 
        {
			fs::remove_all(folderPath); 
			return true;
		}
	}
	catch (const std::exception& e) 
    {
        DebuggerPrintf("Failed to delete folder %d", e.what());
	}
	return false;
}

bool SaveSystem::QuickSave(int slot, const ISerializable* object, SaveFormat format)
{
    std::string filename = Stringf("QuickSave_%d%s", slot, GetExtensionForFormat(format).c_str());
    return Save(filename, object, format);
}

bool SaveSystem::QuickLoad(int slot, ISerializable* object, SaveFormat format)
{
    std::string filename = Stringf("QuickSave_%d%s", slot, GetExtensionForFormat(format).c_str());
    return Load(filename, object, format);
}

std::string SaveSystem::GetFullPath(const std::string& filename) const
{
    if (m_saveDirectory.empty())
        return filename;
    return m_saveDirectory + "/" + filename;
}

std::string SaveSystem::GetExtensionForFormat(SaveFormat format) const
{
    switch (format)
    {
    case SaveFormat::BINARY:
        return ".dat";
    case SaveFormat::XML:
        return ".xml";
    case SaveFormat::JSON:
        return ".json";
    case SaveFormat::TEXT:
        return ".txt";
    case SaveFormat::CSV:
        return ".csv";
    default:
        return "";
    }
}

void SaveSystem::PushSaveContext(const std::string& subdirectory)
{
    if (subdirectory.empty())
    {
        ERROR_RECOVERABLE("PushSaveContext: Empty string! Not pushing.");
        return; 
    }
    
    m_directoryStack.push_back(m_saveDirectory);
    SetSaveDirectory(subdirectory);
}

void SaveSystem::PopSaveContext()
{
    if (!m_directoryStack.empty())
    {
        std::string previousDir = m_directoryStack.back();
        m_directoryStack.pop_back();
        SetSaveDirectory(previousDir);
    }
}

std::string SaveSystem::GetCurrentSaveDirectory() const
{
    return m_saveDirectory;
}

void SaveSystem::DumpContextStack() const
{
}
