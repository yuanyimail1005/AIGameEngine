#version 450
// Writes a simple procedural sky (gradient + sun glow) into a cube map. Used when no HDRI is assigned.
#extension GL_GOOGLE_include_directive : require
#include "CubeMap.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2DArray o_Cube;

layout(push_constant) uniform SkyConstants
{
	vec4 u_Zenith;
	vec4 u_Horizon;
	vec4 u_Ground;
	vec4 u_SunDirection; // xyz: direction towards the sun, w: sun intensity
	float u_Size;
} u_Sky;

void main()
{
	uvec3 id = gl_GlobalInvocationID;
	if (id.x >= uint(u_Sky.u_Size) || id.y >= uint(u_Sky.u_Size))
		return;
	vec3 direction = CubeTexelDirection(id, u_Sky.u_Size);
	float h = direction.y;
	vec3 color;
	if (h >= 0.0)
		color = mix(u_Sky.u_Horizon.rgb, u_Sky.u_Zenith.rgb, pow(h, 0.6));
	else
		color = mix(u_Sky.u_Horizon.rgb, u_Sky.u_Ground.rgb, pow(-h, 0.4));
	float sun = max(dot(direction, normalize(u_Sky.u_SunDirection.xyz)), 0.0);
	color += vec3(1.0, 0.95, 0.85) * u_Sky.u_SunDirection.w * (pow(sun, 64.0) * 0.5 + pow(sun, 2048.0) * 20.0);
	imageStore(o_Cube, ivec3(id), vec4(color, 1.0));
}
