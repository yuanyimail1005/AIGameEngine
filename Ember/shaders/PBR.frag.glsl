#version 450
#extension GL_GOOGLE_include_directive : require
// Forward PBR shading (metallic-roughness, GGX). Image-based lighting, cascaded soft shadows (PCSS),
// directional / point / spot lights, screen-space ambient occlusion.
#include "Common.glsl"
#include "Material.glsl"

layout(set = 0, binding = 1) uniform texture2DArray t_ShadowMap;
layout(set = 0, binding = 2) uniform textureCube t_Irradiance;
layout(set = 0, binding = 3) uniform textureCube t_Prefiltered;
layout(set = 0, binding = 4) uniform texture2D t_BRDFLut;
layout(set = 0, binding = 5) uniform texture2D t_SSAO;
layout(set = 0, binding = 6) uniform sampler s_LinearClamp;
layout(set = 0, binding = 7) uniform sampler s_ShadowCompare;
layout(set = 0, binding = 9) uniform sampler s_PointClamp; // depth formats may not support linear filtering

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord;
layout(location = 4) in float v_ViewDepth;

layout(location = 0) out vec4 o_Color;

// ---------------------------------------------------------------------------------------------------
// BRDF

float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-7);
}

float GeometrySmithGGX(float NdotV, float NdotL, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float gv = NdotV / (NdotV * (1.0 - k) + k);
	float gl = NdotL / (NdotL * (1.0 - k) + k);
	return gv * gl;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0)
{
	vec3 H = normalize(V + L);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float D = DistributionGGX(NdotH, roughness);
	float G = GeometrySmithGGX(NdotV, NdotL, roughness);
	vec3 F = FresnelSchlick(HdotV, F0);

	vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	return (kD * albedo / PI + specular) * radiance * NdotL;
}

// Smooth inverse-square falloff that reaches zero at the light's range.
float RangeAttenuation(float distance, float range)
{
	float d2 = distance * distance;
	float ratio = clamp(1.0 - pow(distance / max(range, 1e-4), 4.0), 0.0, 1.0);
	return ratio * ratio / max(d2, 1e-4);
}

// ---------------------------------------------------------------------------------------------------
// Shadows: cascaded shadow maps with percentage-closer soft shadows

const vec2 c_Poisson[16] = vec2[](
	vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
	vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
	vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
	vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float SampleShadow(vec3 worldPosition, vec3 normal, vec3 lightDirection)
{
	int cascadeCount = int(u_ShadowParams.x);
	if (cascadeCount == 0)
		return 1.0;

	int cascade = -1;
	for (int i = 0; i < cascadeCount; i++)
	{
		if (v_ViewDepth <= u_CascadeSplits[i])
		{
			cascade = i;
			break;
		}
	}
	if (cascade < 0)
		return 1.0; // beyond the shadow distance

	float mapSize = u_ShadowParams.z;
	float texel = 1.0 / mapSize;
	float cascadeWidth = u_CascadeInfo[cascade].x;
	float depthRange = u_CascadeInfo[cascade].y;
	float texelWorld = cascadeWidth * texel;

	// Normal offset proportional to the texel footprint (larger at grazing angles) avoids acne without peter-panning.
	float NdotL = clamp(dot(normal, -lightDirection), 0.0, 1.0);
	vec3 offsetPosition = worldPosition + normal * texelWorld * u_ShadowParams.w * (1.0 - 0.5 * NdotL);

	vec4 lightSpace = u_ShadowMatrices[cascade] * vec4(offsetPosition, 1.0);
	vec3 projected = lightSpace.xyz / lightSpace.w;
	vec2 uv = vec2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
	float receiver = projected.z;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || receiver >= 1.0)
		return 1.0;

	float angle = InterleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
	mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
	// Light size: tangent of the light's angular radius. Softness 1 ~ a 1.7 degree wide sun disc.
	float lightSize = max(u_ShadowParams.y, 0.0) * 0.015;
	float bias = 1.5 / 65535.0;

	// 1. Blocker search over the region that could shadow this point (occluders up to ~8 m away).
	float searchRadius = clamp(lightSize * 8.0 / cascadeWidth, 2.0 * texel, 24.0 * texel);
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	for (int i = 0; i < 16; i++)
	{
		vec2 offset = rotation * c_Poisson[i] * searchRadius;
		float depth = texture(sampler2DArray(t_ShadowMap, s_PointClamp), vec3(uv + offset, float(cascade))).r;
		if (depth < receiver - bias)
		{
			blockerSum += depth;
			blockerCount += 1.0;
		}
	}
	if (blockerCount < 1.0)
		return 1.0;
	float blocker = blockerSum / blockerCount;

	// 2. Penumbra width from the world-space distance between blocker and receiver (similar triangles).
	float penumbraWorld = (receiver - blocker) * depthRange * lightSize;
	float filterRadius = clamp(penumbraWorld / cascadeWidth, 1.0 * texel, 24.0 * texel);

	// 3. PCF with hardware depth comparison
	float lit = 0.0;
	for (int i = 0; i < 16; i++)
	{
		vec2 offset = rotation * c_Poisson[i] * filterRadius;
		lit += texture(sampler2DArrayShadow(t_ShadowMap, s_ShadowCompare), vec4(uv + offset, float(cascade), receiver - bias));
	}
	return lit / 16.0;
}

// ---------------------------------------------------------------------------------------------------

vec3 GetNormal(vec2 uv)
{
	vec3 N = normalize(v_Normal);
	if (!gl_FrontFacing)
		N = -N;
	if (m_TextureFlags.z == 0u)
		return N;

	vec3 tangentNormal = texture(sampler2D(t_Normal, s_Material), uv).xyz * 2.0 - 1.0;
	tangentNormal.xy *= m_Params.z;

	vec3 T;
	vec3 B;
	if (dot(v_Tangent.xyz, v_Tangent.xyz) > 1e-6)
	{
		T = normalize(v_Tangent.xyz - N * dot(N, v_Tangent.xyz));
		B = cross(N, T) * (v_Tangent.w < 0.0 ? -1.0 : 1.0);
	}
	else
	{
		// No tangents: derive a basis from screen-space derivatives.
		vec3 dp1 = dFdx(v_WorldPosition);
		vec3 dp2 = dFdy(v_WorldPosition);
		vec2 duv1 = dFdx(uv);
		vec2 duv2 = dFdy(uv);
		vec3 dp2perp = cross(dp2, N);
		vec3 dp1perp = cross(N, dp1);
		T = dp2perp * duv1.x + dp1perp * duv2.x;
		B = dp2perp * duv1.y + dp1perp * duv2.y;
		float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
		T *= invmax;
		B *= invmax;
	}
	return normalize(mat3(T, B, N) * tangentNormal);
}

void main()
{
	vec2 uv = MaterialUV(v_TexCoord);
	vec4 baseColor = ApplyAlphaMask(uv);
	vec3 albedo = baseColor.rgb;

	float metallic = m_Params.x;
	float roughness = m_Params.y;
	if (m_TextureFlags.y != 0u)
	{
		vec4 mr = texture(sampler2D(t_MetallicRoughness, s_Material), uv);
		roughness *= mr.g;
		metallic *= mr.b;
	}
	roughness = clamp(roughness, 0.045, 1.0);
	metallic = clamp(metallic, 0.0, 1.0);

	float occlusion = 1.0;
	if (m_TextureFlags.w != 0u)
		occlusion = mix(1.0, texture(sampler2D(t_Occlusion, s_Material), uv).r, m_Params.w);
	if (u_FrameParams.w > 0.5 && m_AlphaParams.x != 2.0)
	{
		vec2 screenUV = gl_FragCoord.xy * u_ViewportSize.zw;
		float ssao = texture(sampler2D(t_SSAO, s_LinearClamp), screenUV).r;
		occlusion *= mix(1.0, ssao, u_RenderParams.y);
	}

	vec3 emissive = m_Emissive.rgb;
	if (m_TextureFlags2.x != 0u)
		emissive *= texture(sampler2D(t_Emissive, s_Material), uv).rgb;

	vec3 N = GetNormal(uv);
	// Orthographic cameras have a constant view direction: the camera's +Z axis (it looks down -Z).
	vec3 V = u_RenderParams.z > 0.5 ? normalize(u_InverseView[2].xyz) : normalize(u_CameraPosition.xyz - v_WorldPosition);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	vec3 color = vec3(0.0);

	for (int i = 0; i < u_LightCounts.x; i++)
	{
		DirectionalLight light = u_DirectionalLights[i];
		vec3 L = -light.Direction.xyz;
		float shadow = light.Color.a > 0.5 ? SampleShadow(v_WorldPosition, normalize(v_Normal), light.Direction.xyz) : 1.0;
		color += EvaluateLight(N, V, L, light.Color.rgb * light.Direction.w * shadow, albedo, metallic, roughness, F0);
	}

	for (int i = 0; i < u_LightCounts.y; i++)
	{
		PointLight light = u_PointLights[i];
		vec3 toLight = light.PositionRange.xyz - v_WorldPosition;
		float distance = length(toLight);
		if (distance >= light.PositionRange.w)
			continue;
		vec3 L = toLight / max(distance, 1e-4);
		vec3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * RangeAttenuation(distance, light.PositionRange.w);
		color += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
	}

	for (int i = 0; i < u_LightCounts.z; i++)
	{
		SpotLight light = u_SpotLights[i];
		vec3 toLight = light.PositionRange.xyz - v_WorldPosition;
		float distance = length(toLight);
		if (distance >= light.PositionRange.w)
			continue;
		vec3 L = toLight / max(distance, 1e-4);
		float cosAngle = dot(-L, light.DirectionCos.xyz);
		float cone = smoothstep(light.DirectionCos.w, light.Params.x, cosAngle);
		if (cone <= 0.0)
			continue;
		vec3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.a * RangeAttenuation(distance, light.PositionRange.w) * cone;
		color += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
	}

	// Image-based ambient lighting
	float NdotV = max(dot(N, V), 1e-4);
	vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 kD = (1.0 - F) * (1.0 - metallic);
	vec3 irradiance = texture(samplerCube(t_Irradiance, s_LinearClamp), RotateY(N, -u_SkyParams.y)).rgb;
	vec3 R = reflect(-V, N);
	float lod = roughness * max(u_SkyParams.z - 1.0, 0.0);
	vec3 prefiltered = textureLod(samplerCube(t_Prefiltered, s_LinearClamp), RotateY(R, -u_SkyParams.y), lod).rgb;
	vec2 brdf = texture(sampler2D(t_BRDFLut, s_LinearClamp), vec2(NdotV, roughness)).rg;
	vec3 ambient = (kD * irradiance * albedo + prefiltered * (F * brdf.x + brdf.y)) * u_SkyParams.x;
	color += ambient * occlusion;

	color += emissive;

	float alpha = m_AlphaParams.x == 2.0 ? baseColor.a : 1.0;
	o_Color = vec4(color, alpha);
}
