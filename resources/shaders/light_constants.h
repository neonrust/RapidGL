#pragma once

#define MAX_POINT_LIGHTS           2048
#define MAX_SPOT_LIGHTS             256
#define MAX_RECT_LIGHTS              32
#define MAX_TUBE_LIGHTS              32
#define MAX_SPHERE_LIGHTS            32
#define MAX_DISC_LIGHTS              32

#define MAX_POINT_SHADOW_CASTERS    256
#define MAX_SPOT_SHADOW_CASTERS      32
#define MAX_RECT_SHADOW_CASTERS       2

// a "normal" number of clusters might be 20x12x58 13920
#define CLUSTER_MAX_COUNT           20480
#define CLUSTER_MAX_LIGHTS            256
#define CLUSTER_INDEX_MAX         9999999

#define CLUSTER_AVERAGE_LIGHTS         32


// 'type_flags' bits:
//
//     31                                     0
//      .... .... .... SSSS SSSS SSSS CV.2 TTTT
//
//   . = unused
//   T = light type (4 bits)
//   2 = two-sided (1 bit); rect & disc lights
//   C = shadow caster (1 bit); point, dir & spot (likely)
//   V = volunetric fog (1 bit)
//   S = shadw slots info index (12 bits; 4095 values, 0xfff = no slot) - index into SSBO_BIND_SHADOW_SLOTS_INFO)
//
#define LIGHT_TYPE_MASK          0x0fu
#define LIGHT_TYPE_POINT         0x00u
#define LIGHT_TYPE_DIRECTIONAL   0x01u
#define LIGHT_TYPE_SPOT          0x02u
#define LIGHT_TYPE_RECT          0x03u
#define LIGHT_TYPE_TUBE          0x04u
#define LIGHT_TYPE_SPHERE        0x05u
#define LIGHT_TYPE_DISC          0x06u
#define LIGHT_TYPE__COUNT        (1 + LIGHT_TYPE_DISC)

#define GET_LIGHT_TYPE(light)      ((light).type_flags & LIGHT_TYPE_MASK)
#define IS_LIGHT_TYPE(light, type) (GET_LIGHT_TYPE(light) == (type))
#define IS_POINT_LIGHT(light)      (IS_LIGHT_TYPE(light, LIGHT_TYPE_POINT))
#define IS_DIR_LIGHT(light)        (IS_LIGHT_TYPE(light, LIGHT_TYPE_DIRECTIONAL))
#define IS_SPOT_LIGHT(light)       (IS_LIGHT_TYPE(light, LIGHT_TYPE_SPOT))
#define IS_RECT_LIGHT(light)       (IS_LIGHT_TYPE(light, LIGHT_TYPE_RECT))
#define IS_TUBE_LIGHT(light)       (IS_LIGHT_TYPE(light, LIGHT_TYPE_TUBE))
#define IS_SPHERE_LIGHT(light)     (IS_LIGHT_TYPE(light, LIGHT_TYPE_SPHERE))
#define IS_DISC_LIGHT(light)       (IS_LIGHT_TYPE(light, LIGHT_TYPE_DISC))

#define LIGHT_DOUBLE_SIDED          0x10u   // rect & disc lights

#define LIGHT_SHADOW_CASTER      0x00000080u
#define LIGHT_SHADOW_MASK        0x000fff00u  // max 1023 shadw-casting lights
#define LIGHT_SHADOW_SHIFT       8u
#define LIGHT_VOLUMETRIC         0x00000040u

#define LIGHT_NO_SHADOW          0xfffu

#define GET_SHADOW_IDX(light)      (((light).type_flags & LIGHT_SHADOW_MASK) >> LIGHT_SHADOW_SHIFT)
#ifdef __cplusplus
void SET_SHADOW_IDX(auto &light, auto idx)
{
	assert(idx <= LIGHT_NO_SHADOW);
	light.type_flags = (light.type_flags & ~LIGHT_SHADOW_MASK) | uint32_t(idx << LIGHT_SHADOW_SHIFT);
}
#endif
#define CLR_SHADOW_IDX(light)      SET_SHADOW_IDX(light, LIGHT_NO_SHADOW)

#define IS_SHADOW_CASTER(light)    (((light).type_flags & LIGHT_SHADOW_CASTER) > 0)
#define IS_VOLUMETRIC(light)       (((light).type_flags & LIGHT_VOLUMETRIC) > 0)
#define IS_DOUBLE_SIDED(light)     (((light).type_flags & LIGHT_DOUBLE_SIDED) > 0)

#define FROXEL_GRID_W      160
#define FROXEL_GRID_H      90
#define FROXEL_GRID_D      64
// possible resolutions: 240x135 224x126 208x117 192x108 176x99 160x90 144x81 128x62 112x63 95x54 80x45
#define FROXEL_THREADS_X   8
#define FROXEL_THREADS_Y   8
#define FROXEL_THREADS_Z   1

#define FROXELS_PER_TILE   10
#define FROXEL_TILE_AVG_LIGHTS 64
#define FROXEL_TILE_MAX_LIGHTS 256
