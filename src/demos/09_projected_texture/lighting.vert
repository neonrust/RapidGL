#version 460 core
layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec3 in_normal;

uniform mat4 model;
uniform mat4 mvp;
uniform mat3 normal_matrix;

out vec2 texcoord;
out vec3 world_pos;
out vec3 normal;

void main()
{
    world_pos = vec3(model * vec4(in_pos, 1.0));
    texcoord  = in_texcoord;
    normal    = normal_matrix * in_normal;

    gl_Position = mvp * vec4(in_pos, 1.0);
}