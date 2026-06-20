#version 460 core

in vec4 frag_pos;

uniform vec3 u_light_pos;
uniform float u_far_z;

void main()
{
    // Manually sets depth map in the range [0, 1]
    gl_FragDepth = length(frag_pos.xyz - u_light_pos) / u_far_z;
}
