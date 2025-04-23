#version 460 core
#include "pbr_lighting.glh"

out vec4 frag_color;

uniform float u_near_z;
uniform uvec3 u_cluster_resolution;
uniform uvec2 u_cluster_size_ss;
uniform float u_log_cluster_res_y;

uniform bool u_debug_cluster_geom;
uniform bool u_debug_clusters_occupancy;
uniform float u_debug_clusters_occupancy_blend_factor;

const vec3 debug_colors[8] = vec3[]
(
   vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 1),
   vec3(1, 0, 0), vec3(1, 0, 1), vec3(1, 1, 0), vec3(1, 1, 1)
);

layout(std430, binding = SSBO_BIND_DIRECTIONAL_LIGHTS) buffer DirLightsSSBO
{
	DirectionalLight dir_lights[];
};

layout(std430, binding = SSBO_BIND_POINT_LIGHTS) buffer PointLightSSBO
{
	PointLight point_lights[];
};

layout(std430, binding = SSBO_BIND_SPOT_LIGHTS) buffer SpotLightsSSBO
{
	SpotLight spot_lights[];
};

layout(std430, binding = SSBO_BIND_AREA_LIGHTS) buffer AreaLightsSSBO
{
	AreaLight area_lights[];
};

layout(std430, binding = SSBO_BIND_POINT_LIGHT_INDEX) buffer PointLightIndexListSSBO
{
	uint point_light_index_list[];
};

layout(std430, binding = SSBO_BIND_CLUSTER_POINT_LIGHTS) buffer PointIndexRangeSSBO
{
	IndexRange point_light_grid[];
};

layout(std430, binding = SSBO_BIND_SPOT_LIGHT_INDEX) buffer SpotLightIndexListSSBO
{
	uint spot_light_index_list[];
};

layout(std430, binding = SSBO_BIND_CLUSTER_SPOT_LIGHTS) buffer SpotIndexRangeSSBO
{
	IndexRange spot_light_grid[];
};

layout (std430, binding = SSBO_BIND_AREA_LIGHT_INDEX) buffer AreaLightIndexListSSBO
{
	uint area_light_index_list[];
};

layout (std430, binding = SSBO_BIND_CLUSTER_AREA_LIGHTS) buffer AreaIndexRangeSSBO
{
    IndexRange area_light_grid[];
};

vec3  fromRedToGreen(float interpolant);
vec3  fromGreenToBlue(float interpolant);
vec3  heatMap(float interpolant);
uint computeClusterIndex(uvec3 cluster_coord);
uvec3 computeClusterCoord(vec2 screen_pos, float view_z);

void main()
{
    vec3 radiance = vec3(0.0);
    vec3 normal = normalize(in_normal);

    MaterialProperties material = getMaterialProperties(normal);

    // Calculate the directional lights
    for (uint i = 0; i < dir_lights.length(); ++i)
    {
        radiance += calcDirectionalLight(dir_lights[i], in_world_pos, material);
    }

    // Locating the cluster we are in
    uvec3 cluster_coord = computeClusterCoord(gl_FragCoord.xy, in_view_pos.z);
    uint cluster_index = computeClusterIndex(cluster_coord);

    // Calculate the point lights contribution
    uint light_index_offset = point_light_grid[cluster_index].offset;
    uint light_count = point_light_grid[cluster_index].count;

    for (uint i = 0; i < light_count; ++i)
    {
        uint light_index = point_light_index_list[light_index_offset + i];
        radiance += calcPointLight(point_lights[light_index], in_world_pos, material);
    }

    // Calculate the spot lights contribution
    light_index_offset = spot_light_grid[cluster_index].offset;
    light_count = spot_light_grid[cluster_index].count;

    for (uint i = 0; i < light_count; ++i)
    {
        uint light_index = spot_light_index_list[light_index_offset + i];
        radiance += calcSpotLight(spot_lights[light_index], in_world_pos, material);
    }

    // Calculate the area lights contribution
    light_index_offset = area_light_grid[cluster_index].offset;
    light_count = area_light_grid[cluster_index].count;

    for (uint i = 0; i < light_count; ++i)
    {
        uint light_index = area_light_index_list[light_index_offset + i];
        radiance += calcLtcAreaLight(area_lights[light_index], in_world_pos, material);
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

		frag_color = vec4(mix(radiance, cluster_color, u_debug_clusters_occupancy_blend_factor), 1);
    }
    else if (u_debug_clusters_occupancy)
    {
        uint total_light_count = point_light_grid[cluster_index].count + spot_light_grid[cluster_index].count + area_light_grid[cluster_index].count;
        if (total_light_count > 0)
        {
            float normalized_light_count = total_light_count / 100.0;
            vec3 heat_map_color = heatMap(clamp(normalized_light_count, 0.0, 1.0));

            frag_color = vec4(mix(radiance, heat_map_color, u_debug_clusters_occupancy_blend_factor), 1.0);
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
