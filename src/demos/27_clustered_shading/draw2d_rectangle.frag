#version 460 core

uniform uvec2 u_screen_size; // in pixels
uniform float u_thickness;  // line thickness, in pixels (0 = filled)
uniform vec4 u_line_color;
uniform uvec2 u_rect_min;
uniform uvec2 u_rect_max;

in vec2 v_uv;
out vec4 frag_color;

void main()
{
    // Convert UV back to screen coordinates
    vec2 pixel_pos = v_uv * u_screen_size;
    pixel_pos.y = u_screen_size.y - pixel_pos.y;

    float alpha = 0;

    if(u_thickness <= 0)
    {
	    // Distance to inside of rectangle
	    vec2 d = max(u_rect_min - pixel_pos, pixel_pos - u_rect_max);
	    float dist = length(max(d, 0)); // outside distance

	    alpha = smoothstep(1, 0, dist);
	}
	else
	{
		 float half_thickness = u_thickness * 0.5;

 		// Distance to each edge
	    float dist_left   = pixel_pos.x - u_rect_min.x + half_thickness;
	    float dist_right  = u_rect_max.x - pixel_pos.x + half_thickness;
	    float dist_top    = pixel_pos.y - u_rect_min.y + half_thickness;
	    float dist_bottom = u_rect_max.y - pixel_pos.y + half_thickness;

	    float edge_dist = min(min(dist_left, dist_right), min(dist_top, dist_bottom));

	    // Distance from outer to inner edge
	    alpha = smoothstep(0, 1, half_thickness - abs(edge_dist - half_thickness));
	}
	if (alpha < 0.05)
		discard;
	else
    	frag_color = vec4(u_line_color.rgb, alpha * u_line_color.a);
}
