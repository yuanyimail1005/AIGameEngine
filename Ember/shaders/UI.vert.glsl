#version 450
// Screen-space UI (panels and distance-field text). Positions are in viewport pixels, y down.

layout(push_constant) uniform UIConstants
{
	vec2 u_ViewportSize;
	float u_DistanceEdge;  // atlas value of a glyph edge (0..1)
	float u_DistanceScale; // atlas pixels per unit of atlas value
} u_UI;

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_OutlineColor;
layout(location = 4) in vec4 a_Params; // x: 1 = distance-field text, y: screen px per atlas px, z: outline width (px)

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;
layout(location = 2) out vec4 v_OutlineColor;
layout(location = 3) out vec4 v_Params;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	v_OutlineColor = a_OutlineColor;
	v_Params = a_Params;
	vec2 ndc = a_Position / u_UI.u_ViewportSize * 2.0 - 1.0;
	// Pixel coordinates are y-down; NDC +Y is up.
	gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
