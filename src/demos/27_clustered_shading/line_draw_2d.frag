#version 460 core

uniform uvec2 u_screen_size; // in pixels
uniform float u_thickness;  // line thickness, in pixels
uniform vec4 u_line_color;
uniform uvec2 u_start;       // line start in pixels
uniform uvec2 u_end;         // line end in pixels

in vec2 v_uv;
out vec4 frag_color;

void main()
{
    vec2 pixel_pos = v_uv * u_screen_size;

    vec2 a = u_start;
    vec2 b = u_end;
    vec2 pa = pixel_pos - a;
    vec2 ba = b - a;

    float h = clamp(dot(pa, ba) / dot(ba, ba), 0, 1);
    float dist = length(pa - ba * h);

  	float alpha = smoothstep(u_thickness, u_thickness - 1, dist);
   	if(alpha < 0.1)
        discard;
    else
	    frag_color = vec4(u_line_color.rgb, alpha*u_line_color.a);
}
