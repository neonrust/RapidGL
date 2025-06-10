#pragma once

#define MAX_POINT_LIGHTS           2048
#define MAX_SPOT_LIGHTS             256
#define MAX_AREA_LIGHTS              32

#define MAX_POINT_SHADOW_CASTERS    256
#define MAX_SPOT_SHADOW_CASTERS      32
#define MAX_AREA_SHADOW_CASTERS       2

// a "normal" number of clusters might be 20x12x58 13920
#define CLUSTER_MAX_COUNT           20480
#define CLUSTER_MAX_POINT_LIGHTS      256
#define CLUSTER_MAX_SPOT_LIGHTS        32
#define CLUSTER_MAX_AREA_LIGHTS         8
#define CLUSTER_INDEX_MAX         9999999

#define CLUSTER_AVERAGE_LIGHTS         32

#define CLUSTER_MAX_LIGHTS            296  // CLUSTER_MAX_POINT_LIGHTS + CLUSTER_MAX_SPOT_LIGHTS + CLUSTER_MAX_AREA_LIGHTS

#define LIGHT_TYPE_POINT       0
#define LIGHT_TYPE_SPOT        1
#define LIGHT_TYPE_AREA        2
#define LIGHT_TYPE_DIRECTIONAL 3

#define LIGHT_SHADOW_CASTER       0x10
#define LIGHT_TWO_SIDED           0x20   // area lights
