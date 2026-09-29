#version 450

layout(set=0,binding=0) uniform sampler2D baseColorTexture;
layout(set=0,binding=1) uniform sampler2D normalTexture;
layout(set=0,binding=2) uniform sampler2D metallicRoughnessTexture;

layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 baseColor;
layout(location=4) in vec4 material;
layout(location=0) out vec4 outColor;

// 0: hardware sRGB conversion (normal OpenXR hand target)
// 1: explicit conversion (DOOM's UNORM final-color target)
layout(constant_id=0) const uint outputTransfer = 0;

vec3 linearToSrgb(vec3 value) {
    value = max(value, vec3(0.0));
    vec3 low = value * 12.92;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), value));
}

vec3 normalFromMap(vec3 geometricNormal) {
    vec3 dpdx = dFdx(worldPosition);
    vec3 dpdy = dFdy(worldPosition);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(determinant) < 1e-6) return geometricNormal;
    vec3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y)
        / determinant);
    vec3 bitangent = normalize((-dpdx * duvdy.x + dpdy * duvdx.x)
        / determinant);
    vec3 mapped = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    return normalize(tangent * mapped.x + bitangent * mapped.y
        + geometricNormal * mapped.z);
}

void main() {
    if(material.x < 0.0) {
        outColor=vec4(outputTransfer==1 ? linearToSrgb(baseColor.rgb) : baseColor.rgb,1.0);
        return;
    }
    vec4 albedo = texture(baseColorTexture, uv) * baseColor;
    vec3 normal = normalize(worldNormal);
    // Blender marks the supplied hand material double-sided. glTF requires
    // the normal to face the viewer on a back-facing fragment. Without this,
    // the palm underside is lit as an inside surface and looks transparent or
    // as though the texture has slipped even though blending is disabled.
    if (!gl_FrontFacing) normal = -normal;
    if (material.z > 0.5) normal = normalFromMap(normal);

    float metallic = material.x;
    float roughness = material.y;
    if (material.w > 0.5) {
        vec4 packed = texture(metallicRoughnessTexture, uv);
        roughness *= packed.g;
        metallic *= packed.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // Stable first-pass environment light; idTech 6 scene lights and shadows
    // are intentionally outside this renderer.
    vec3 lightDirection = normalize(vec3(-0.35, 0.75, 0.55));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 halfVector = normalize(lightDirection + normalize(vec3(0.2,0.2,1.0)));
    float specularPower = mix(96.0, 8.0, roughness);
    float specular = pow(max(dot(normal, halfVector), 0.0), specularPower);
    vec3 dielectric = albedo.rgb * (0.28 + 0.72 * diffuse);
    vec3 metalSpecular = mix(vec3(0.04), albedo.rgb, metallic) * specular;
    vec3 litColor = dielectric * (1.0 - metallic * 0.35) + metalSpecular;
    if (outputTransfer != 0) litColor = linearToSrgb(litColor);
    outColor = vec4(litColor, albedo.a);
}
