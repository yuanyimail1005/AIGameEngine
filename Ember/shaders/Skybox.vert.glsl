#version 450
// Fullscreen triangle on the far plane; outputs the view direction for each pixel.
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(location = 0) out vec3 v_Direction;

void main()
{
	vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 viewDirection = u_InverseProjection * vec4(ndc, 1.0, 1.0);
	v_Direction = mat3(u_InverseView) * (viewDirection.xyz / viewDirection.w);
	gl_Position = vec4(ndc, 1.0, 1.0);
}
