#version 450

// Soruce: https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

in vec2 v_uv;
out vec4 frag_color;

layout(binding = 0) uniform sampler2D u_filter_texture;

uniform float u_exposure;
uniform float u_gamma;
uniform float u_saturation;

vec3 gammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0/u_gamma));
}

vec3 Tonemap_Filmic_UC2(vec3 linearColor, float linearWhite, float A, float B, float C, float D, float E, float F)
{
	// Uncharted II configurable tonemapper.

	// A = shoulder strength
	// B = linear strength
	// C = linear angle
	// D = toe strength
	// E = toe numerator
	// F = toe denominator
	// Note: E / F = toe angle
	// linearWhite = linear white point value

	vec3 x = linearColor;
	vec3 color = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;

	x = vec3(linearWhite);
	vec3 white = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;

	return color / white;
}

vec3 Tonemap_Filmic_UC2Default(vec3 linearColor) {

	// Uncharted II fixed tonemapping formula.
	// Gives a warm and gritty image, saturated shadows and bleached highlights.

	return Tonemap_Filmic_UC2(linearColor, 11.2, 0.22, 0.3, 0.1, 0.2, 0.01, 0.30);
}

vec3 hsv2rgb(vec3 hsvColor);
vec3 rgb2hsv(vec3 rgbColor);

void main()
{
	vec4 x = u_exposure * texture(u_filter_texture, v_uv);

    vec3 color = Tonemap_Filmic_UC2Default(x.rgb);
		 color = gammaCorrect(color);

	vec3 color_hsv = rgb2hsv(color);
	color_hsv.g *= u_saturation;
	color = hsv2rgb(color_hsv);

	frag_color = vec4(color, 1.0);
}

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0, -1.0 / 3.0, 2.0 / 3.0, -1);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1, 2.0 / 3.0, 1.0 / 3.0, 3);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0, 1), c.y);
}
