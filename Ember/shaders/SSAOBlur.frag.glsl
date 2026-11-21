#version 450
// Separable, depth-aware (bilateral) blur of the SSAO term.

layout(set = 0, binding = 1) uniform texture2D t_Input;
layout(set = 0, binding = 2) uniform texture2D t_Depth;
layout(set = 0, binding = 3) uniform sampler s_PointClamp;

layout(push_constant) uniform BlurConstants
{
	vec2 u_TexelStep; // (1/width, 0) or (0, 1/height)
	float u_DepthSharpness;
	float u_Padding;
} u_Blur;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out float o_Occlusion;

void main()
{
	const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
	float centerDepth = texture(sampler2D(t_Depth, s_PointClamp), v_UV).r;
	float sum = texture(sampler2D(t_Input, s_PointClamp), v_UV).r * weights[0];
	float weightSum = weights[0];
	for (int i = 1; i < 5; i++)
	{
		for (int side = -1; side <= 1; side += 2)
		{
			vec2 uv = v_UV + u_Blur.u_TexelStep * float(i * side) * 1.5;
			float depth = texture(sampler2D(t_Depth, s_PointClamp), uv).r;
			float w = weights[i] * exp(-abs(depth - centerDepth) * u_Blur.u_DepthSharpness);
			sum += texture(sampler2D(t_Input, s_PointClamp), uv).r * w;
			weightSum += w;
		}
	}
	o_Occlusion = sum / weightSum;
}
