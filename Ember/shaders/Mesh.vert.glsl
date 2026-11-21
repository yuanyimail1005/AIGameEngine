#version 450
#extension GL_GOOGLE_include_directive : require
// Vertex shader for the depth/normal prepass and the forward PBR pass.
#include "Common.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_TexCoord;
layout(location = 4) out float v_ViewDepth;

// The prepass and forward pass must produce bit-identical depth (forward uses an EQUAL-or-less test).
invariant gl_Position;

void main()
{
	vec4 world = u_Draw.u_Model * vec4(a_Position, 1.0);
	v_WorldPosition = world.xyz;
	mat3 normalMatrix = GetNormalMatrix();
	v_Normal = normalize(normalMatrix * a_Normal);
	v_Tangent = vec4(normalize(mat3(u_Draw.u_Model) * a_Tangent.xyz), a_Tangent.w);
	v_TexCoord = a_TexCoord;
	vec4 view = u_View * world;
	v_ViewDepth = -view.z;
	gl_Position = u_Projection * view;
}
