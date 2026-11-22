#version 450
// Screen-space UI. Colors are display (sRGB) values written straight to the UNORM LDR target, like the tonemapper's
// output. Text samples a single-channel signed distance field; coverage is computed from the distance in screen
// pixels, which keeps edges antialiased at any size without derivatives.

layout(push_constant) uniform UIConstants
{
	vec2 u_ViewportSize;
	float u_DistanceEdge;
	float u_DistanceScale;
} u_UI;

layout(set = 0, binding = 0) uniform texture2D t_Texture;
layout(set = 0, binding = 1) uniform sampler s_Sampler;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec4 v_OutlineColor;
layout(location = 3) in vec4 v_Params;

layout(location = 0) out vec4 o_Color;

void main()
{
	vec4 texel = texture(sampler2D(t_Texture, s_Sampler), v_TexCoord);
	if (v_Params.x < 0.5)
	{
		o_Color = v_Color * texel;
		return;
	}

	// Signed distance to the glyph edge in screen pixels (positive inside).
	float distance = (texel.r - u_UI.u_DistanceEdge) * u_UI.u_DistanceScale * v_Params.y;
	float fill = clamp(distance + 0.5, 0.0, 1.0) * v_Color.a;
	float outline = v_Params.z > 0.0 ? clamp(distance + v_Params.z + 0.5, 0.0, 1.0) * v_OutlineColor.a : 0.0;

	// Fill composited over the outline (premultiplied), then un-premultiplied for SrcAlpha blending.
	float alpha = fill + outline * (1.0 - fill);
	vec3 color = v_Color.rgb * fill + v_OutlineColor.rgb * outline * (1.0 - fill);
	o_Color = alpha > 0.0 ? vec4(color / alpha, alpha) : vec4(0.0);
}
