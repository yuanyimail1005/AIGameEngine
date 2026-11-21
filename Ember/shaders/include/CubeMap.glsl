// Helpers for compute shaders that write cube maps through an image2DArray view (layer = face).
#ifndef EMBER_CUBEMAP_GLSL
#define EMBER_CUBEMAP_GLSL

const float PI = 3.14159265359;

// Direction through the center of texel (x, y) of the given face. Face order: +X, -X, +Y, -Y, +Z, -Z.
vec3 CubeTexelDirection(uvec3 id, float size)
{
	vec2 st = (vec2(id.xy) + 0.5) / size * 2.0 - 1.0; // y grows downwards
	float u = st.x;
	float v = st.y;
	vec3 direction;
	switch (id.z)
	{
		case 0u: direction = vec3(1.0, -v, -u); break;
		case 1u: direction = vec3(-1.0, -v, u); break;
		case 2u: direction = vec3(u, 1.0, v); break;
		case 3u: direction = vec3(u, -1.0, -v); break;
		case 4u: direction = vec3(u, -v, 1.0); break;
		default: direction = vec3(-u, -v, -1.0); break;
	}
	return normalize(direction);
}

// Builds an orthonormal basis around n.
mat3 TangentBasis(vec3 n)
{
	vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, n));
	vec3 bitangent = cross(n, tangent);
	return mat3(tangent, bitangent, n);
}

vec2 Hammersley(uint i, uint count)
{
	uint bits = i;
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return vec2(float(i) / float(count), float(bits) * 2.3283064365386963e-10);
}

// GGX importance sample: returns a half vector in tangent space.
vec3 ImportanceSampleGGX(vec2 xi, float roughness)
{
	float a = roughness * roughness;
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-7);
}

#endif
