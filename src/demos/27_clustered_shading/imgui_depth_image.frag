#version 460 core

in vec2 Frag_UV;
in vec4 Frag_Color;
layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler2D u_depth;

void main()
{
	float d = 1 - texture(u_depth, Frag_UV).r;  // raw depth
	d = pow(d, 0.4);                        // optional gamma
	frag_color = vec4(d, d, d, 1);           // grayscale output
}
