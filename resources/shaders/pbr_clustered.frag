#version 460 core
#include "light.glh"
#include "pbr_lighting.glh"
#include "shadows.glh"
#include "volumetrics.glh"

out vec4 frag_color;

uniform float u_near_z;
uniform uvec3 u_cluster_resolution;
uniform uvec2 u_cluster_size_ss;
uniform float u_log_cluster_res_y;
uniform float u_light_max_distance;

uniform bool u_debug_cluster_geom;
uniform bool u_debug_clusters_occupancy;
uniform bool u_debug_tile_occupancy;
uniform float u_debug_overlay_blend;
uniform uvec2 u_viewport_size;
uniform bool  u_csm_colorize_cascades;

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
vec3 dirLightVisibility(GPULight light, vec3 world_pos);
vec3 spotLightVisibility(GPULight light, vec3 world_pos);
vec3 rectLightVisibility(GPULight light);
vec3 tubeLightVisibility(GPULight light);
vec3 sphereLightVisibility(GPULight light);
vec3 discLightVisibility(GPULight light);


void main()
{
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

    vec3 radiance = vec3(0);

    // too many lights?
    if(lights_range.count > CLUSTER_MAX_LIGHTS)
		radiance += vec3(1, 0, 1);

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
		        visibility = dirLightVisibility(light, in_world_pos);
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

          	case LIGHT_TYPE_RECT:
           	{
	           	visibility = rectLightVisibility(light);
		        if(visibility.x + visibility.y + visibility.z > s_min_visibility)
		        	contribution = calcRectLight(light, in_world_pos, material);
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
	            contribution = vec3(5, 0, 5);
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
		uvec2 tile = uvec2(gl_FragCoord.xy) / (u_viewport_size / tile_grid);
		uint tile_index = tile.y*tile_grid.x + tile.x;

		IndexRange lights_range = ssbo_tile_lights[tile_index];

		if (lights_range.count > 0)
        {
            float normalized_light_count = float(lights_range.count) / float(FROXEL_TILE_MAX_LIGHTS);
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
        // TODO: apply accumulated fog/scattering here?  (instead of the 'bake' step)
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

float fadeByDistance(float distance, float hard_limit);

float fadeLightByDistance(GPULight light)
{
	float light_edge_distance = max(0, distance(light.position, u_cam_pos) - light.affect_radius);
	// fade the whole light by distance
	return fadeByDistance(light_edge_distance, u_light_max_distance);
}

// returns [1, 0] whther the fragment corresponding to 'atlas_uv' has LOS to the light
float fadeByDistance(float distance, float hard_limit);

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
	vec4 slot_rect; // shadow slot rectangle in atlas, in absolute pixels
	uint slot_idx = detectCubeFaceSlot(light_to_frag, slot_info, view_proj, slot_rect);

	vec4 clip_pos = view_proj * vec4(world_pos, 1);
	clip_pos /= clip_pos.w;
	float shadow_visibility = shadowVisibility(clip_pos.xyz, camera_distance, light, slot_rect);

	float shadow_faded = 1 - (1 - shadow_visibility) * shadow_fade;
	float visible = light_fade * shadow_faded;

	return vec3(visible);
}

vec3 dirLightVisibility(GPULight light, vec3 world_pos)
{
	if(! IS_SHADOW_CASTER(light))
		return vec3(1);

	// vec3 cascade_debug_indicator = vec3(0.0, 0.0, 0.0);
	float camera_distance = distance(world_pos, u_cam_pos);
	float shadow_fade = 1;
	// float shadow_fade = fadeByDistance(camera_distance, u_shadow_dir_max_distance);
	// if(shadow_fade == 0)
	// 	return vec3(0);

	uint shadow_idx = GET_SHADOW_IDX(light);
	if(shadow_idx == LIGHT_NO_SHADOW)
		return vec3(1);  // no shadow map in use

	ShadowSlotInfo slot_info = ssbo_shadow_slots[shadow_idx];

	// use depth splits to select slot
	// find cascade index
	uint cascade_index = 0;
	for(uint idx = 0; idx < u_csm_num_cascades - 1; ++idx)
	{
	    if(in_view_pos.z < u_csm_depth_splits[idx])
	        cascade_index = idx + 1;
	}

	mat4 view_proj = slot_info.view_proj[cascade_index];
	uvec4 slot_rect = slot_info.atlas_rect[cascade_index];

	vec4 clip_pos = view_proj * vec4(world_pos, 1);
	clip_pos /= clip_pos.w;
	float shadow_visibility = shadowVisibility(clip_pos.xyz, camera_distance, light, slot_rect);

	float shadow_faded = 1 - (1 - shadow_visibility) * shadow_fade;
	vec3 visible_color = vec3(shadow_faded);

	if(u_csm_colorize_cascades)
	{
		if(cascade_index % 2 == 0)
			visible_color *= vec3(0.8, 1.4, 0.8);
		else
			visible_color *= vec3(1.4, 0.8, 1.4);
	}

	return visible_color;
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
	vec4 slot_rect = slot_info.atlas_rect[0];

	vec4 clip_pos = view_proj * vec4(world_pos, 1);
	clip_pos /= clip_pos.w;
	float shadow_visibility = shadowVisibility(clip_pos.xyz, camera_distance, light, slot_rect);

	float shadow_faded = 1 - (1 - shadow_visibility) * shadow_fade;
	float visible = light_fade * shadow_faded;

	return vec3(visible);
}

vec3 rectLightVisibility(GPULight light)
{
	return vec3(fadeLightByDistance(light));
}

vec3 tubeLightVisibility(GPULight light)
{
	return vec3(fadeLightByDistance(light));
}

vec3 sphereLightVisibility(GPULight light)
{
	return vec3(fadeLightByDistance(light));
}

vec3 discLightVisibility(GPULight light)
{
	return vec3(fadeLightByDistance(light));
}


float fadeByDistance(float distance, float hard_limit)
{
	return 1 - smoothstep(hard_limit*0.8, hard_limit, distance);
}
