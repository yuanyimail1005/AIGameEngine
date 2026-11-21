#version 450
// Dear ImGui fragment shader. Colors are already in display (sRGB) space; targets are UNORM.

layout(set = 0, binding = 0) uniform texture2D t_Texture;
layout(set = 0, binding = 1) uniform sampler s_Sampler;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = v_Color * texture(sampler2D(t_Texture, s_Sampler), v_TexCoord);
}
