#version 450
// Split-sum environment BRDF lookup table: x = NdotV, y = roughness -> (scale, bias) applied to F0.
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D o_Lut;

layout(push_constant) uniform Constants
{
	float u_Size;
	float u_SampleCount;
} u_Params;

float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
	float k = (roughness * roughness) / 2.0;
	return NdotV / (NdotV * (1.0 - k) + k);
}

void main()
{
	uvec2 id = gl_GlobalInvocationID.xy;
	if (id.x >= uint(u_Params.u_Size) || id.y >= uint(u_Params.u_Size))
		return;
	float NdotV = max((float(id.x) + 0.5) / u_Params.u_Size, 1e-3);
	float roughness = (float(id.y) + 0.5) / u_Params.u_Size;
	vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

	float a = 0.0;
	float b = 0.0;
	uint count = uint(u_Params.u_SampleCount);
	for (uint i = 0u; i < count; i++)
	{
		vec3 H = ImportanceSampleGGX(Hammersley(i, count), roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		float NdotL = max(L.z, 0.0);
		float NdotH = max(H.z, 0.0);
		float VdotH = max(dot(V, H), 0.0);
		if (NdotL > 0.0)
		{
			float G = GeometrySchlickGGX_IBL(NdotV, roughness) * GeometrySchlickGGX_IBL(NdotL, roughness);
			float Gvis = G * VdotH / max(NdotH * NdotV, 1e-4);
			float Fc = pow(1.0 - VdotH, 5.0);
			a += (1.0 - Fc) * Gvis;
			b += Fc * Gvis;
		}
	}
	imageStore(o_Lut, ivec2(id), vec4(a / float(count), b / float(count), 0.0, 1.0));
}
