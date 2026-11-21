#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(set = 0, binding = 6) uniform sampler s_LinearClamp;
layout(set = 0, binding = 8) uniform textureCube t_Environment;

layout(location = 0) in vec3 v_Direction;
layout(location = 0) out vec4 o_Color;

void main()
{
	vec3 direction = normalize(v_Direction);
	vec3 color = textureLod(samplerCube(t_Environment, s_LinearClamp), RotateY(direction, -u_SkyParams.y), 0.0).rgb;
	o_Color = vec4(color * u_SkyParams.x, 1.0);
}
