#version 450

out vec2 v_uv;

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
	v_uv = uvs[gl_VertexID];
	gl_Position = vec4(verts[gl_VertexID], 0, 1);
}
