#version 450

in vec2 v_uv;
out vec4 frag_color;

layout(binding = 0) uniform sampler2D u_texture;

void main()
{
	frag_color = texture(u_texture, v_uv);
}
