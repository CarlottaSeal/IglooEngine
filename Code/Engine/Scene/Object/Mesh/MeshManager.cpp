#include "MeshManager.h"

#include <filesystem>

#include "Engine/Core/StaticmeshCache.h"
#include "Engine/Scene/Scene.h"

MeshManager::MeshManager(Scene* scene)
    : m_scene(scene)
{
}

MeshManager::~MeshManager()
{
    for (auto& pair : m_loadedMeshes)
    {
        delete pair.second;
        pair.second = nullptr;
    }
    m_loadedMeshes.clear();
}

StaticMesh* MeshManager::GetOrLoadMesh(const std::string& name, const std::string& path)
{
    if (m_loadedMeshes.find(name) != m_loadedMeshes.end())
    {
        DebuggerPrintf("[MeshManager] Returning cached mesh: %s\n", name.c_str());
        return m_loadedMeshes[name];
    }

    if (m_nextMeshIndex >= MAX_MESH_COUNT)
    {
        DebuggerPrintf("[MeshManager] ERROR: Cannot create more meshes! (MAX_MESH_COUNT=%d)\n", MAX_MESH_COUNT);
        return nullptr;
    }

    StaticMesh* mesh = nullptr;
    bool loadedFromCache = false;

#ifdef ENGINE_DX12_RENDERER
    std::string cacheFileName = StaticMeshCache::GetCacheFileName(path);
    std::string cachePath = "MeshCache/" + cacheFileName;
    std::string sourcePath = path + ".xml";
    
    if (StaticMeshCache::IsCacheValid(cachePath, sourcePath))
    {
        std::vector<uint8_t> buffer;
        if (g_theSaveSystem->LoadBinary(cachePath, buffer))
        {
            StaticMeshCache cache;
            size_t offset = 0;
            if (cache.LoadFromBinary(buffer, offset))
            {
                mesh = new StaticMesh();
                
                if (cache.ApplyToMesh(mesh, m_nextMeshIndex))
                {
                    //DebuggerPrintf("[MeshManager] Successfully loaded '%s' from cache\n", name.c_str());
                    loadedFromCache = true;
                }
                else
                {
                    //DebuggerPrintf("[MeshManager] Cache apply failed for '%s', falling back to normal load\n", 
                    //               name.c_str());
                    delete mesh;
                    mesh = nullptr;
                }
            }
        }
    }
#endif

    if (!mesh)
    {
        mesh = new StaticMesh((Renderer*)m_scene->m_config.m_renderer, path, true);
        mesh->GenerateOrGetSDF(m_scene->m_config.m_renderer, 1.f, m_nextMeshIndex);

#ifdef ENGINE_DX12_RENDERER
        StaticMeshCache cache;
        cache.CaptureFromMesh(mesh, 1.0f);
        
        std::vector<uint8_t> buffer;
        cache.SaveToBinary(buffer);
        
        //SaveSystem::EnsureDirectoryForFile(cachePath);
        if (g_theSaveSystem->SaveBinary(cachePath, buffer))
        {
            DebuggerPrintf("[MeshManager] Cache saved: %s (%zu bytes)\n", 
                           cachePath.c_str(), buffer.size());
        }
#endif
    }
    m_loadedMeshes[name] = mesh;
    m_nextMeshIndex++;
    return mesh;
}

void MeshManager::ClearMeshCache()
{
    if (!SaveSystem::DirectoryExists("Data/MeshCache"))
    {
        DebuggerPrintf("[MeshManager] Cache directory doesn't exist\n");
        return;
    }
    namespace fs = std::filesystem;
    try
    {
        int removedCount = 0;
        for (const auto& entry : fs::directory_iterator("Data/MeshCache"))
        {
            if (entry.path().extension() == ".mcache")
            {
                fs::remove(entry.path());
                removedCount++;
            }
        }
        DebuggerPrintf("[MeshManager] Cleared %d mesh cache files\n", removedCount);
    }
    catch (const std::exception& e)
    {
        ERROR_RECOVERABLE(Stringf("Failed to clear mesh cache: %s", e.what()).c_str());
    }
}

void MeshManager::InvalidateMeshCache(const std::string& name)
{
    auto it = m_loadedMeshes.find(name);
    if (it != m_loadedMeshes.end())
    {
        std::string cacheFileName = StaticMeshCache::GetCacheFileName(it->second->m_filePath);
        std::string cachePath = "MeshCache/" + cacheFileName;
        
        namespace fs = std::filesystem;
        if (fs::exists(cachePath))
        {
            fs::remove(cachePath);
            DebuggerPrintf("[MeshManager] Invalidated cache for: %s\n", name.c_str());
        }
        else
        {
            DebuggerPrintf("[MeshManager] No cache file to invalidate for: %s\n", name.c_str());
        }
    }
    else
    {
        DebuggerPrintf("[MeshManager] Mesh '%s' not found in loaded meshes\n", name.c_str());
    }
}