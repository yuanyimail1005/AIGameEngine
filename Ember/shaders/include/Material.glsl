// Material descriptor set (set 1). Must match Renderer/Material.cpp.
#ifndef EMBER_MATERIAL_GLSL
#define EMBER_MATERIAL_GLSL

layout(set = 1, binding = 0, std140) uniform MaterialConstants
{
	vec4 m_BaseColor;
	vec4 m_Emissive;       // rgb: emissive color * strength
	vec4 m_Params;         // x: metallic, y: roughness, z: normal scale, w: occlusion strength
	vec4 m_AlphaParams;    // x: alpha mode (0 opaque, 1 mask, 2 blend), y: cutoff
	uvec4 m_TextureFlags;  // x: base color, y: metallic-roughness, z: normal, w: occlusion (1 = present)
	uvec4 m_TextureFlags2; // x: emissive
};

layout(set = 1, binding = 1) uniform texture2D t_BaseColor;
layout(set = 1, binding = 2) uniform texture2D t_MetallicRoughness;
layout(set = 1, binding = 3) uniform texture2D t_Normal;
layout(set = 1, binding = 4) uniform texture2D t_Occlusion;
layout(set = 1, binding = 5) uniform texture2D t_Emissive;
layout(set = 1, binding = 6) uniform sampler s_Material;

vec4 SampleBaseColor(vec2 uv)
{
	vec4 color = m_BaseColor;
	if (m_TextureFlags.x != 0u)
		color *= texture(sampler2D(t_BaseColor, s_Material), uv);
	return color;
}

// Discards fragments for alpha-masked materials. Returns the base color.
vec4 ApplyAlphaMask(vec2 uv)
{
	vec4 color = SampleBaseColor(uv);
	if (m_AlphaParams.x == 1.0 && color.a < m_AlphaParams.y)
		discard;
	return color;
}

#endif
