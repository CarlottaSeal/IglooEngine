#pragma once
#include <string>
#include <unordered_map>

#include "Engine/Core/StaticMesh.h"

class Scene;
class StaticMesh;

class MeshManager
{
    friend class Scene;
public:
    MeshManager(Scene* scene);
    ~MeshManager();
    StaticMesh* GetOrLoadMesh(const std::string& name, const std::string& path);

    void ClearMeshCache();  
    void InvalidateMeshCache(const std::string& name);
    
protected:
    Scene* m_scene;
    std::unordered_map<std::string, StaticMesh*> m_loadedMeshes; //name->Mesh
    int m_nextMeshIndex = 0;
};
