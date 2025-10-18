#version 460 core
#include "pbr_lighting.glh"
#include "volumetrics.glh"

out vec4 frag_color;

uniform float u_near_z;
uniform uvec3 u_cluster_resolution;
uniform uvec2 u_cluster_size_ss;
uniform float u_log_cluster_res_y;
uniform float u_light_max_distance;
uniform float u_shadow_max_distance;

uniform bool u_debug_cluster_geom;
uniform bool u_debug_clusters_occupancy;
uniform bool u_debug_tile_occupancy;
uniform float u_debug_overlay_blend;

uniform float u_shadow_bias_constant;
uniform float u_shadow_bias_slope_scale;
uniform float u_shadow_bias_distance_scale;
uniform float u_shadow_bias_slope_power;
uniform float u_shadow_bias_scale;

const float s_min_visibility = 1e-3;

// TODO distance-based fg
// uniform float u_fog_thickness;
// uniform vec3 u_fog_color;

const vec3 debug_colors[8] = vec3[]
(
   vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 1),
   vec3(1, 0, 0), vec3(1, 0, 1), vec3(1, 1, 0), vec3(1, 1, 1)
);


layout(std430, binding = SSBO_BIND_LIGHTS) readonly buffer LightsSSBO
{
	GPULight ssbo_lights[];
};

layout(std430, binding = SSBO_BIND_CLUSTER_LIGHT_RANGE) readonly buffer ClusterLightsSSBO
{
	IndexRange ssbo_cluster_lights[];
};

SSBO_ALL_CLUSTER_LIGHTS_ro;

layout(std430, binding = SSBO_BIND_SHADOW_SLOTS_INFO) readonly buffer ShadowParamsSSBO
{
	ShadowSlotInfo ssbo_shadow_slots[];
};

layout(std430, binding = SSBO_BIND_VOLUMETRIC_TILE_LIGHTS_INDEX)
readonly buffer VolTileLightsIndexSSBO
{
	IndexRange ssbo_tile_lights[];  // size = num_tiles
};

vec3  fromRedToGreen(float interpolant);
vec3  fromGreenToBlue(float interpolant);
vec3  heatMap(float interpolant);
vec3 falseColor(float value);
uint computeClusterIndex(uvec3 cluster_coord);
uvec3 computeClusterCoord(vec2 screen_pos, float view_z);

vec3 pointLightVisibility(GPULight light, vec3 world_pos);
vec3 dirLightVisibility(GPULight light);
vec3 spotLightVisibility(GPULight light, vec3 world_pos);
vec3 areaLightVisibility(GPULight light);
vec3 tubeLightVisibility(GPULight light);
vec3 sphereLightVisibility(GPULight light);
vec3 discLightVisibility(GPULight light);

vec2 shadow_atlas_texel_size;

float shadow_low_sampling_distance  = u_shadow_max_distance / 3;
float shadow_mid_sampling_distance  = u_shadow_max_distance / 8;


void main()
{
    vec3 radiance = vec3(0);
    vec3 normal = normalize(in_normal);

	shadow_atlas_texel_size = 1.0 / vec2(textureSize(u_shadow_atlas, 0));

    MaterialProperties material = getMaterialProperties(normal);

    // Locating the cluster we are in
    uvec3 cluster_coord = computeClusterCoord(gl_FragCoord.xy, in_view_pos.z);
    uint cluster_index = computeClusterIndex(cluster_coord);

    IndexRange lights_range = ssbo_cluster_lights[cluster_index];

    uint num_clusters = u_cluster_resolution.x*u_cluster_resolution.y*u_cluster_resolution.z;

    // Calculate the point lights contribution
    uint num_lights = min(lights_range.count, CLUSTER_MAX_LIGHTS);
    // too many lights?
    if(lights_range.count > CLUSTER_MAX_LIGHTS)
		radiance += vec3(1, 0, 0);

    for (uint idx = 0; idx < num_lights; ++idx)
    {
	    uint light_index = all_lights_index[lights_range.start_index + idx];

        GPULight light = ssbo_lights[light_index];

        vec3 visibility = vec3(1);
		vec3 contribution = vec3(0);

       	switch(GET_LIGHT_TYPE(light))
       	{
			case LIGHT_TYPE_POINT:
			{
		        visibility = pointLightVisibility(light, in_world_pos);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcPointLight(light, in_world_pos, material);
			}
			break;

	        case LIGHT_TYPE_DIRECTIONAL:
			{
		        visibility = dirLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcDirectionalLight(light, in_world_pos, material);
			}
			break;

			case LIGHT_TYPE_SPOT:
			{
				visibility = spotLightVisibility(light, in_world_pos);
			    if(visibility.x + visibility.y + visibility.z > s_min_visibility)
    				contribution = calcSpotLight(light, in_world_pos, material);
        	}
         	break;

          	case LIGHT_TYPE_AREA:
           	{
	           	visibility = areaLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcAreaLight(light, in_world_pos, material);
            }
            break;

           	case LIGHT_TYPE_TUBE:
            {
	           	visibility = tubeLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcTubeLight(light, in_world_pos, material);
            }
            break;

           	case LIGHT_TYPE_SPHERE:
            {
	           	visibility = sphereLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcSphereLight(light, in_world_pos, material);
            }
            break;

           	case LIGHT_TYPE_DISC:
            {
	           	visibility = discLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcDiscLight(light, in_world_pos, material);
            }
            break;

            default:
            	// unexpected light type, set an "error" color
	            frag_color = vec4(10, 0, 2, 1);
				return;
		}

       	radiance += visibility * contribution;
    }

    radiance += indirectLightingIBL(in_world_pos, material);
    radiance += material.emission;

    if (u_debug_cluster_geom)
    {
    	vec3 cluster_color = vec3(debug_colors[cluster_coord.z % 8]);
		if((cluster_coord.x + (cluster_coord.y % 2)) % 2 == 0)
		{
			cluster_color *= 0.8;
			radiance = vec3(max(radiance.r, max(radiance.g, radiance.b))) - radiance;
		}

		frag_color = vec4(mix(radiance, cluster_color, u_debug_overlay_blend), 1);
    }
    else if (u_debug_clusters_occupancy)
    {
        if (lights_range.count > 0)
        {
            float normalized_light_count = float(lights_range.count) / 32.f;//float(CLUSTER_MAX_LIGHTS);
            vec3 heat_map_color = falseColor(clamp(normalized_light_count, 0, 1));

            frag_color = vec4(mix(radiance, heat_map_color, u_debug_overlay_blend), 1);
        }
        else
        {
        	// no lights, draw "a pattern"
        	float pattern = uint(gl_FragCoord.x / 8 + gl_FragCoord.y / 5) % 2 == 0 ? 0.3: 0.5;
	    	frag_color = vec4(0.3, pattern, 0.4, 1); // purple is not in the heat map gradient
		}
    }
    else if(u_debug_tile_occupancy)
    {
		uvec2 tile = uvec2(gl_FragCoord.xy) / uvec2(120, 120); // TODO: screen_size / tile_grid = (120, 120)
		uint tile_index = tile.y*tile_grid.x + tile.x;

		IndexRange lights_range = ssbo_tile_lights[tile_index];

		if (lights_range.count > 0)
        {
            float normalized_light_count = float(lights_range.count) / FROXEL_TILE_MAX_LIGHTS;
            vec3 heat_map_color = falseColor(clamp(normalized_light_count, 0, 1));

            frag_color = vec4(mix(radiance, heat_map_color, u_debug_overlay_blend), 1);
        }
        else
        {
        	// no lights, draw "a pattern"
        	float pattern = uint(gl_FragCoord.x / 8 + gl_FragCoord.y / 5) % 2 == 0 ? 0.3: 0.5;
			frag_color = vec4(0.3, pattern, 0.4, 1); // purple is not in the heat map gradient
		}
    }
    else
    {
        // Total lighting
        frag_color = vec4(radiance, 1.0);
    }
}

uint computeClusterIndex(uvec3 cluster_coord)
{
    return cluster_coord.x + (u_cluster_resolution.x * (cluster_coord.y + u_cluster_resolution.y * cluster_coord.z));
}

uvec3 computeClusterCoord(vec2 screen_pos, float view_z)
{
    uint x = uint(screen_pos.x / u_cluster_size_ss.x);
    uint y = uint(screen_pos.y / u_cluster_size_ss.y);

    // View space z is negative (right-handed coordinate system)
    // so the view-space z coordinate needs to be negated to make it positive.
    uint z = uint(log(-view_z / u_near_z) * u_log_cluster_res_y);

    return uvec3(x, y, z);
}

// Heat map functions
// source: https://www.shadertoy.com/view/ltlSRj
vec3 fromRedToGreen(float interpolant)
{
    if (interpolant < 0.5)
    {
       return vec3(1.0, 2.0 * interpolant, 0.0);
    }
    else
    {
        return vec3(2.0 - 2.0 * interpolant, 1.0, 0.0 );
    }
}

vec3 fromGreenToBlue(float interpolant)
{
    if (interpolant < 0.5)
    {
       return vec3(0.0, 1.0, 2.0 * interpolant);
    }
    else
    {
        return vec3(0.0, 2.0 - 2.0 * interpolant, 1.0 );
    }
}

vec3 heatMap(float interpolant)
{
    float invertedInterpolant = interpolant;
    if (invertedInterpolant < 0.5)
    {
        float remappedFirstHalf = 1.0 - 2.0 * invertedInterpolant;
        return fromGreenToBlue(remappedFirstHalf);
    }
    else
    {
        float remappedSecondHalf = 2.0 - 2.0 * invertedInterpolant;
        return fromRedToGreen(remappedSecondHalf);
    }
}

vec3 falseColor(float value)
{
    value = clamp(value, 0, 1);

	// TODO: could do this in a more general way (i.e. a gradient)
	//  but this is probably faster

	const float N = 8.0;      // number of sub-gradients
    const float w = 1.0 / N;

    const float v = 3;

    const vec3 c0 = vec3( 0,   0,  v/2);
    const vec3 c1 = vec3( 0,   0,   v);
	const vec3 c2 = vec3( 0,  v/2,  v);
	const vec3 c3 = vec3( 0,   v,   v);
    const vec3 c4 = vec3(v/2,  v,  v/2);
    const vec3 c5 = vec3( v,   v,   0);
    const vec3 c6 = vec3( v,  v/2,  0);
    const vec3 c7 = vec3( v,   0,   0);
    const vec3 c8 = vec3(v/2,  0,   0);

    if (value < 1*w)
    	return mix(c0, c1, (value - 0*w) * N);
    if (value < 2*w)
	    return mix(c1, c2, (value - 1*w) * N);
    if (value < 3*w)
    	return mix(c2, c3, (value - 2*w) * N);
    if (value < 4*w)
     	return mix(c3, c4, (value - 3*w) * N);
    if (value < 5*w)
      	return mix(c4, c5, (value - 4*w) * N);
	if (value < 6*w)
        return mix(c5, c6, (value - 5*w) * N);
    if (value < 7*w)
        return mix(c6, c7, (value - 6*w) * N);
	return mix(c7, c8, (value - 7*w) * N);
}

vec3 unpackNormal(vec2 f)
{
    // float z = sqrt(max(0, 1 - dot(f, f)));
    // return vec3(f, z);

    // octahedral encoding
    f = f * 2.0 - 1.0; // Back to [-1, 1]
    vec3 n = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy -= sign(n.xy) * t;
    return normalize(n);
}

float fadeByDistance(float distance, float hard_limit);

float fadeLightByDistance(GPULight light)
{
	float light_edge_distance = max(0, distance(light.position, u_cam_pos) - light.affect_radius);
	// fade the whole light by distance
	return fadeByDistance(light_edge_distance, u_light_max_distance);
}

// returns [1, 0] whther the fragment corresponding to 'atlas_uv' has LOS to the light
float sampleShadow(float distance, float normalized_depth, vec2 atlas_uv, vec2 uv_min, vec2 uv_max);
float fadeByDistance(float distance, float hard_limit);

uint detectCubeFaceSlot(vec3 light_to_frag, ShadowSlotInfo slot_info, out mat4 view_proj, out vec4 rect);

float shadowVisibility(vec3 light_to_frag, vec3 world_pos, GPULight light, mat4 proj, vec4 rect);
vec2 calculateShadowUV(vec3 world_pos, mat4 proj, vec4 rect, out vec4 rect_uv);

vec3 pointLightVisibility(GPULight light, vec3 world_pos)
{
	float light_fade = fadeLightByDistance(light);
	if(light_fade == 0)
		return vec3(0);

	if(! IS_SHADOW_CASTER(light))
		return vec3(light_fade);

	uint shadow_idx = GET_SHADOW_IDX(light);
	if(shadow_idx == LIGHT_NO_SHADOW)
		return vec3(light_fade);  // no shadow map in use

	// fade the shadow sample by distance
	float camera_distance = distance(world_pos, u_cam_pos);
	float shadow_fade = fadeByDistance(camera_distance, u_shadow_max_distance);
	if(shadow_fade == 0)
		return vec3(light_fade);

	ShadowSlotInfo slot_info = ssbo_shadow_slots[shadow_idx];

	vec3 light_to_frag = world_pos - light.position;

	mat4 view_proj;
	vec4 rect; // shadow slot rectangle in atlas, in absolute pixels
	detectCubeFaceSlot(light_to_frag, slot_info, view_proj, rect);

	float shadow_visibility = shadowVisibility(light_to_frag, world_pos, light, view_proj, rect);

	float shadow_faded = 1 - (1 - shadow_visibility) * shadow_fade;
	float visible = light_fade * shadow_faded;

	return vec3(visible);
}

vec3 dirLightVisibility(GPULight light)
{
	return vec3(1); // TODO
}

vec3 spotLightVisibility(GPULight light, vec3 world_pos)
{
	float light_fade = fadeLightByDistance(light);
	if(light_fade == 0)
		return vec3(0);

	if(! IS_SHADOW_CASTER(light))
		return vec3(light_fade);

	uint shadow_idx = GET_SHADOW_IDX(light);
	if(shadow_idx == LIGHT_NO_SHADOW)
		return vec3(light_fade);  // no shadow map in use

	// fade the shadow sample by distance
	float camera_distance = distance(world_pos, u_cam_pos);
	float shadow_fade = fadeByDistance(camera_distance, u_shadow_max_distance);
	if(shadow_fade == 0)
		return vec3(light_fade);

	ShadowSlotInfo slot_info = ssbo_shadow_slots[shadow_idx];

	mat4 view_proj = slot_info.view_proj[0];
	vec4 rect = slot_info.atlas_rect[0];

	vec4 rect_uv;
	vec2 atlas_uv = calculateShadowUV(world_pos, view_proj, rect, rect_uv);

	vec3 light_to_frag = world_pos - light.position;
	float shadow_visibility = shadowVisibility(light_to_frag, world_pos, light, view_proj, rect);

	float shadow_faded = 1 - (1 - shadow_visibility) * shadow_fade;
	float visible = light_fade * shadow_faded;

	return vec3(visible);
}

vec3 areaLightVisibility(GPULight light)
{
	return vec3(1); // TODO will probably never cast shadows
}

vec3 tubeLightVisibility(GPULight light)
{
	return vec3(1); // TODO will probably never cast shadows
}

vec3 sphereLightVisibility(GPULight light)
{
	return vec3(1); // TODO will probably never cast shadows
}

vec3 discLightVisibility(GPULight light)
{
	return vec3(1); // TODO will probably never cast shadows
}

vec2 calculateShadowUV(vec3 world_pos, mat4 view_proj, vec4 rect, out vec4 rect_uv)
{
	// rect     : slot square in atlas pixel-space; x,y & w,h
	// rect_uv  : slot square in atlas UV-space; x,y & w,h
	// face_uv  : fragment position within the point light's cube face (facing the direction of the fragment)
	//            0,0 of that cube face corresponds to the "top-lect" corner of the slot square
	// atlas_uv : combination of above; the rect_uv + offset by face_uv, in atlas UV-space.

	// ignore 1 pixel around the edges,
	//   so the linear filtering doesn't blend  outside the slot rect
	rect.x += 1;
	rect.y += 1;
	rect.z -= 2;
	rect.w -= 2;

	// to light space
	vec4 light_space = view_proj * vec4(world_pos, 1);
	light_space.xyz /= light_space.w; // NDC
	vec2 face_uv = light_space.xy * 0.5 + 0.5; // [0, 1]
	rect_uv = rect * vec4(shadow_atlas_texel_size, shadow_atlas_texel_size);
	return rect_uv.xy + face_uv * (rect_uv.zw + shadow_atlas_texel_size);
}

float shadowVisibility(vec3 light_to_frag, vec3 world_pos, GPULight light, mat4 view_proj, vec4 rect)
{
	vec4 rect_uv;
	vec2 atlas_uv = calculateShadowUV(world_pos, view_proj, rect, rect_uv);

	float camera_distance = distance(world_pos, u_cam_pos);
	float light_distance = length(light_to_frag);
	float normalized_depth = light_distance / light.affect_radius;

	// calculate shadow depth bias by various factors
	// bias by depth
	float bias = normalized_depth * u_shadow_bias_distance_scale;

	// bias based on surface normals
	// bias increases when the angle between normal and light_dir is steep.
	if(u_shadow_bias_slope_scale > 0)
	{
		vec2 encoded_normal = textureLod(u_shadow_atlas_normals, atlas_uv, 0).xy;
		vec3 depth_normal = unpackNormal(encoded_normal);
		vec3 light_dir = normalize(-light_to_frag);
		float angle = dot(depth_normal, light_dir);
		angle = pow(angle, u_shadow_bias_slope_power);
		angle = clamp(angle, 0.0, 0.99);
		bias += (0.001 / angle) * u_shadow_bias_slope_scale;
	}

	bias += u_shadow_bias_constant;
	bias = clamp(bias, -0.05, 0.05) * u_shadow_bias_scale;

	normalized_depth -= bias;

	vec2 uv_min = rect_uv.xy;
	vec2 uv_max = rect_uv.xy + rect_uv.zw - shadow_atlas_texel_size;
	return sampleShadow(camera_distance, normalized_depth, atlas_uv, uv_min, uv_max);
}

float sampleShadow1(float current_depth, vec2 atlas_uv, vec2 uv_min, vec2 uv_max)
{
	// if 'atlas_uv' is closer than 2 pixels to 'uv_min' or 'uv_mac' (component wise),
	// use the _single sampler
	float margin = shadow_atlas_texel_size.x*2;

	if(atlas_uv.x <= uv_min.x + margin || atlas_uv.x >= uv_max.x - margin
	   || atlas_uv.y <= uv_min.y + margin || atlas_uv.y >= uv_max.y - margin)
	{
		atlas_uv = clamp(atlas_uv, uv_min, uv_max);
		return texture(u_shadow_atlas_single, atlas_uv).r > current_depth? 1: 0;
	}
	else
		return texture(u_shadow_atlas, vec3(atlas_uv, current_depth));
}

float sampleShadow5(float current_depth, vec2 atlas_uv, vec2 uv_min, vec2 uv_max)
{
	// 5-point "kernel", X-shape

	vec2 uv;
	float shadow = 0;
	float sample_depth;

	// NOTE: UV is capped to stay withing the single shadow map slot (cube face)
	//   but strictly, the sampling should in those cases instead sample from the
	//   "spatial naighbour" slot. That is, however, quite complicated... :|

	// TODO: sample using sampler2DShadow for all samples,
	//   except near edges (1-2 texel margin), where sampler2D should be used.
	//   otherwise, sampler2DShadow will sample outside the slot square (commonly uses 2x2 texels)
#define SAMPLE(uv_offset, weight) \
	uv = atlas_uv + uv_offset*shadow_atlas_texel_size; \
	shadow += sampleShadow1(current_depth, uv, uv_min, uv_max)*weight;

	// cheaper sampling: only center and four corners of the 3x3 box
	const float weights5[2] = { 0.4, 0.15 };  // total = 1
	// top corners
	SAMPLE(vec2(-1, -1), weights5[1]);
	SAMPLE(vec2( 1, -1), weights5[1]);
	// center
	SAMPLE(vec2( 0,  0), weights5[0]);
	// bottom corners
	SAMPLE(vec2(-1,  1), weights5[1]);
	SAMPLE(vec2( 1,  1), weights5[1]);

	return shadow;
}

float sampleShadow9(float current_depth, vec2 atlas_uv, vec2 uv_min, vec2 uv_max)
{
	// 3x3 gauss kernel

	// TODO: random sampling
	//    https://www.youtube.com/watch?v=NCptEJ1Uevg&t=380s
	//    https://www.youtube.com/watch?v=3FMONJ1O39U&t=850s

	vec2 uv;
	float shadow = 0;
	float sample_depth;

	// NOTE: UV is capped to stay withing the single shadow map slot (cube face)
	//   but strictly, the sampling should in those cases instead sample from the
	//   "spatial naighbour" slot. That is, however, quite complicated... :|

	// TODO: possible to increase the sample raidus depending on 'current_depth'
	//   i.e. to achieve more blurred shadow for more distant shadows

	// sample a 3x3 box around the sample
#define SAMPLE(uv_offset, weight) \
	uv = atlas_uv + uv_offset*shadow_atlas_texel_size; \
	shadow += sampleShadow1(current_depth, uv, uv_min, uv_max)*weight;

  	// 3x3 gauss box
  	const float weights9[3] = { 0.25, 0.125, 0.0625 };  // total = 1
	// top row
	SAMPLE(vec2(-1, -1), weights9[2]);
	SAMPLE(vec2( 0, -1), weights9[1]);
	SAMPLE(vec2( 1, -1), weights9[2]);
	// middle row
	SAMPLE(vec2(-1,  0), weights9[1]);
	SAMPLE(vec2( 0,  0), weights9[0]);
	SAMPLE(vec2( 1,  0), weights9[1]);
	// bottom row
	SAMPLE(vec2(-1,  1), weights9[2]);
	SAMPLE(vec2( 0,  1), weights9[1]);
	SAMPLE(vec2( 1,  1), weights9[2]);

	return shadow;
}

float sampleShadow(float distance, float normalized_depth, vec2 atlas_uv, vec2 uv_min, vec2 uv_max)
{
	if(distance > shadow_low_sampling_distance)
		return sampleShadow1(normalized_depth, atlas_uv, uv_min, uv_max);
	else if(distance > shadow_mid_sampling_distance)
		return sampleShadow5(normalized_depth, atlas_uv, uv_min, uv_max);
	return sampleShadow9(normalized_depth, atlas_uv, uv_min, uv_max);
}

float fadeByDistance(float distance, float hard_limit)
{
	return 1 - smoothstep(hard_limit*0.8, hard_limit, distance);
}

uint detectCubeFaceSlot(vec3 light_to_frag, ShadowSlotInfo slot_info, out mat4 view_proj, out vec4 rect)
{
	// figure out which of the 6 cube faces is relevant

	// X-axis as initial assumption
	int major_axis = 0;
	float max_axis = abs(light_to_frag.x);
	vec3 abs_dir = abs(light_to_frag);

	if (abs_dir.y > max_axis)
	{
	    major_axis = 1;
	    max_axis = abs_dir.y;
	}
	if (abs_dir.z > max_axis)
	{
	    major_axis = 2;
	    max_axis = abs_dir.z;
	}

	// select projection and atlas rect for this face
	//   surprisingly, this ugliness is quite a bit faster than just view_proj[face]
	mat4 vp_0 = slot_info.view_proj[0];
	mat4 vp_1 = slot_info.view_proj[1];
	mat4 vp_2 = slot_info.view_proj[2];
	mat4 vp_3 = slot_info.view_proj[3];
	mat4 vp_4 = slot_info.view_proj[4];
	mat4 vp_5 = slot_info.view_proj[5];

	vec4 rect_0 = slot_info.atlas_rect[0];
	vec4 rect_1 = slot_info.atlas_rect[1];
	vec4 rect_2 = slot_info.atlas_rect[2];
	vec4 rect_3 = slot_info.atlas_rect[3];
	vec4 rect_4 = slot_info.atlas_rect[4];
	vec4 rect_5 = slot_info.atlas_rect[5];

	uint face;

	if (major_axis == 0)
	{
	    if(light_to_frag.x > 0) // +X
		{
			face = 0;
			view_proj = vp_0;
			rect = rect_0;
		}
		else  // -X
		{
			face = 1;
			view_proj = vp_1;
			rect = rect_1;
		}
	}
	else if (major_axis == 1)
	{
		if(light_to_frag.y > 0) // +Y
		{
			face = 2;
			view_proj = vp_2;
			rect = rect_2;
		}
		else  // -Y
		{
			face = 3;
			view_proj = vp_3;
			rect = rect_3;
		}
	}
	else
	{
	    if(light_to_frag.z > 0) // +Z
		{
			face = 4;
			view_proj = vp_4;
			rect = rect_4;
		}
		else  // -Z
		{
			face = 5;
			view_proj = vp_5;
			rect = rect_5;
		}
	}
	return face;
}
