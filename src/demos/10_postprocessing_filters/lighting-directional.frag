#version 460 core
#include "../03_lighting/lighting.glh"

uniform DirectionalLight directional_light;

void main()
{
    frag_color = calcDirectionalLight(directional_light, normalize(normal), world_pos);
} 