#version 450
layout(push_constant) uniform Mask { vec4 corners[4]; } mask;
void main() { gl_Position = mask.corners[gl_VertexIndex]; }
