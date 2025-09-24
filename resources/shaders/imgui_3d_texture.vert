#version 460 core

layout (location = 0) out vec3 out_texcoord;

uniform mat4 u_projection;
// "major" axis, i.e. the axis not expressed by "in_texcoord"
uniform int u_axis;     // 0,1,2 = x,y,z
uniform float u_level;  // texcoord along "u_axis"

// single triangle covering the entire screen
const vec2 verts[3] = vec2[] (
    vec2(-1, -1),
    vec2( 3, -1),
    vec2(-1,  3)
);
// UV [0, 1] mapped to screen corners
const vec2 uvs[3] = vec2[] (
	vec2(0, 0),
	vec2(2, 0),
	vec2(0, 2)
);

void main()
{
	vec2 uv = uvs[gl_VertexID];

	if(u_axis == 0)
		out_texcoord = vec3(u_level, uv.x, uv.y);
	else if(u_axis == 1)
		out_texcoord = vec3(uv.x, u_level, uv.y);
	else if(u_axis == 2)
		out_texcoord = vec3(uv.x, uv.y, u_level);

	gl_Position = vec4(verts[gl_VertexID], 0, 1);
}
