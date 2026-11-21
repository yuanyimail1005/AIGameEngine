#version 450
// Generates the next mip level of a cube map (bilinear 2x2 average of the previous level).
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform textureCube t_Source;
layout(set = 0, binding = 1) uniform sampler s_Linear;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2DArray o_Cube;

layout(push_constant) uniform Constants
{
	float u_Size;        // destination size
	float u_SourceLevel; // mip to read
} u_Params;

void main()
{
	uvec3 id = gl_GlobalInvocationID;
	if (id.x >= uint(u_Params.u_Size) || id.y >= uint(u_Params.u_Size))
		return;
	vec3 d = CubeTexelDirection(id, u_Params.u_Size);
	vec3 color = textureLod(samplerCube(t_Source, s_Linear), d, u_Params.u_SourceLevel).rgb;
	imageStore(o_Cube, ivec3(id), vec4(color, 1.0));
}
