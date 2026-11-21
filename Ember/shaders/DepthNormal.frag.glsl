#version 450
#extension GL_GOOGLE_include_directive : require
// Prepass: writes view-space normals (for SSAO) and entity IDs (for editor picking). Depth is written by the
// rasterizer and reused by the forward pass with an EQUAL depth test.
#include "Common.glsl"
#include "Material.glsl"

layout(location = 1) in vec3 v_Normal;
layout(location = 3) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Normal;
layout(location = 1) out uint o_EntityID;

void main()
{
	ApplyAlphaMask(MaterialUV(v_TexCoord));
	vec3 n = normalize(v_Normal);
	if (!gl_FrontFacing)
		n = -n;
	vec3 viewNormal = normalize(mat3(u_View) * n);
	o_Normal = vec4(viewNormal * 0.5 + 0.5, 1.0);
	o_EntityID = u_Draw.u_EntityID;
}
