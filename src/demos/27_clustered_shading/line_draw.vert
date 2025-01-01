#version 460
layout(location = 0) in vec3 in_pos;

uniform mat4 u_view_projection;

void main()
{
    gl_Position = u_view_projection * vec4(in_pos, 1);
}
