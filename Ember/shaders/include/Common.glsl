// Shared declarations for Ember shaders.
//
// Descriptor sets (must match Renderer/RenderPipelines.cpp):
//   set 0 = frame / view resources, set 1 = material resources, push constants = per-draw data.
// Frame set: b0 constants, b1 shadow cascades, b2 irradiance, b3 prefiltered, b4 BRDF LUT, b5 SSAO,
//            b6 linear clamp sampler, b7 shadow comparison sampler, b8 environment cube, b9 point clamp sampler.
// NVRHI is configured with zero Vulkan binding offsets, so GLSL bindings equal NVRHI slots.
//
// Conventions: right-handed world, Y up, camera looks down -Z. NDC +Y is up (NVRHI flips the viewport),
// depth range [0, 1]. UV (0,0) is the top-left of a render target.

#ifndef EMBER_COMMON_GLSL
#define EMBER_COMMON_GLSL

#define MAX_DIRECTIONAL_LIGHTS 4
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 16
#define MAX_CASCADES 4

const float PI = 3.14159265359;

struct DirectionalLight
{
	vec4 Direction; // xyz: direction the light travels (normalized), w: intensity
	vec4 Color;     // rgb: color, a: 1 if this light casts the cascaded shadows
};

struct PointLight
{
	vec4 PositionRange;  // xyz: world position, w: range
	vec4 ColorIntensity; // rgb: color, a: intensity
};

struct SpotLight
{
	vec4 PositionRange;  // xyz: world position, w: range
	vec4 DirectionCos;   // xyz: direction, w: cos(outer cone angle)
	vec4 ColorIntensity; // rgb: color, a: intensity
	vec4 Params;         // x: cos(inner cone angle)
};

layout(set = 0, binding = 0, std140) uniform FrameConstants
{
	mat4 u_View;
	mat4 u_Projection;
	mat4 u_ViewProjection;
	mat4 u_InverseView;
	mat4 u_InverseProjection;
	mat4 u_InverseViewProjection;
	vec4 u_CameraPosition;   // xyz
	vec4 u_ViewportSize;     // xy: size in pixels, zw: 1 / size
	vec4 u_FrameParams;      // x: time, y: near, z: far, w: SSAO enabled (1/0)
	vec4 u_SkyParams;        // x: intensity, y: rotation (radians), z: prefiltered mip count, w: draw skybox
	vec4 u_RenderParams;     // x: exposure, y: SSAO strength, z: orthographic (1/0), w: unused
	DirectionalLight u_DirectionalLights[MAX_DIRECTIONAL_LIGHTS];
	PointLight u_PointLights[MAX_POINT_LIGHTS];
	SpotLight u_SpotLights[MAX_SPOT_LIGHTS];
	ivec4 u_LightCounts;     // x: directional, y: point, z: spot
	mat4 u_ShadowMatrices[MAX_CASCADES];
	vec4 u_CascadeSplits;    // view-space distance where each cascade ends
	vec4 u_ShadowParams;     // x: cascade count (0 = no shadows), y: light size (softness), z: map size, w: normal bias (texels)
	vec4 u_CascadeInfo[MAX_CASCADES]; // x: world-space width covered by the cascade, y: world-space depth range
};

// Per-draw data (push constants), shared by the mesh shaders. Shaders with their own push constant block
// define EMBER_NO_DRAW_CONSTANTS before including this file.
#ifndef EMBER_NO_DRAW_CONSTANTS
// NVRHI limits push constants to 128 bytes, so the normal matrix is packed as three vec4 columns (120 bytes total).
layout(push_constant) uniform DrawConstants
{
	mat4 u_Model;
	vec4 u_NormalColumns[3]; // inverse-transpose of the model matrix's upper 3x3, by column
	uint u_EntityID;
	uint u_DrawFlags;        // bit 0: selected (editor)
} u_Draw;

mat3 GetNormalMatrix()
{
	return mat3(u_Draw.u_NormalColumns[0].xyz, u_Draw.u_NormalColumns[1].xyz, u_Draw.u_NormalColumns[2].xyz);
}
#endif

// Rotates a direction around +Y (used for sky rotation).
vec3 RotateY(vec3 v, float angle)
{
	float s = sin(angle);
	float c = cos(angle);
	return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// Reconstructs view-space position from a depth-buffer value and screen UV.
vec3 ViewPositionFromDepth(vec2 uv, float depth)
{
	vec4 ndc = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	vec4 view = u_InverseProjection * ndc;
	return view.xyz / view.w;
}

#endif
