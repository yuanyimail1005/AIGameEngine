#version 450
// Diffuse irradiance convolution (cosine-weighted hemisphere integral of the environment).
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform textureCube t_Environment;
layout(set = 0, binding = 1) uniform sampler s_Linear;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2DArray o_Cube;

layout(push_constant) uniform Constants
{
	float u_Size;
	float u_SourceLevel; // pre-blurred environment mip to sample (reduces noise / aliasing)
	float u_SampleDelta; // angular step in radians
} u_Params;

void main()
{
	uvec3 id = gl_GlobalInvocationID;
	if (id.x >= uint(u_Params.u_Size) || id.y >= uint(u_Params.u_Size))
		return;
	vec3 N = CubeTexelDirection(id, u_Params.u_Size);
	mat3 basis = TangentBasis(N);

	vec3 irradiance = vec3(0.0);
	float count = 0.0;
	float delta = max(u_Params.u_SampleDelta, 0.01);
	for (float phi = 0.0; phi < 2.0 * PI; phi += delta)
	{
		for (float theta = 0.0; theta < 0.5 * PI; theta += delta)
		{
			vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			vec3 direction = basis * tangentSample;
			irradiance += textureLod(samplerCube(t_Environment, s_Linear), direction, u_Params.u_SourceLevel).rgb * cos(theta) * sin(theta);
			count += 1.0;
		}
	}
	irradiance = PI * irradiance / max(count, 1.0);
	imageStore(o_Cube, ivec3(id), vec4(irradiance, 1.0));
}
