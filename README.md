[![Build Status](https://github.com/neonrust/RapidGL/actions/workflows/cpp_cmake.yml/badge.svg)](https://github.com/neonrust/RapidGL/actions)


# RapidGL
Framework for rapid OpenGL demos prototyping.

Forked from https://github.com/tgalaj/RapidGL but detached so I can use Git LFS.

----

This framework consists of two major parts:
* **Core library** - which is used as a static library for all examples. Source files are located in ```src/core```.
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

- Clustered shading (a.k.a. forward+, I think?), PBR.
- Light types: point, directional, spot, rectangle, tube, sphere and disc.
- Shadow mapping. Uses one large atlas. Supported by point, directional and spot lights.
  - CSM
  - shadow range compression
  - contact shadows
- Volumetric light scattering; inject + accumulate method (all lights supported, for better or worse).
  

## How to build
After cloning the repository, run one of the *.bat* scripts to generate Visual Studio 2019/2022 solution:

* **setup_vs2019.bat** - to generate VS 2019 solution.
* **setup_vs2022.bat** - to generate VS 2022 solution.

Or run the following command in the root directory to generate project files with the default build system for your system:

```
cmake -B build
```

Either of these approaches will create project files in the *build* directory.

## How to add a new demo using Template Project

The following instructions are also located in ```src/demos/00_template_project/template_project.h```.

To begin creating a new demo using RapidGL framework follow these steps:

1) Create new directory in ```src/demos/<your_dir_name>```.
2) Add the following line to ```src/demos/CMakeLists.txt```: ```add_subdirectory(<your_dir_name>)```.
3) Copy contents of ```src/demos/00_template_project``` to ```src/demos/<your_dir_name>```.
4) Change target name of your demo in ```src/demos/<your_dir_name>/CMakeLists.txt``` from ```set(DEMO_NAME "00_template_project")``` to ```set(DEMO_NAME "your_demo_name")```.
5) (Re-)Run CMake

**Notes:** After changing class name from e.g. TemplateProject to something else, update ```main.cpp``` in ```<your_dir_name>``` accordingly.


### Clustered Forward Shading
Clustered Forward Shading implementation based on *[Clustered Deferred and Forward Shading (2012)](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf) (Ola Olsson, Markus Billeter, Ulf Assarsson)* and [Jeremiah van Oosten's DX12 demo](https://github.com/jpvanoosten/VolumeTiledForwardShading).

For light culling, I used view aligned AABB grid. During the lighting stage, only the visible clusters are taken into account (it greatly improves the performance as we limit the searching domain). 

The demo is able to render ~100k lights at interactive frame rates (> 30FPS) on NVidia GTX 1660 Ti with Max-Q Design at 1920x1080 resolution.

To further improve the performance, you may look into adding lights BVH structure as described in O. Olsson's paper. [Jeremiah van Oosten's DX12 demo](https://github.com/jpvanoosten/VolumeTiledForwardShading) includes the fully optimized version of clustered shading algorithm. I highly recommend looking into it.

**19/07/2023 update:**
The demo now also supports the LTC Area Lights based on Eric Heitz's paper *[Real-Time Polygonal-Light Shading with Linearly Transformed Cosines (2016)](https://eheitzresearch.wordpress.com/415-2/)*. 
The area lights are also being culled by the clustered shading algorithm.

<img src="screenshots/27_clustered_shading0.png" width="50%" height="50%" alt="Clustered Forward Shading implementation." />

<img src="screenshots/27_clustered_shading1.png" width="50%" height="50%" alt="Clustered Forward Shading implementation with Area Lights." />
