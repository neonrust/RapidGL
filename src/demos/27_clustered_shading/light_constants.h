#pragma once

#define MAX_POINT_LIGHTS           2048
#define MAX_SPOT_LIGHTS             256
#define MAX_AREA_LIGHTS              32

#define MAX_POINT_SHADOW_CASTERS    256
#define MAX_SPOT_SHADOW_CASTERS      32
#define MAX_AREA_SHADOW_CASTERS       2

// a "normal" number of clusters might be 20x12x58 13920
#define CLUSTER_MAX_COUNT           20480
#define CLUSTER_MAX_LIGHTS            256
#define CLUSTER_INDEX_MAX         9999999

#define CLUSTER_AVERAGE_LIGHTS         32

#define LIGHT_TYPE_MASK          0x0fu
#define LIGHT_TYPE_POINT         0x00u
#define LIGHT_TYPE_DIRECTIONAL   0x01u
#define LIGHT_TYPE_SPOT          0x02u
#define LIGHT_TYPE_AREA          0x03u
#define LIGHT_TYPE_TUBE          0x04u
#define LIGHT_TYPE_SPHERE        0x05u
#define LIGHT_TYPE_DISC          0x06u

#define IS_POINT_LIGHT(light)    ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_POINT)
#define IS_DIR_LIGHT(light)      ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_DIRECTIONAL)
#define IS_SPOT_LIGHT(light)     ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_SPOT)
#define IS_AREA_LIGHT(light)     ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_AREA)
#define IS_TUBE_LIGHT(light)     ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_TUBE)
#define IS_SPHERE_LIGHT(light)   ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_SPHERE)
#define IS_DISC_LIGHT(light)     ((light.type_flags & LIGHT_TYPE_MASK) == LIGHT_TYPE_DISC)

#define LIGHT_TWO_SIDED          0x10u   // area & disc lights

// max 256 shadw-casting lights?
#define LIGHT_SHADOW_CASTER      0x008000u
#define LIGHT_SHADOW_MASK        0xff0000u
#define LIGHT_SHADOW_SHIFT       16u
#define LIGHT_NO_SHADOW          0xffu

#define GET_SHADOW_IDX(light)      ((light.type_flags & LIGHT_SHADOW_MASK) >> LIGHT_SHADOW_SHIFT)
#define SET_SHADOW_IDX(light, idx) (light.type_flags = (light.type_flags & ~LIGHT_SHADOW_MASK) | uint32_t(idx << LIGHT_SHADOW_SHIFT))
#define CLR_SHADOW_IDX(light)      SET_SHADOW_IDX(light, 0xffu)

#define IS_SHADOW_CASTER(light)    ((light.type_flags & LIGHT_SHADOW_CASTER) > 0)
