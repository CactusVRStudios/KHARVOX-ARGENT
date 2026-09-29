#version 450

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProjection;
} pc;

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUv;
layout(location=3) in vec4 inBaseColor;
layout(location=4) in vec4 inMaterial;

layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
layout(location=3) out vec4 baseColor;
layout(location=4) out vec4 material;

void main() {
    vec4 world = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.viewProjection * world;
    worldPosition = world.xyz;
    worldNormal = normalize(mat3(pc.model) * inNormal);
    uv = inUv;
    baseColor = inBaseColor;
    material = inMaterial;
}
