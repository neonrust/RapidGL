#version 460 core
#include "pbr_lighting.glh"

out vec4 frag_color;

uniform float u_near_z;
uniform uvec3 u_cluster_resolution;
uniform uvec2 u_cluster_size_ss;
uniform float u_log_cluster_res_y;
uniform uint  u_num_cluster_avg_lights;


uniform bool u_debug_cluster_geom;
uniform bool u_debug_clusters_occupancy;
uniform float u_debug_clusters_occupancy_blend_factor;

const vec3 debug_colors[8] = vec3[]
(
   vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 1),
   vec3(1, 0, 0), vec3(1, 0, 1), vec3(1, 1, 0), vec3(1, 1, 1)
);


layout(std430, binding = SSBO_BIND_LIGHTS) buffer LightsMgmtSSBO
{
	LightsManagement lights;
};

layout(std430, binding = SSBO_BIND_SIMPLE_CLUSTERS_AABB) buffer ClustersSimpleSSBO
{
	SimpleCluster clusters[];
};


vec3  fromRedToGreen(float interpolant);
vec3  fromGreenToBlue(float interpolant);
vec3  heatMap(float interpolant);
uint computeClusterIndex(uvec3 cluster_coord);
uvec3 computeClusterCoord(vec2 screen_pos, float view_z);


float lightVisibility(DirectionalLight light);
float lightVisibility(PointLight light);
float lightVisibility(SpotLight light);
float lightVisibility(AreaLight light);

mat4 lightViewProjection(PointLight light);

void main()
{
    vec3 radiance = vec3(0.0);
    vec3 normal = normalize(in_normal);

    MaterialProperties material = getMaterialProperties(normal);

    // Calculate the directional lights
    for (uint i = 0; i < lights.num_dir_lights; ++i)
    {
    	float visibility = lightVisibility(lights.dir_lights[i]);
     	if(visibility > 0)
        	radiance += visibility * calcDirectionalLight(lights.dir_lights[i], in_world_pos, material);
    }

    // Locating the cluster we are in
    uvec3 cluster_coord = computeClusterCoord(gl_FragCoord.xy, in_view_pos.z);
    uint cluster_index = computeClusterIndex(cluster_coord);

    SimpleCluster cluster = clusters[cluster_index];

    uint num_clusters = u_cluster_resolution.x*u_cluster_resolution.y*u_cluster_resolution.z;

    // Calculate the point lights contribution
    uint num_point_lights = min(cluster.num_point_lights, CLUSTER_MAX_POINT_LIGHTS);
    // TODO: if cluster.num_point_lights > CLUSTER_MAX_POINT_LIGHTS, highlight the pixel?
    for (uint i = 0; i < num_point_lights; ++i)
    {
	    uint light_index = cluster.light_index[i];
       	float visibility = lightVisibility(lights.point_lights[light_index]);
        if(visibility > 0)
        	radiance += visibility * calcPointLight(lights.point_lights[light_index], in_world_pos, material);
    }

    // Calculate the spot lights contribution
    uint spot_offset = num_clusters;
    uint idx_spot_offset = spot_offset*u_num_cluster_avg_lights;

    uint index_offset = CLUSTER_MAX_POINT_LIGHTS;
    uint num_spot_lights = min(cluster.num_spot_lights, CLUSTER_MAX_SPOT_LIGHTS);
    // TODO: if cluster.num_spot_lights > CLUSTER_MAX_SPOT_LIGHTS, highlight the pixel?
    for (uint i = 0; i < num_spot_lights; ++i)
    {
	    uint light_index = cluster.light_index[i + index_offset];
       	float visibility = lightVisibility(lights.spot_lights[light_index]);
        if(visibility > 0)
	        radiance += visibility * calcSpotLight(lights.spot_lights[light_index], in_world_pos, material);
    }

    // Calculate the area lights contribution
    uint area_offset = 2*num_clusters;
    uint idx_area_offset = area_offset*u_num_cluster_avg_lights;

    index_offset += CLUSTER_MAX_SPOT_LIGHTS;
    uint num_area_lights = min(cluster.num_area_lights, CLUSTER_MAX_AREA_LIGHTS);
    // TODO: if cluster.num_area_lights > CLUSTER_MAX_AREA_LIGHTS, highlight the pixel?
    for (uint i = 0; i < num_area_lights; ++i)
    {
	    uint light_index = cluster.light_index[i + index_offset];
       	float visibility = lightVisibility(lights.area_lights[light_index]);
        if(visibility > 0)
	        radiance += visibility * calcLtcAreaLight(lights.area_lights[light_index], in_world_pos, material);
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
        uint total_light_count = cluster.num_point_lights \
						       	+ cluster.num_spot_lights \
						        + cluster.num_area_lights;
        if (total_light_count > 0)
        {
            float normalized_light_count = total_light_count / 100.0;
            vec3 heat_map_color = heatMap(clamp(normalized_light_count, 0.0, 1.0));

            frag_color = vec4(mix(radiance, heat_map_color, u_debug_clusters_occupancy_blend_factor), 1.0);
        }
        else
	       frag_color = vec4(0.4, 0.15, 0.5, 1); // purple is not in the heat map gradient
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

float lightVisibility(DirectionalLight light)
{
	return 1; // TODO
}
float lightVisibility(PointLight light)
{
	return 1; // TODO
}

float lightVisibility(SpotLight light)
{
	return 1; // TODO
}

float lightVisibility(AreaLight light)
{
	return 1; // TODO
}
