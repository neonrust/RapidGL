#version 460 core

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uint in_icon_index;
layout(location = 2) flat in vec3 in_color;

out vec4 fragColor;

layout(binding = 1) uniform sampler2DArray u_icon_array;

void main()
{
    vec4 icon_pixel = texture(u_icon_array, vec3(in_uv, in_icon_index));

    // TODO: fade with distance could be passed in via a uniform/instance
    fragColor = vec4(icon_pixel.rgb * in_color, icon_pixel.a);
}
