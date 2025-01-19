[![Build Status](https://github.com/neonrust/RapidGL/actions/workflows/cpp_cmake.yml/badge.svg)](https://github.com/Shot511/RapidGL/actions)


# RapidGL
Framework for rapid OpenGL demos prototyping.

Forked from https://github.com/tgalaj/RapidGL but detached so I can use Git LFS.

This framework consists of two major parts:
* **Core library** - which is used as a static library for all examples. Source files are located in ```src/core```.
* **Demos**. Source files are located in ```src/demos```.

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

## Examples
All of the demos are available in ```src/demos```.

### Template Project
<img src="screenshots/00_template_project.png" width="50%" height="50%" alt="Template Project" />

### Simple Triangle
<img src="screenshots/01_simple_triangle.png" width="50%" height="50%" alt="Simple Triangle" />

### Simple 3D
This demo shows how to create camera, load models, generate primitives using built-in functions and add textures for specific objects.

<img src="screenshots/02_simple_3d.png" width="50%" height="50%" alt="Simple 3D" />

### Lighting
This demo presents implementation of Blinn-Phong shading model for directional, point and spot lights.

<img src="screenshots/03_lighting.png" width="50%" height="50%" alt="Lighting" />

### Multitextured terrain
This demo presents implementation of multitextured terrain. It uses a blend map (for varying X-Z texturing) and slope based texturing (for texturing the slopes).

<img src="screenshots/04_terrain.png" width="50%" height="50%" alt="Multitextured terrain" />

### Toon shading
This demo presents implementation of various toon shading methods (Simple, Advanced, Simple with Rim, Twin Shade) with different outline rendering methods (Stencil, Post-Process).

<img src="screenshots/05_toon_outline.png" width="50%" height="50%" alt="Toon shading" />

### Simple Fog
Implementation of a simple fog rendering. Three modes are available: linear, exp, exp2.

<img src="screenshots/06_simple_fog.png" width="50%" height="50%" alt="Simple Fog" />

### Alpha Cutout
This demo shows implementation of an alpha cutout using fragments discarding.

<img src="screenshots/07_alpha_cutout.png" width="50%" height="50%" alt="Alpha Cutout" />

### Environment mapping
Implementation of dynamic and static environment mapping (light reflection and refraction).

<img src="screenshots/08_enviro_mapping.png" width="50%" height="50%" alt="Environment mapping" />

### Projected texture
Demo presents projecting a texture onto a surface.

<img src="screenshots/09_projected_texture.png" width="50%" height="50%" alt="Projected texture" />

### Postprocessing filters
Negative, edge detection (Sobel operator) and Gaussian blur filters demo.

<img src="screenshots/10_postprocessing_filters.png" width="50%" height="50%" alt="Postprocessing filters" />

### Geometry Shader: Point Sprites
Demo presents generation of quad sprites from points data using Geometry Shader.

<img src="screenshots/11_gs_point_sprites.png" width="50%" height="50%" alt="Geometry Shader: Point Sprites" />

### Geometry Shader: Wireframe on top of a shaded model
<img src="screenshots/12_gs_wireframe.png" width="50%" height="50%" alt="Geometry Shader: Wireframe on top of a shaded model" />

### Tessellation - 1D
<img src="screenshots/13_ts_curve.png" width="50%" height="50%" alt="Tessellation - 1D" />

### Tessellation - 2D
<img src="screenshots/14_ts_quad.png" width="50%" height="50%" alt="Tessellation - 2D" />

### PN Triangles Tessellation with Level of Detail
This demo implements Point-Normal tessellation algorithm (see *main.cpp* for references) with depth based level of detail (NOTE: works for each mesh with vertex normals).

<img src="screenshots/15_ts_lod.png" width="50%" height="50%" alt="PN Triangles Tessellation with Level of Detail" />

### Procedural noise textures
<img src="screenshots/16_noise.png" width="50%" height="50%" alt="Procedural noise textures" />

### Surface animation with vertex displacement
<img src="screenshots/17_vertex_displacement.png" width="50%" height="50%" alt="Surface animation with vertex displacement" />

### Simple particle system using Transform Feedback
Available presets: fountain, fire and smoke.

<img src="screenshots/18_simple_particles_system.png" width="50%" height="50%" alt="Simple particle system using Transform Feedback" />

### Particle system using instanced meshes with the Compute Shader
<img src="screenshots/19_instanced_particles_compute_shader.png" width="50%" height="50%" alt="Particle system using instanced meshes with the Compute Shader" />

### Mesh skinning
This demo presents simple model animation system using Assimp. There are two skinning methods available: Linear Blend Skinning and Dual Quaternion Blend Skinning.

<img src="screenshots/20_mesh_skinning.png" width="50%" height="50%" alt="Mesh skinning" />

### Order Independent Transparency (OIT) with MSAA
Order Independent Transparency using linked lists (per pixel) with MSAA.

<img src="screenshots/21_oit.png" width="50%" height="50%" alt="Order Independent Transparency (OIT)" />

### Physically Based Rendering (PBR)
Including directional and punctual lights (spot and point) with square falloff attenuation. The demo supports textured and non-textured objects.

<img src="screenshots/22_pbr.png" width="50%" height="50%" alt="Physically Based Rendering (PBR)" />

### Geometry Shader: Face Extrusion
<img src="screenshots/23_gs_face_extrusion.gif" width="50%" height="50%" alt="Physically Based Rendering (PBR)" />

### Percentage Closer Soft Shadows (PCSS)
<img src="screenshots/24_pcss.png" width="50%" height="50%" alt="Percentage Closer Soft Shadows" />

### Cascaded Percentage Closer Soft Shadows (CPCSS)
<img src="screenshots/25_cascaded_pcss.png" width="50%" height="50%" alt="Cascaded Percentage Closer Soft Shadows" />

### Bloom
Bloom implementation based on Call of Duty: Advanced  Warfare [Jimenez14](http://goo.gl/eomGso). Implemented using Compute Shaders with shared memory utilization for improved performance. Full bloom pass (1920x1080) takes ~0.75ms on NVidia GTX 1660 Ti with Max-Q Design (according to NVIDIA Nsight Graphics).

<img src="screenshots/26_bloom.png" width="50%" height="50%" alt="Bloom implementation based on Call of Duty: Advanced Warfare." />

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
