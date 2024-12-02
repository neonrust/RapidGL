#version 450

out vec2 texcoord;

void main()
{
    // IDX    UV         POS
    //  0    { 0, 0 }   { -1, -1, 0, 1 )
    //  1    { 2, 0 }   {  3, -1, 0, 1 )
    //  2    { 0, 2 }   { -1,  3, 0, 1 )
    // i.e. a single triangle covering the entire screen;
    //  UV 0-1 mapped to screen corners.
    texcoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(texcoord * 2.0f - 1.0f, 0.0f, 1.0f);
}
