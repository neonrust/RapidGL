#version 460

layout(location = 0) in vec2 texcoord;
layout(binding = 0) uniform sampler2D u_albedo_texture;

void main()
{
	// NOTE: this of course doesn't work if the rendered mesh doesn't have an albedo texture.
	//   Making this decision correctly, requires a Material instance.
	// float alpha = texture(u_albedo_texture, texcoord).a;
	// if(alpha < 0.5) discard;
}
