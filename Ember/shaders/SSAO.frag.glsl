#version 450
// Screen-space ambient occlusion: normal-oriented hemisphere sampling with range check.
#extension GL_GOOGLE_include_directive : require
#define EMBER_NO_DRAW_CONSTANTS
#include "Common.glsl"

layout(set = 0, binding = 1) uniform texture2D t_Depth;
layout(set = 0, binding = 2) uniform texture2D t_Normals;
layout(set = 0, binding = 3) uniform sampler s_PointClamp;

layout(push_constant) uniform SSAOConstants
{
	float u_Radius;
	float u_Bias;
	float u_Power;
	int u_SampleCount;
} u_SSAO;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out float o_Occlusion;

float Hash(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
	float depth = texture(sampler2D(t_Depth, s_PointClamp), v_UV).r;
	if (depth >= 1.0)
	{
		o_Occlusion = 1.0;
		return;
	}

	vec3 position = ViewPositionFromDepth(v_UV, depth);
	vec3 normal = normalize(texture(sampler2D(t_Normals, s_PointClamp), v_UV).xyz * 2.0 - 1.0);

	// Random rotation around the normal (per pixel) to trade banding for noise, removed by the blur.
	float angle = Hash(gl_FragCoord.xy) * 2.0 * PI;
	vec3 randomVector = vec3(cos(angle), sin(angle), 0.0);
	vec3 tangent = normalize(randomVector - normal * dot(randomVector, normal));
	if (any(isnan(tangent)))
		tangent = abs(normal.x) < 0.9 ? normalize(cross(normal, vec3(1, 0, 0))) : normalize(cross(normal, vec3(0, 1, 0)));
	vec3 bitangent = cross(normal, tangent);
	mat3 TBN = mat3(tangent, bitangent, normal);

	int count = clamp(u_SSAO.u_SampleCount, 4, 64);
	float occlusion = 0.0;
	for (int i = 0; i < count; i++)
	{
		// Spiral distribution over the hemisphere, denser near the center.
		float t = (float(i) + 0.5) / float(count);
		float phi = float(i) * 2.399963;
		float r = sqrt(t);
		vec3 sampleDirection = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - t, 0.0)));
		float scale = mix(0.1, 1.0, t * t);
		vec3 samplePosition = position + TBN * sampleDirection * u_SSAO.u_Radius * scale;

		vec4 offset = u_Projection * vec4(samplePosition, 1.0);
		offset.xyz /= offset.w;
		vec2 sampleUV = vec2(offset.x * 0.5 + 0.5, 0.5 - offset.y * 0.5);
		if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
			continue;

		float sceneDepth = ViewPositionFromDepth(sampleUV, texture(sampler2D(t_Depth, s_PointClamp), sampleUV).r).z;
		float rangeCheck = smoothstep(0.0, 1.0, u_SSAO.u_Radius / max(abs(position.z - sceneDepth), 1e-4));
		occlusion += (sceneDepth >= samplePosition.z + u_SSAO.u_Bias ? 1.0 : 0.0) * rangeCheck;
	}

	o_Occlusion = pow(clamp(1.0 - occlusion / float(count), 0.0, 1.0), u_SSAO.u_Power);
}
