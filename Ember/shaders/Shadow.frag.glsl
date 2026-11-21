#version 450
#extension GL_GOOGLE_include_directive : require
// Shadow pass fragment shader: only needed for alpha-masked materials (depth is written automatically).
#include "Material.glsl"

layout(location = 0) in vec2 v_TexCoord;

void main()
{
	ApplyAlphaMask(v_TexCoord);
}
