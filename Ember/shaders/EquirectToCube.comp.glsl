#version 450
// Converts an equirectangular HDR panorama into a cube map. The panorama's center faces -Z.
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D t_Equirect;
layout(set = 0, binding = 1) uniform sampler s_Linear;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2DArray o_Cube;

layout(push_constant) uniform Constants
{
	float u_Size;
} u_Params;

void main()
{
	uvec3 id = gl_GlobalInvocationID;
	if (id.x >= uint(u_Params.u_Size) || id.y >= uint(u_Params.u_Size))
		return;
	vec3 d = CubeTexelDirection(id, u_Params.u_Size);
	vec2 uv = vec2(atan(d.x, -d.z) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
	vec3 color = textureLod(sampler2D(t_Equirect, s_Linear), uv, 0.0).rgb;
	// Clamp extreme values (sun pixels) to keep the convolutions stable.
	imageStore(o_Cube, ivec3(id), vec4(min(color, vec3(64000.0)), 1.0));
}
