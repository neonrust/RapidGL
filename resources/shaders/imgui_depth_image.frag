#version 460 core

layout (location = 0) in vec2 texcoord;

layout (location = 0) out vec4 frag_color;

layout (binding = 0) uniform sampler2D u_depth;

uniform float u_brightness;

void main()
{
	float d = texture(u_depth, texcoord).r;     // raw depth
	d = pow(d, 0.5);                            // optional gamma
	d = 1 - d;                                  // invert; brighter = closer
	d *= u_brightness;
	if(d == 0)
		frag_color = vec4(0.2, 0.4, 0, 1);
	else
		frag_color = vec4(d, d, d, 1);              // grayscale output
}
