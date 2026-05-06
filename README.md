# Igloo Engine

A modular C++/DirectX 12 game engine with a real-time global illumination system, deferred rendering pipeline, custom math library, UI framework, multi-threaded job system, and cross-format serialization.

Built as the foundation for [LuminaGI](https://github.com/CarlottaSeal/LuminaGI) — a real-time GI thesis project at SMU Guildhall. The Vulkan rendering backend has been split out into its own repository, [Tessera](https://github.com/CarlottaSeal/Tessera), where it continues to evolve as a tile-deferred + forward-A/B Vulkan engine; this Igloo repo is now the DirectX 12 backend.

![Protogame3D](https://github.com/user-attachments/assets/277793ea-8406-4806-b880-9dcf2a65661f)
*Protogame3D*

![Math & Physics](https://github.com/user-attachments/assets/c177a184-b218-4391-9e1a-f79509cf52f9)
*Math & Physics Library*

![DevConsole](https://github.com/user-attachments/assets/616d295b-65f1-457e-ab72-7e223c38f5bc)
*DevConsole*

![UI System](https://github.com/user-attachments/assets/4e34b193-666d-4e40-982a-d1261fbc5dc5)
*Window & UI System*

---

## Features

- **DirectX 12 Renderer** — Deferred GBuffer pipeline, instanced indexed drawing, async compute queue, descriptor heap management, 128 MB ring buffers for vertex/index data
- **Real-Time Global Illumination** — Surface cache atlas, screen-space probe system, surface radiosity, voxel irradiance volume, software SDF sphere tracing (per-mesh SDFs are baked at load time using BVH-accelerated point-triangle distance queries; runtime tracing is texture-only, no BVH)
- **Shadow System** — Directional PCF shadow maps (2048²) + omnidirectional point light cube shadow arrays (512² × 6 faces, up to 4 lights)
- **DXR Support** — Bottom-level (per-mesh BLAS) and top-level (TLAS) acceleration structures for hardware ray tracing
- **Math Library** — Vec2/3/4, Mat44, AABB2/3, OBB3, Sphere, Frustum, Plane, Capsule, Euler angles, Hermite/Bezier splines, BSP tree, BVH
- **UI Framework** — Hierarchical element system: Button, Checkbox, Slider, ProgressBar, Text, Sprite, Panel, Canvas, DialogueSystem
- **Job System** — Worker + I/O thread types, thread-safe pending/executing/completed queues
- **Save System** — Binary, XML, JSON, CSV, and text formats; RLE compression; slot-based quick save/load
- **Input** — Keyboard, Xbox controller (XInput), analog joystick, tablet/stylus (Wintab)
- **Audio** — FMOD integration
- **Mesh Loading** — OBJ and glTF/GLB via cgltf; BVH construction and per-mesh SDF generation at load time
- **Particle System** — Configurable emitters with presets (fire, smoke, sparks, explosion)
- **Dev Tools** — DevConsole, DebugRenderSystem, ImGui + ImPlot integration, 17 GI visualization modes

---

## Architecture

```
Engine/Code/Engine/
├── Audio/                  # FMOD audio system
├── Core/                   # Foundation utilities
│   ├── DevConsole          # In-game debug console
│   ├── EventSystem         # Event dispatching
│   ├── DebugRenderSystem   # Runtime debug drawing
│   ├── StaticMesh          # OBJ/GLB loading, BVH build, SDF generation
│   ├── NetworkSystem       # TCP client/server sockets
│   └── EngineConfig        # GI atlas and feature configuration
├── Input/                  # Keyboard, Xbox controller, tablet
├── Job/                    # Multi-threaded job system
├── Math/                   # Full math library (see below)
├── Renderer/
│   ├── DX12Renderer        # Core DX12 rendering engine
│   ├── Cache/              # Surface cache, shadow passes, screen probes
│   │   ├── SurfaceCache            # 4096² atlas with 6 layer types
│   │   ├── SurfaceCard             # Per-mesh card templates + instances
│   │   ├── CardBVH                 # Legacy: spatial-query class from earlier exploration; runtime path uses linear FindBestCard over the 6 cards per mesh (O(6) constant)
│   │   ├── DirectionalShadowPass   # PCF directional shadows
│   │   ├── PointLightShadowPass    # Cube array point light shadows
│   │   ├── SurfaceRadiosityCache   # Radiosity probe grid on surface cache
│   │   ├── ScreenProbeFinalGather  # Screen-space probe final gather
│   │   └── CombineSurfaceCache     # Combine all cache layers
│   ├── GI/                 # GI orchestration and GBuffer
│   │   ├── GISystem                # Top-level GI controller
│   │   ├── GBufferData             # Albedo, Normal, Material, WorldPos, Depth
│   │   └── GIVisualization         # 17 debug visualization modes
│   └── DXR/                # DirectX Ray Tracing acceleration structures
├── Save/                   # Multi-format serialization + RLE compression
├── Scene/
│   ├── Scene               # Entity container, GI registration, dirty tracking
│   ├── Object/
│   │   ├── SceneObject     # Base: transform, visibility, moved detection
│   │   ├── MeshObject      # Mesh entity with card instance management
│   │   └── LightObject     # Directional / Point / Spot / Area lights
│   └── SDF/
│       ├── SDFGenerator    # Per-mesh SDF volume generation
│       ├── VoxelScene      # Global SDF composition
│       └── SDFCommon       # GPU structures, GLOBAL_SDF_RESOLUTION = 64
├── Tool/                   # ParticleSystem with fire/smoke/spark/explosion presets
├── UI/                     # Hierarchical UI framework
└── Window/                 # Win32 window management
```

---

## Renderer

### DX12 Pipeline

The `DX12Renderer` manages the full DirectX 12 device lifecycle: swap chain, command queues (graphics + async compute), descriptor heaps (RTV, DSV, CBV/SRV), per-frame command allocators, and fence-based synchronization.

**Constant buffer layout (shared across all shaders):**

| Slot | Name | Contents |
|------|------|----------|
| b0 | FrameConstants | Time, debug int/float |
| b1 | CameraConstants | View, projection, inverse matrices, camera position |
| b2 | ModelConstants | World matrix, tint color |
| b3 | MaterialConstants | Texture IDs |
| b4 | LightConstants | Sun direction/color + point light array (up to 15) |
| b5 | ShadowConstants | Directional VP matrices + point light indices/far planes |
| b6 | SurfaceRadiosityConstants | Radiosity probe parameters |
| b7 | ScreenProbeFinalGatherConstants | Probe gather parameters |
| b8–b10 | Card Capture Constants | Per-card capture data |
| b11 | GIVisualizationConstants | Debug mode parameters |
| b12 | CompositeConstants | Final composite parameters |

**Per-frame render pass order:**

| Order | Pass | Type | Trigger |
|-------|------|------|---------|
| 1 | Directional Shadow Map (2048²) | Rasterization | On sun change |
| 2 | Point Light Cube Shadow (512² × 24) | Rasterization | On light or geometry change (Execute called every frame, internally early-outs unless dirty) |
| 3 | GBuffer | Rasterization | Every frame |
| 4 | Card Capture (dirty only) | Rasterization | On geometry change (per-card dirty flag) |
| 5 | DirectLight Update | Compute | On light change (sun or any point light) |
| 6 | Build Voxel Visibility | Compute | On geometry change (gated by `m_needsRebuildGlobalLighting`) |
| 7 | Inject Voxel Lighting | Compute | Every frame |
| 8 | Surface Radiosity (Trace + Filter + SH) | Compute | Force dispatch on lighting dirty; otherwise every 10th settle frame; converges and stops after ~300 stable frames |
| 9 | Combine Surface Cache | Compute | On lighting dirty (gated by `m_combinedDirty`, set whenever DirectLight or radiosity wrote new data; skipped when both stable) |
| 10 | Screen Probes (11 passes) | Compute | Every frame |
| 11 | Screen-Space Temporal Filter | Compute | Every frame |
| 12 | Final Composite | Full-screen PS | Every frame |

### GBuffer Layout

| Target | Format | Contents |
|--------|--------|----------|
| Albedo | RGBA8 | Surface reflectance color |
| Normal | RGBA16F | World-space normal (packed) |
| Material | RGBA8 | R: Roughness, G: Metallic, B: AO, A: ObjectID |
| WorldPos | RGBA32F | World-space position |
| Depth | D32F | Hardware depth buffer |

### Surface Cache

A 4096×4096 `Texture2DArray` with 6 layers, capturing surface attributes via axis-aligned card projection. Because `Texture2DArray` requires a single shared format across all slices, every layer is bound as `R16G16B16A16_FLOAT` even when a layer's content semantically only needs RGBA8:

| Layer | Contents | Stored Format | Semantic Format |
|-------|----------|---------------|-----------------|
| 0 | Albedo | RGBA16F | RGBA8 |
| 1 | World-Space Normal | RGBA16F | RGBA16F |
| 2 | Material (roughness/metallic/AO) | RGBA16F | RGBA8 |
| 3 | Direct Light (HDR irradiance) | RGBA16F | RGBA16F |
| 4 | Indirect Light from radiosity (HDR irradiance) | RGBA16F | RGBA16F |
| 5 | Combined Light (HDR outgoing radiance) | RGBA16F | RGBA16F |

Total atlas footprint is `4096² × 8 bytes × 6 = 768 MiB`. Splitting Albedo and Material out as separate `R8G8B8A8_UNORM` Texture2D resources is queued as a future memory optimization (~233 MiB savings, no quality cost).

Each mesh generates six axis-aligned **surface cards** (±X, ±Y, ±Z). Cards store a 128-bit light mask packed as `uint32_t LightMask[4]` to efficiently skip unaffected lights during shader-side relighting. When a light moves, `Scene::RegisterLightInfluence` rebuilds the mask via AABB-vs-AABB overlap (plus a cone cosine test for spot lights, since their AABB over-bounds the actual cone). Per-card distance priority (`1 / (1 + distance × 0.1)`) is computed for ordering; a per-frame card budget is design intent listed as future work, currently the DirectLight Update dispatch is bulk over the active card set with the LUT below short-circuiting empty atlas tiles.

A 64×64 `R32_UINT` **tile→card-index LUT** (16 KB in the default 64-pixel-tile config; dimensions track `atlas_size / m_tileSize`) replaces the original O(n) per-thread linear card search in DirectLight Update with a single `Texture2D.Load`, dropping per-light-change cost from ~33 ms to ~2 ms. The same LUT is reused as an O(1) tile-occupancy early-out by the surface radiosity passes.

### Screen Probe System (11 compute passes)

| Pass | Shader | Key Operation |
|------|--------|---------------|
| 1. Probe Placement | ProbePlacement.hlsl | 16×16 pixel grid (matches Lumen's `ScreenProbeDownsampleFactor=16`), depth-based world position |
| 2. BRDF PDF | BRDFPDFGeneration.hlsl | Analytic SH projection of clamped cosine lobe (Lambertian) |
| 3. Lighting PDF | LightingPDFGeneration.hlsl | 32 Fibonacci hemisphere samples reprojected into previous frame, SH-projected by cosine-weighted luminance |
| 4. Sample Directions | GenerateSampleDirections.hlsl | 64 rays/probe on fixed octahedral 8×8 grid; per-direction PDF combines BRDF + lighting via MIS power heuristic (β=2) |
| 5. Mesh SDF Trace | MeshSDFTrace.hlsl | Short-range SDF sphere tracing (up to 100 units) |
| 6. Voxel SDF Trace | VoxelSDFTrace.hlsl | Long-range global SDF trace (up to 500 units) |
| 7. Radiance Composite | RadianceComposite.hlsl | Voxel sample at hit + per-ray distance AO (`saturate(closestHitDist / AO_RADIUS)`, packed in alpha) |
| 8. Spatial Filter | SpatialFilter.hlsl | 4-neighbor cross bilateral filter (depth + normal weighted) |
| 9. Oct Irradiance | OctIrradiance.hlsl | L2 SH (9 coeff/channel) low-pass projection + reconstruction |
| 10. Final Gather | FinalGather.hlsl | 5-probe bilateral blend (depth + normal weights) → per-pixel irradiance + AO modulation `lerp(1, ao, AOStrength=0.5)` on indirect only |
| 11. Screen Temporal | ScreenTemporal.hlsl | Temporal blend (α=0.05) for stability |

---

## Math Library

| Category | Types |
|----------|-------|
| Vectors | `Vec2`, `Vec3`, `Vec4`, `IntVec3`, `IntVec4` |
| Matrices | `Mat44` (column-major, full transform ops) |
| Rotation | `EulerAngles`, quaternion utilities |
| Bounds | `AABB2`, `AABB3`, `OBB3`, `Sphere`, `Frustum` (6 planes), `Plane3` |
| 2D Primitives | `Disc2D`, `Capsule2D` |
| Curves | `LinearCurve1D`, `PiecewiseCurve1D`, Cubic Hermite spline |
| Spatial | `BSPTree2D`, `BVHTree2D`, CPU `BVH` + `GPUBVHNode` |
| Utility | `MathUtils`, `RandomNumberGenerator` |

---

## Third-Party Libraries

| Library | Purpose |
|---------|---------|
| [ImGui](https://github.com/ocornut/imgui) | Immediate-mode debug GUI |
| [ImPlot](https://github.com/epezent/implot) | GPU timing graphs via ImGui |
| [TinyXML2](https://github.com/leethomason/tinyxml2) | XML parsing and writing |
| [stb_image](https://github.com/nothings/stb) | PNG/JPG/BMP image loading and writing |
| [cgltf](https://github.com/jkuhlmann/cgltf) | glTF 2.0 / GLB mesh loading |
| [FMOD](https://www.fmod.com/) | Audio playback |
| [Wintab](https://developer-docs.wacom.com/) | Tablet/stylus input |
| d3dx12 | DirectX 12 helper headers |

---

## Build Requirements

- **OS**: Windows 10/11
- **IDE**: Visual Studio 2022
- **Graphics API**: DirectX 12 (Feature Level 12_0 minimum; 12_2 for full DXR)
- **GPU**: Any DX12-capable GPU (DXR optional)
- **Audio**: FMOD Studio API (place in `Code/ThirdParty/fmod/`)

Projects using this engine should place it as a sibling directory named `Engine`:
```
/YourProject/
/Engine/        ← this repo
```
