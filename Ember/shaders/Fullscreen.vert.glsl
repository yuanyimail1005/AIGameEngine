#version 450
// Fullscreen triangle. UV (0,0) is the top-left of the target.

layout(location = 0) out vec2 v_UV;

void main()
{
	vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
	v_UV = position;
	// NDC +Y is up: UV y = 0 (top) maps to NDC y = +1.
	gl_Position = vec4(position.x * 2.0 - 1.0, 1.0 - position.y * 2.0, 0.0, 1.0);
}
