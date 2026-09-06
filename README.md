[![Build Status](https://github.com/neonrust/RapidGL/actions/workflows/cpp_cmake.yml/badge.svg)](https://github.com/neonrust/RapidGL/actions)


# RapidGL
Framework for rapid OpenGL demos prototyping.

Forked from https://github.com/tgalaj/RapidGL but detached so I can use Git LFS.

----

This framework consists of two major parts:
* **Core library** - which is used as a static library for all apps. Source files are located in ```src/core```.
* **Application**. Source files are located in ```src/apps```.

3rd-party dependencies:
- assimp
- glad   (vendored)
- glfw
- glm
- imgui
- stb_image  (vendored)
- libjxl


### Rendering features

- Clustered light culling (a.k.a. forward+, I think?)
- PBR shading
- Light types: point, directional, spot, rectangle, tube, sphere and disc.
- Shadow mapping. Uses one large atlas. Supports point, directional and spot lights.
  - CSM
  - shadow range compression
  - contact shadows
- Volumetric light scattering; inject + accumulate method (all lights supported, with varying degrees of realism).
- Ambient Occlusion (GTAO) - shader lifted from box3d
- BVH-based scene culling (not really rendering, but it's pretty closely related) - BVH tree lifted from box3d


## Building

After cloning the repository, run the following command in the root directory to generate project files with the default build system for your system:

```
cmake -B build
```

Will result in a buildable "solution" in the *build* directory.

### Clustered Forward Shading

Clustered Forward Shading implementation based on 
*[Clustered Deferred and Forward Shading (2012)](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf) (Ola Olsson, Markus Billeter, Ulf Assarsson)* 
and [Jeremiah van Oosten's DX12 demo](https://github.com/jpvanoosten/VolumeTiledForwardShading).

For light culling, I used view aligned AABB grid. During the lighting stage. 
Only the visible clusters are taken into account (it greatly improves the performance as we limit the searching domain). 
Also, the contributing lights read back from the GPU to determine if its shadow map rendering may be skipped.

The demo is able to render ~100k lights at interactive frame rates (> 30FPS) on NVidia GTX 1660 Ti with Max-Q Design at 1920x1080 resolution.

<img src="screenshots/27_clustered_shading0.png" width="50%" height="50%" alt="Clustered Forward Shading implementation." />

<img src="screenshots/27_clustered_shading1.png" width="50%" height="50%" alt="Clustered Forward Shading implementation with Area Lights." />
