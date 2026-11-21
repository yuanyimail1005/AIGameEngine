#version 450
// Shadow map rendering for one cascade.

layout(push_constant) uniform ShadowConstants
{
	mat4 u_Model;
	mat4 u_LightViewProjection;
} u_Shadow;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord; // shadow input layout: position + texcoord only

layout(location = 0) out vec2 v_TexCoord;

void main()
{
	v_TexCoord = a_TexCoord;
	gl_Position = u_Shadow.u_LightViewProjection * u_Shadow.u_Model * vec4(a_Position, 1.0);
}
