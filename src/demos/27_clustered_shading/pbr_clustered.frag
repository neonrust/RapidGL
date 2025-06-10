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

uniform float u_shadow_bias_constant;
uniform float u_shadow_bias_slope_scale;
uniform float u_shadow_bias_distance_scale;
uniform float u_shadow_bias_slope_power;
uniform float u_shadow_bias_scale;

const vec3 debug_colors[8] = vec3[]
(
   vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 1),
   vec3(1, 0, 0), vec3(1, 0, 1), vec3(1, 1, 0), vec3(1, 1, 1)
);


layout(std430, binding = SSBO_BIND_LIGHTS) readonly buffer LightsMgmtSSBO
{
	LightsManagement lights;
};

layout(std430, binding = SSBO_BIND_SIMPLE_CLUSTERS_AABB) readonly buffer ClustersSimpleSSBO
{
	SimpleCluster clusters[];
};

layout(std430, binding = SSBO_BIND_SHADOW_PARAMS) readonly buffer ShadowParamsSSBO
{
	LightShadowParams shadow_params[];
};

vec3  fromRedToGreen(float interpolant);
vec3  fromGreenToBlue(float interpolant);
vec3  heatMap(float interpolant);
uint computeClusterIndex(uvec3 cluster_coord);
uvec3 computeClusterCoord(vec2 screen_pos, float view_z);


float dirLightVisibility(uint index);
float pointLightVisibility(uint index);
float spotLightVisibility(uint index);
float areaLightVisibility(uint index);

mat4 lightViewProjection(PointLight light);

void main()
{
    vec3 radiance = vec3(0.0);
    vec3 normal = normalize(in_normal);

    MaterialProperties material = getMaterialProperties(normal);

    // Calculate the directional lights
    for (uint index = 0; index < lights.num_dir_lights; ++index)
    {
    	float visibility = dirLightVisibility(index);
     	if(visibility > 0)
        	radiance += visibility * calcDirectionalLight(lights.dir_lights[index], in_world_pos, material);
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
       	float visibility = pointLightVisibility(light_index);
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
       	float visibility = spotLightVisibility(light_index);
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
       	float visibility = areaLightVisibility(light_index);
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

// returns [1, 0] whther the fragment corresponding to 'atlas_uv' has LOS to the light
float lineOfSight(float current_depth, vec2 atlas_uv, vec2 texel_size);

float dirLightVisibility(uint index)
{
	return 1; // TODO
}

float pointLightVisibility(uint index)
{
	PointLight light = lights.point_lights[index];
	if((light.base.feature_flags & LIGHT_SHADOW_CASTER) == 0)
		return 1;

	LightShadowParams params = shadow_params[index];

	vec3 light_to_frag = in_world_pos - light.position;

	// figure out which of the 6 faces is relevant
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
	mat4 proj_0 = params.view_proj[0];
	mat4 proj_1 = params.view_proj[1];
	mat4 proj_2 = params.view_proj[2];
	mat4 proj_3 = params.view_proj[3];
	mat4 proj_4 = params.view_proj[4];
	mat4 proj_5 = params.view_proj[5];

	vec4 rect_0 = params.atlas_rect[0];
	vec4 rect_1 = params.atlas_rect[1];
	vec4 rect_2 = params.atlas_rect[2];
	vec4 rect_3 = params.atlas_rect[3];
	vec4 rect_4 = params.atlas_rect[4];
	vec4 rect_5 = params.atlas_rect[5];

	mat4 proj;
	vec4 rect;

	if (major_axis == 0)
	{
	    if(light_to_frag.x > 0) // +X
		{
			proj = proj_0;
			rect = rect_0;
		}
		else  // -X
		{
			proj = proj_1;
			rect = rect_1;
		}
	}
	else if (major_axis == 1)
	{
		if(light_to_frag.y > 0) // +Y
		{
			proj = proj_2;
			rect = rect_2;
		}
		else  // -Y
		{
			proj = proj_3;
			rect = rect_3;
		}
	}
	else
	{
	    if(light_to_frag.z > 0) // +Z
		{
			proj = proj_4;
			rect = rect_4;
		}
		else  // -Z
		{
			proj = proj_5;
			rect = rect_5;
		}
	}

	// return float(face)/5.0;


	// to light space
	vec4 light_space = proj * vec4(in_world_pos, 1);
	light_space.xyz /= light_space.w; // NDC
	vec2 face_uv = light_space.xy * 0.5 + 0.5; // [0, 1]

	vec2 atlas_uv = rect.xy + face_uv * rect.zw;

	// TODO: use square distance?
	float light_distance = length(light_to_frag);
	float normalized_depth = light_distance / light.radius;

	vec2 encoded_normal = texture(u_shadow_atlas_normals, atlas_uv).xy;
	vec3 depth_normal = unpackNormal(encoded_normal);
	vec3 light_dir = normalize(-light_to_frag);
	float angle = dot(depth_normal, light_dir);


	float bias = 0;

	// depth-scaled bias
	bias += u_shadow_bias_distance_scale * normalized_depth;

	// bias based on surface normals
	// bias increases when the angle between normal and light_dir is steep.
	angle = pow(angle, u_shadow_bias_slope_power);
	angle = clamp(angle, 0.0, 0.99);
	bias += (0.001 / angle) * u_shadow_bias_slope_scale;

	bias = u_shadow_bias_constant + bias * u_shadow_bias_scale;
	bias = clamp(bias, 0.0, 0.05);

	normalized_depth -= bias;

	// float shadow_depth = texture(u_shadow_atlas, atlas_uv).r;
	// return normalized_depth > shadow_depth ? 0 : 1;
	vec2 texel_size = 1.0 / vec2(textureSize(u_shadow_atlas, 0));
	return lineOfSight(normalized_depth, atlas_uv, texel_size);
}

float spotLightVisibility(uint index)
{
	return 1; // TODO
}

float areaLightVisibility(uint index)
{
	return 1; // TODO
}

// float pcfInShadow(float current_depth, vec2 atlas_uv, vec2 texel_size)
// {
// 	float shadow = 0;

// 	// in-shadow state, averaged over a 3x3 kernel
// 	for (int x = -1; x <= 1; ++x)
// 	{
// 		for (int y = -1; y <= 1; ++y)
// 		{
// 		    vec2 offset = vec2(x, y) * texel_size;
// 		    float sample_depth = texture(u_shadow_atlas, atlas_uv + offset).r;
// 		    shadow += current_depth > sample_depth ? 0.0 : 1.0;
// 		}
// 	}

// 	return shadow / 9.0;
// }

float lineOfSight(float current_depth, vec2 atlas_uv, vec2 texel_size)
{
	// 3x3 gauss kernel

	float shadow = 0;
	float sample_depth;

	texel_size = vec2(1)/4096.f;

#define SAMPLE(uv_offset, weight) \
	sample_depth = texture(u_shadow_atlas, atlas_uv + uv_offset*texel_size).r; \
 	shadow += current_depth > sample_depth ? 0.0 : (weight);

  	const float weights[3] = { 0.25, 0.125, 0.0625 };

	// top row
	SAMPLE(vec2(-1, -1), weights[2]);
	SAMPLE(vec2( 0, -1), weights[1]);
	SAMPLE(vec2( 1, -1), weights[2]);
	// middle row
	SAMPLE(vec2(-1,  0), weights[1]);
	SAMPLE(vec2( 0,  0), weights[0]);
	SAMPLE(vec2( 1,  0), weights[1]);
	// bottom row
	SAMPLE(vec2(-1,  1), weights[2]);
	SAMPLE(vec2( 0,  1), weights[1]);
	SAMPLE(vec2( 1,  1), weights[2]);

	return shadow;
}
