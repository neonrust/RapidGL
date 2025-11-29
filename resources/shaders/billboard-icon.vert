#version 460 core

layout(location = 0) in vec3 in_world_pos;  // per-instance: center
layout(location = 1) in uint in_icon_index;
layout(location = 2) in vec3 in_color;

layout(location = 0)      out vec2 out_uv;
layout(location = 1) flat out uint out_icon_index;
layout(location = 2) flat out vec3 out_color;

uniform mat4 u_view;
uniform mat4 u_projection;

const vec2 quad_vertices[4] = vec2[4](
    vec2(-1, -1),
    vec2( 1, -1),
    vec2(-1,  1),
    vec2( 1,  1)
);

void main()
{
    // Camera right/up vectors (columns of the view matrix)
    vec3 cam_right = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);
    vec3 cam_up    = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);

    // Scale (constant screen size or distance-based)
    float size = 0.5;

    vec2 quad_vertex = quad_vertices[gl_VertexID];

    // Offset in world space
    vec3 offset = (quad_vertex.x * cam_right + quad_vertex.y * cam_up) * size;
    vec3 world_pos = in_world_pos + offset;

    gl_Position = u_projection * u_view * vec4(world_pos, 1);

    // Map quad corners to [0,1] UV
    out_uv = quad_vertex * 0.5 + 0.5;
    out_uv.y = 1 - out_uv.y;
    out_icon_index = in_icon_index;

    out_color = in_color;
}
