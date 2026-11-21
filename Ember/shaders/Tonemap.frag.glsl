#version 450
// HDR -> display: exposure, tonemapping (ACES fitted / Reinhard / none), sRGB encoding.

layout(set = 0, binding = 0) uniform texture2D t_HDR;
layout(set = 0, binding = 1) uniform sampler s_Linear;

layout(push_constant) uniform TonemapConstants
{
	float u_Exposure;
	int u_Operator; // 0: ACES, 1: Reinhard, 2: none (clamp)
	float u_Gamma;
	float u_Padding;
} u_Tonemap;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

// Stephen Hill's fitted ACES approximation.
vec3 ACESFitted(vec3 color)
{
	const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
	const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
	color = inputMatrix * color;
	vec3 a = color * (color + 0.0245786) - 0.000090537;
	vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	color = a / b;
	return clamp(outputMatrix * color, 0.0, 1.0);
}

vec3 LinearToSRGB(vec3 color)
{
	vec3 low = color * 12.92;
	vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
	return mix(high, low, lessThanEqual(color, vec3(0.0031308)));
}

void main()
{
	vec3 hdr = max(texture(sampler2D(t_HDR, s_Linear), v_UV).rgb, vec3(0.0)) * u_Tonemap.u_Exposure;
	vec3 mapped;
	if (u_Tonemap.u_Operator == 0)
		mapped = ACESFitted(hdr);
	else if (u_Tonemap.u_Operator == 1)
		mapped = hdr / (hdr + 1.0);
	else
		mapped = clamp(hdr, 0.0, 1.0);
	o_Color = vec4(u_Tonemap.u_Gamma > 0.0 ? LinearToSRGB(mapped) : mapped, 1.0);
}
