#version 460 core

layout (location = 0) in vec3 in_texcoord;
layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler3D u_texture;

uniform float u_brightness;
uniform float u_alpha_boost;

void main()
{
	frag_color = texture(u_texture, in_texcoord);
	frag_color.rgb = min(frag_color.rgb * u_brightness, vec3(1));
	frag_color.a = min(frag_color.a * u_alpha_boost, 1);
}
