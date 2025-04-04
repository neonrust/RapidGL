#version 460 core
out vec4 frag_color;

in vec3 normal;
in vec3 world_pos;
in vec2 texcoord;

uniform vec3 light_color;
uniform vec3 light_direction;
uniform float light_intensity;
uniform float ambient_factor;
uniform vec3 object_color;

uniform vec3 cam_pos;
uniform float gamma;

uniform float specular_power;
uniform float specular_intensity;

uniform sampler2D texture_diffuse1;

uniform float diffuse_levels;
uniform float specular_levels;

vec4 toonShading(vec3 normal)
{
    vec3 dir_to_eye  = normalize(cam_pos - world_pos);
    vec3 half_vector = normalize(dir_to_eye - light_direction);
    
    float df = max(0.0, dot(normal, -light_direction));
    float sf = max(0.0, dot(normal, half_vector));
          sf = pow(sf, specular_power);

    df = floor((df * diffuse_levels)  + 0.5) / diffuse_levels;
    sf = floor((sf * specular_levels) + 0.5) / specular_levels;

    vec3 albedo_texture = texture(texture_diffuse1, texcoord).rgb;
    vec3 ambient_color  = ambient_factor * object_color * albedo_texture * light_intensity;
    vec3 diffuse_color  = light_color * light_intensity * object_color * albedo_texture;
    vec3 specular_color = vec3(specular_intensity) * light_intensity;

    vec3 color = ambient_color + df * diffuse_color + sf * specular_color;

    return vec4(color, 1.0);
}

vec4 reinhard(vec4 hdr_color)
{
    // reinhard tonemapping
    vec3 ldr_color = hdr_color.rgb / (hdr_color.rgb + 1.0);

    // gamma correction
    ldr_color = pow(ldr_color, vec3(1.0 / gamma));

    return vec4(ldr_color, 1.0);
}

void main()
{
    frag_color = reinhard(toonShading(normalize(normal)));
} 