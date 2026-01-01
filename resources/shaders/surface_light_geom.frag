#version 460 core

layout(location = 0) flat in vec3 in_color_intensity;
layout(location = 1) flat in uint in_double_sided;

layout(location = 0) out vec4 frag_color;

void main()
{
    if (gl_FrontFacing || in_double_sided == 1)
       	frag_color = vec4(in_color_intensity, 1);
    else
    	discard;
}
