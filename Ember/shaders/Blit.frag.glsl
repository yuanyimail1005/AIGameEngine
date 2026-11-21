#version 450
// Copies a texture to the current render target (format conversion, e.g. to the swapchain).

layout(set = 0, binding = 0) uniform texture2D t_Source;
layout(set = 0, binding = 1) uniform sampler s_Linear;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = texture(sampler2D(t_Source, s_Linear), v_UV);
}
