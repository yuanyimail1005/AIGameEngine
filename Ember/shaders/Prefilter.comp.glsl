#version 450
// Specular pre-filtering (split-sum approximation) for one roughness level, using filtered importance sampling.
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform textureCube t_Environment;
layout(set = 0, binding = 1) uniform sampler s_Linear;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2DArray o_Cube;

layout(push_constant) uniform Constants
{
	float u_Size;            // destination mip size
	float u_Roughness;
	float u_EnvironmentSize; // size of environment mip 0
	float u_SampleCount;
	float u_MaxSourceLevel;
} u_Params;

void main()
{
	uvec3 id = gl_GlobalInvocationID;
	if (id.x >= uint(u_Params.u_Size) || id.y >= uint(u_Params.u_Size))
		return;
	vec3 N = CubeTexelDirection(id, u_Params.u_Size);
	vec3 V = N;
	float roughness = max(u_Params.u_Roughness, 0.0);

	if (roughness < 0.001)
	{
		imageStore(o_Cube, ivec3(id), vec4(textureLod(samplerCube(t_Environment, s_Linear), N, 0.0).rgb, 1.0));
		return;
	}

	mat3 basis = TangentBasis(N);
	uint count = uint(u_Params.u_SampleCount);
	vec3 color = vec3(0.0);
	float weight = 0.0;
	float texelSolidAngle = 4.0 * PI / (6.0 * u_Params.u_EnvironmentSize * u_Params.u_EnvironmentSize);
	for (uint i = 0u; i < count; i++)
	{
		vec3 H = basis * ImportanceSampleGGX(Hammersley(i, count), roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = dot(N, L);
		if (NdotL <= 0.0)
			continue;
		float NdotH = max(dot(N, H), 0.0);
		float pdf = DistributionGGX(NdotH, roughness) * 0.25 + 1e-4;
		float sampleSolidAngle = 1.0 / (float(count) * pdf);
		float lod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, u_Params.u_MaxSourceLevel);
		color += textureLod(samplerCube(t_Environment, s_Linear), L, lod).rgb * NdotL;
		weight += NdotL;
	}
	imageStore(o_Cube, ivec3(id), vec4(color / max(weight, 1e-4), 1.0));
}
