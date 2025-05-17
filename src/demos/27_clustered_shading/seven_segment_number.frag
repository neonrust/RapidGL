#version 460

uniform uvec2 u_screen_size;
uniform float u_thickness;
uniform vec4  u_color;

uniform uint  u_number;
uniform uvec2 u_bottom_right;
uniform float u_height;

in vec2 v_uv;
out vec4 frag_color;

//   --A--
//  |     |
//  F     B
//  |     |
//   --G--
//  |     |
//  E     C
//  |     |
//   --D--
const uint A = 1u << 0;  // top
const uint B = 1u << 1;  // top right
const uint C = 1u << 2;  // bottom right
const uint D = 1u << 3;  // bottom
const uint E = 1u << 4;  // bottom left
const uint F = 1u << 5;  // top left
const uint G = 1u << 6;  // middle

const uint SEGMENT_MASKS[7] = { A, B, C, D, E, F, G };
const uint MAX_DIGITS = 4;

const uint DIGIT_SEGMENTS[10] = uint[10](
    A|B|C|D|E|F,   // 0 -> 0111111
    B|C,           // 1 -> 0000110
    A|B|D|E|G,     // 2 -> 1011011
    A|B|C|D|G,     // 3 -> 1001111
    B|C|F|G,       // 4 -> 1100110
    A|C|D|F|G,     // 5 -> 1101101
    A|C|D|E|F|G,   // 6 -> 1111101
    A|B|C,         // 7 -> 0000111
    A|B|C|D|E|F|G, // 8 -> 1111111
    A|B|C|D|F|G    // 9 -> 1101111
);

float draw_line(vec2 p, vec2 a, vec2 b);
vec4 get_segment_points(uint segment, float width, float height, float pad);

void main()
{
    vec2 frag_px = v_uv * vec2(u_screen_size);

    // Convert Y to top-down
    frag_px.y = u_screen_size.y - frag_px.y;

    float h = u_height;
    float width = h * 0.5;
    vec2 top_left = u_bottom_right - vec2(width, h);
    float pad = 1 + width / 8 + u_thickness/2;
    float spacing = width / 2;

    uint number = u_number;
    float alpha = 0;
    for(uint idx = 0; idx < MAX_DIGITS && (number > 0 || idx == 0); idx++)
    {
    	uint digit = number % 10;

	    uint segments = DIGIT_SEGMENTS[clamp(digit, 0u, 9u)];

	    vec4 corner = vec4(top_left, top_left);

	    for(uint segment = 0; segment < SEGMENT_MASKS.length(); segment++)
	    {
	    	uint segment_mask = SEGMENT_MASKS[segment];
		    if ((segments & segment_mask) > 0)
			{
				vec4 points = get_segment_points(segment_mask, width, h, pad) + corner;
	    		alpha += draw_line(frag_px, points.xy, points.zw);
	      	}
	    }
    	number /= 10;
     	top_left.x -= width + spacing;
	}
    alpha = min(alpha, 1);
    if (alpha < 0.05)
        discard;

    frag_color = vec4(u_color.rgb, alpha*u_color.a);
}

float draw_line(vec2 p, vec2 a, vec2 b)
{
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	float dist = length(pa - ba * h);

	float alpha = smoothstep(u_thickness, u_thickness - 1, dist);
	return alpha;
}

vec4 get_segment_points(uint segment, float width, float height, float pad)
{
	float mid_y = height/2;
	float edge = u_thickness/2;
	width -= edge;
	height -= edge;

	vec2 p0, p1;

	if (segment == A) // Top horizontal
	{
	    p0 = vec2(pad,         edge);
	    p1 = vec2(width - pad, edge);
	}
	else if (segment == B) // Top-right vertical
	{
	    p0 = vec2(width, pad + edge);
	    p1 = vec2(width, mid_y - pad);
	}
	else if (segment == C) // Bottom-right vertical
	{
	    p0 = vec2(width, mid_y + pad);
	    p1 = vec2(width, height - pad);
	}
	else if (segment == D) // Bottom horizontal
	{
	    p0 = vec2(pad,         height);
	    p1 = vec2(width - pad, height);
	}
	else if (segment == E) // Bottom-left vertical
	{
	    p0 = vec2(edge, mid_y + pad);
	    p1 = vec2(edge, height - pad);
	}
	else if (segment == F) // Top-left vertical
	{
	    p0 = vec2(edge, pad + edge);
	    p1 = vec2(edge, mid_y - pad);
	}
	else if (segment == G) // Middle horizontal
	{
	    p0 = vec2(pad + edge,  mid_y);
	    p1 = vec2(width - pad, mid_y);
	}

	return vec4(p0, p1);
}
