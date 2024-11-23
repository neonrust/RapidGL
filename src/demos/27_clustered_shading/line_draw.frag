#version 460

out vec4 frag_color;

uniform vec4 u_line_color;

void main()
{
    frag_color = u_line_color;
}
