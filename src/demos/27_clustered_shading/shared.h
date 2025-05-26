// Type definitions shared between C++ and GLSL

#include "light_constants.h"  // IWYU pragma: keep

#include "ssbo_binds.h"
#include "generated/shared-structs.h"


// 'feature_flags' bits
#define LIGHT_SHADOW_CASTER       0x01


// struct ShadowMapParams
// {
// 	mat4 view_proj[6];
// 	uvec4 atlas_rect[6];
// };
