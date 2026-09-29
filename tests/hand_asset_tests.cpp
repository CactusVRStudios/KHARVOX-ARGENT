#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
bool nodeInScene(const cgltf_node* node, const cgltf_scene* scene) {
    if (!scene) return true;
    const cgltf_node* root = node;
    while (root && root->parent) root = root->parent;
    for (cgltf_size index = 0; index < scene->nodes_count; ++index)
        if (scene->nodes[index] == root) return true;
    return false;
}

bool validate(const std::filesystem::path& path,
    const char* expectedSceneName) {
    cgltf_options options{};
    cgltf_data* data{};
    const auto name = path.u8string();
    if (cgltf_parse_file(&options, name.c_str(), &data)
            != cgltf_result_success)
        return false;
    const bool buffersLoaded = cgltf_load_buffers(&options, data, name.c_str())
        == cgltf_result_success;
    const cgltf_node* meshNode{};
    const cgltf_primitive* primitive{};
    if (buffersLoaded && data->scene) {
        for (cgltf_size index = 0; index < data->nodes_count; ++index) {
            const auto& node = data->nodes[index];
            if (node.mesh && node.mesh->primitives_count
                    && nodeInScene(&node, data->scene)) {
                meshNode = &node;
                primitive = &node.mesh->primitives[0];
                break;
            }
        }
    }
    bool position{}, normal{}, uv{}, joints{}, weights{};
    if (primitive) {
        for (cgltf_size index = 0; index < primitive->attributes_count;
                ++index) {
            const auto& attribute = primitive->attributes[index];
            position |= attribute.type == cgltf_attribute_type_position;
            normal |= attribute.type == cgltf_attribute_type_normal;
            uv |= attribute.type == cgltf_attribute_type_texcoord
                && attribute.index == 0;
            joints |= attribute.type == cgltf_attribute_type_joints
                && attribute.index == 0;
            weights |= attribute.type == cgltf_attribute_type_weights
                && attribute.index == 0;
        }
    }
    const bool staticPose = meshNode && !meshNode->skin && !joints && !weights;
    const bool bakedRigSource = meshNode && meshNode->skin && joints && weights
        && meshNode->skin->inverse_bind_matrices
        && meshNode->skin->joints_count > 0;
    const bool valid = buffersLoaded && data->scene && data->scene->name
        && std::string(data->scene->name) == expectedSceneName
        && meshNode && (staticPose || bakedRigSource)
        && primitive && primitive->type == cgltf_primitive_type_triangles
        && primitive->indices && primitive->material
        && primitive->material->has_pbr_metallic_roughness
        && primitive->material->pbr_metallic_roughness
            .base_color_texture.texture
        && position && normal && uv;
    cgltf_free(data);
    return valid;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path assets(argv[1]);
    if (!validate(assets / "DOOM_LEFT_HAND_FIST.glb", "L_Pose_1")) {
        std::cerr << "left fist GLB does not match the baked-pose contract\n";
        return 2;
    }
    if (!validate(assets / "DOOM_RIGHT_HAND_FIST.glb", "R_Pose_1")) {
        std::cerr << "right fist GLB does not match the baked-pose contract\n";
        return 3;
    }
    if (!validate(assets / "DOOM_LEFT_HAND_GUN.glb", "L_Pose_2")) {
        std::cerr << "left gun GLB does not match the baked-pose contract\n";
        return 4;
    }
    if (!validate(assets / "DOOM_RIGHT_HAND_GUN.glb", "R_Pose_2")) {
        std::cerr << "right gun GLB does not match the baked-pose contract\n";
        return 5;
    }
    return 0;
}
