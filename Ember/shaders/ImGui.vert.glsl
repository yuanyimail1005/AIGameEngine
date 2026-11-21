#version 450
// Dear ImGui vertex shader.

layout(push_constant) uniform ImGuiConstants
{
	vec2 u_Scale;
	vec2 u_Translate;
} u_ImGui;

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	vec2 ndc = a_Position * u_ImGui.u_Scale + u_ImGui.u_Translate;
	// ImGui uses y-down screen coordinates; NDC +Y is up.
	gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
