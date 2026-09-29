#include "../QuadRuntime.h"
#include "Fsr1Upscaler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

namespace {

void fsrLog(const std::string& message) {argent::log("FSR1 "+message);}
void fsrStatus(const std::string& message) {argent::log("FSR1_STATUS "+message);}

VkFormat unormCopyFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_UNORM;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

std::uint32_t floatBits(float value) {
    std::uint32_t bits{};
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct ImageResource {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    bool initialized{};
};

struct alignas(16) FsrPushConstants {
    std::array<std::array<std::uint32_t, 4>, 4> con{};
    std::array<std::uint32_t, 2> outputSize{};
    std::array<std::uint32_t, 2> padding{};
};
static_assert(sizeof(FsrPushConstants) == 80);

} // namespace

struct Fsr1Upscaler::Impl {
    VkPhysicalDevice physical{};
    VkDevice device{};
    FsrDispatch vk{};
    VkFormat inputFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D sourceExtent{};
    VkExtent2D maximumOutputExtent{};
    bool destinationSrgb{};
    std::array<ImageResource, 2> input{};
    std::array<ImageResource, 2> easu{};
    std::array<ImageResource, 2> output{};
    std::array<std::uint64_t, 2> processedRevision{};
    std::array<VkImage,2> processedImage{};std::array<uint32_t,2> processedLayer{};
    std::array<Fsr1SourceRect, 2> processedSourceRect{};
    std::array<VkExtent2D, 2> processedOutputExtent{};
    VkSampler sampler{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkDescriptorPool descriptorPool{};
    std::array<VkDescriptorSet, 4> descriptorSets{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline easuPipeline{};
    VkPipeline rcasPipeline{};
    bool ready{};

    bool completeDispatch() const {
        return vk.createImage && vk.destroyImage && vk.getImageMemoryRequirements
            && vk.allocateMemory && vk.freeMemory && vk.bindImageMemory
            && vk.getPhysicalDeviceMemoryProperties && vk.createImageView
            && vk.destroyImageView && vk.createSampler && vk.destroySampler
            && vk.createShaderModule && vk.destroyShaderModule
            && vk.createDescriptorSetLayout && vk.destroyDescriptorSetLayout
            && vk.createDescriptorPool && vk.destroyDescriptorPool
            && vk.allocateDescriptorSets && vk.updateDescriptorSets
            && vk.createPipelineLayout && vk.destroyPipelineLayout
            && vk.createComputePipelines && vk.destroyPipeline
            && vk.cmdBindPipeline && vk.cmdBindDescriptorSets
            && vk.cmdPushConstants && vk.cmdDispatch
            && vk.cmdPipelineBarrier && vk.cmdCopyImage;
    }

    void transition(VkCommandBuffer commandBuffer, VkImage image,
                    VkImageLayout oldLayout, VkImageLayout newLayout,
                    VkAccessFlags sourceAccess, VkAccessFlags destinationAccess) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vk.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
            1, &barrier);
    }

    bool allocateImage(ImageResource& resource, VkFormat format, VkExtent2D extent,
                       VkImageUsageFlags usage) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vk.createImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements{};
        vk.getImageMemoryRequirements(device, resource.image, &requirements);
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vk.getPhysicalDeviceMemoryProperties(physical, &memoryProperties);
        std::uint32_t memoryType = UINT32_MAX;
        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            if ((requirements.memoryTypeBits & (1u << index))
                && (memoryProperties.memoryTypes[index].propertyFlags
                    & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryType = index;
                break;
            }
        }
        if (memoryType == UINT32_MAX) return false;

        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (vk.allocateMemory(device, &allocation, nullptr, &resource.memory) != VK_SUCCESS
            || vk.bindImageMemory(device, resource.image, resource.memory, 0) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = resource.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        return vk.createImageView(device, &viewInfo, nullptr, &resource.view) == VK_SUCCESS;
    }

    VkPipeline loadPipeline(const wchar_t* fileName) {
        std::ifstream file(argent::runtimePath()/fileName,
            std::ios::binary | std::ios::ate);
        if (!file) {
            fsrLog(std::string("shader missing: ")
                + (fileName[4] == L'E' ? "Fsr1Easu.spv" : "Fsr1Rcas.spv"));
            return VK_NULL_HANDLE;
        }
        const auto length = file.tellg();
        if (length <= 0 || (static_cast<std::size_t>(length) & 3u)) return VK_NULL_HANDLE;
        std::vector<std::uint32_t> code(static_cast<std::size_t>(length) / 4u);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(code.data()), length);

        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = static_cast<std::size_t>(length);
        moduleInfo.pCode = code.data();
        VkShaderModule module{};
        if (vk.createShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stage;
        pipelineInfo.layout = pipelineLayout;
        VkPipeline pipeline{};
        const auto result = vk.createComputePipelines(device, VK_NULL_HANDLE, 1,
            &pipelineInfo, nullptr, &pipeline);
        vk.destroyShaderModule(device, module, nullptr);
        return result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE;
    }

    bool makePipelinesAndDescriptors() {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
        }};
        VkDescriptorSetLayoutCreateInfo setInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        setInfo.pBindings = bindings.data();
        if (vk.createDescriptorSetLayout(device, &setInfo, nullptr,
                &descriptorSetLayout) != VK_SUCCESS)
            return false;

        VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0,
            sizeof(FsrPushConstants)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        if (vk.createPipelineLayout(device, &layoutInfo, nullptr,
                &pipelineLayout) != VK_SUCCESS)
            return false;

        easuPipeline = loadPipeline(L"Fsr1Easu.spv");
        rcasPipeline = loadPipeline(L"Fsr1Rcas.spv");
        if (!easuPipeline || !rcasPipeline) return false;

        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vk.createSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
            return false;

        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4}
        }};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 4;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (vk.createDescriptorPool(device, &poolInfo, nullptr, &descriptorPool)
                != VK_SUCCESS)
            return false;
        std::array<VkDescriptorSetLayout, 4> layouts{{
            descriptorSetLayout, descriptorSetLayout,
            descriptorSetLayout, descriptorSetLayout
        }};
        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        allocateInfo.pSetLayouts = layouts.data();
        if (vk.allocateDescriptorSets(device, &allocateInfo, descriptorSets.data())
                != VK_SUCCESS)
            return false;

        for (int eye = 0; eye < 2; ++eye) {
            std::array<VkDescriptorImageInfo, 2> easuImages{{
                {sampler, input[eye].view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, easu[eye].view, VK_IMAGE_LAYOUT_GENERAL}
            }};
            std::array<VkDescriptorImageInfo, 2> rcasImages{{
                {sampler, easu[eye].view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, output[eye].view, VK_IMAGE_LAYOUT_GENERAL}
            }};
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (std::uint32_t pass = 0; pass < 2; ++pass) {
                auto& images = pass == 0 ? easuImages : rcasImages;
                const auto set = descriptorSets[eye * 2 + pass];
                for (std::uint32_t binding = 0; binding < 2; ++binding) {
                    auto& write = writes[pass * 2 + binding];
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = set;
                    write.dstBinding = binding;
                    write.descriptorCount = 1;
                    write.descriptorType = binding == 0
                        ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                        : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    write.pImageInfo = &images[binding];
                }
            }
            vk.updateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
                writes.data(), 0, nullptr);
        }
        return true;
    }

    void destroyAll() {
        ready = false;
        if (!device) return;
        if (easuPipeline && vk.destroyPipeline)
            vk.destroyPipeline(device, easuPipeline, nullptr);
        if (rcasPipeline && vk.destroyPipeline)
            vk.destroyPipeline(device, rcasPipeline, nullptr);
        if (pipelineLayout && vk.destroyPipelineLayout)
            vk.destroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorPool && vk.destroyDescriptorPool)
            vk.destroyDescriptorPool(device, descriptorPool, nullptr);
        if (descriptorSetLayout && vk.destroyDescriptorSetLayout)
            vk.destroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        if (sampler && vk.destroySampler) vk.destroySampler(device, sampler, nullptr);
        auto destroyImage = [&](ImageResource& image) {
            if (image.view && vk.destroyImageView)
                vk.destroyImageView(device, image.view, nullptr);
            if (image.image && vk.destroyImage)
                vk.destroyImage(device, image.image, nullptr);
            if (image.memory && vk.freeMemory)
                vk.freeMemory(device, image.memory, nullptr);
            image = {};
        };
        for (auto& image : input) destroyImage(image);
        for (auto& image : easu) destroyImage(image);
        for (auto& image : output) destroyImage(image);
        *this = Impl{};
    }
};

Fsr1Upscaler::Fsr1Upscaler() : impl_(new Impl) {}

Fsr1Upscaler::~Fsr1Upscaler() {
    // Vulkan can destroy the device before DLL detach. Failed initialization
    // is cleaned while the device is valid; process shutdown reclaims the
    // successful runtime resources with the device.
    delete impl_;
}

bool Fsr1Upscaler::initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                              const FsrDispatch& dispatch,
                              VkFormat sourceFormat, VkExtent2D sourceExtent,
                              VkExtent2D maximumOutputExtent) {
    auto& state = *impl_;
    if (state.ready) return state.sourceExtent.width == sourceExtent.width
        && state.sourceExtent.height == sourceExtent.height
        && state.maximumOutputExtent.width == maximumOutputExtent.width
        && state.maximumOutputExtent.height == maximumOutputExtent.height;
    if (state.device || !sourceExtent.width || !sourceExtent.height
        || !maximumOutputExtent.width || !maximumOutputExtent.height)
        return false;

    state.physical = physicalDevice;
    state.device = device;
    state.vk = dispatch;
    state.sourceExtent = sourceExtent;
    state.maximumOutputExtent = maximumOutputExtent;
    state.destinationSrgb = sourceFormat == VK_FORMAT_B8G8R8A8_SRGB
        || sourceFormat == VK_FORMAT_R8G8B8A8_SRGB;
    state.inputFormat = unormCopyFormat(sourceFormat);
    if (state.inputFormat == VK_FORMAT_UNDEFINED || !state.completeDispatch()) {
        fsrLog("unsupported source format or incomplete Vulkan compute dispatch; linear stereo retained");
        fsrStatus("Stereo linear fallback (FSR1 unavailable)");
        state.destroyAll();
        return false;
    }

    constexpr VkImageUsageFlags sampledInputUsage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkImageUsageFlags computeOutputUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    for (int eye = 0; eye < 2; ++eye) {
        if (!state.allocateImage(state.input[eye], state.inputFormat,
                sourceExtent, sampledInputUsage)
            || !state.allocateImage(state.easu[eye], VK_FORMAT_R8G8B8A8_UNORM,
                maximumOutputExtent, computeOutputUsage)
            || !state.allocateImage(state.output[eye], VK_FORMAT_R8G8B8A8_UNORM,
                maximumOutputExtent, computeOutputUsage)) {
            fsrLog("image allocation failed; linear stereo retained");
            fsrStatus("Stereo linear fallback (FSR1 image allocation failed)");
            state.destroyAll();
            return false;
        }
    }
    if (!state.makePipelinesAndDescriptors()) {
        fsrLog("pipeline or descriptor creation failed; linear stereo retained");
        fsrStatus("Stereo linear fallback (FSR1 pipeline unavailable)");
        state.destroyAll();
        return false;
    }
    state.ready = true;
    fsrStatus("FSR1 ACTIVE");
    fsrLog("FSR1 ACTIVE source=" + std::to_string(sourceExtent.width) + "x"
        + std::to_string(sourceExtent.height) + " maximumOutput="
        + std::to_string(maximumOutputExtent.width) + "x"
        + std::to_string(maximumOutputExtent.height)
        + " passes=EASU+RCAS sharpnessStops=0.2 renderer=shared-stereo");
    return true;
}

bool Fsr1Upscaler::active() const {
    return impl_ && impl_->ready;
}

void Fsr1Upscaler::releaseAfterCompletion() {
    impl_->destroyAll();
    *impl_ = Impl{};
}

void Fsr1Upscaler::discardRecordedFrame() {
    for (auto& image : impl_->input) image.initialized = false;
    for (auto& image : impl_->easu) image.initialized = false;
    for (auto& image : impl_->output) image.initialized = false;
    impl_->processedRevision = {};
}

VkImage Fsr1Upscaler::record(VkCommandBuffer commandBuffer, VkImage source,
                             int eye, std::uint64_t sourceRevision,
                             Fsr1SourceRect sourceRect,
                             VkExtent2D outputExtent, uint32_t sourceLayer) {
    auto& state = *impl_;
    if (!state.ready || !source || eye < 0 || eye > 1
        || !sourceRect.width || !sourceRect.height || !outputExtent.width
        || !outputExtent.height
        || sourceRect.x < 0 || sourceRect.y < 0
        || static_cast<std::uint64_t>(sourceRect.x) + sourceRect.width
            > state.sourceExtent.width
        || static_cast<std::uint64_t>(sourceRect.y) + sourceRect.height
            > state.sourceExtent.height
        || outputExtent.width > state.maximumOutputExtent.width
        || outputExtent.height > state.maximumOutputExtent.height)
        return VK_NULL_HANDLE;

    const auto& oldRect = state.processedSourceRect[eye];
    const auto& oldOutput = state.processedOutputExtent[eye];
    if (sourceRevision && state.output[eye].initialized
        && state.processedRevision[eye] == sourceRevision
        && state.processedImage[eye]==source && state.processedLayer[eye]==sourceLayer
        && oldRect.x == sourceRect.x && oldRect.y == sourceRect.y
        && oldRect.width == sourceRect.width && oldRect.height == sourceRect.height
        && oldOutput.width == outputExtent.width
        && oldOutput.height == outputExtent.height)
        return state.output[eye].image;

    auto& input = state.input[eye];
    auto& easu = state.easu[eye];
    auto& output = state.output[eye];
    state.transition(commandBuffer, input.image,
        input.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        input.initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy inputCopy{};
    inputCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    inputCopy.srcSubresource.layerCount = 1;
    inputCopy.srcSubresource.baseArrayLayer = sourceLayer;
    inputCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    inputCopy.dstSubresource.layerCount = 1;
    inputCopy.extent = {state.sourceExtent.width, state.sourceExtent.height, 1};
    state.vk.cmdCopyImage(commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &inputCopy);
    state.transition(commandBuffer, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    input.initialized = true;

    state.transition(commandBuffer, easu.image,
        easu.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        easu.initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_SHADER_WRITE_BIT);

    FsrPushConstants easuConstants{};
    const float sourceWidth = static_cast<float>(state.sourceExtent.width);
    const float sourceHeight = static_cast<float>(state.sourceExtent.height);
    const float viewportWidth = static_cast<float>(sourceRect.width);
    const float viewportHeight = static_cast<float>(sourceRect.height);
    const float targetWidth = static_cast<float>(outputExtent.width);
    const float targetHeight = static_cast<float>(outputExtent.height);
    easuConstants.con[0] = {
        floatBits(viewportWidth / targetWidth),
        floatBits(viewportHeight / targetHeight),
        floatBits(0.5f * viewportWidth / targetWidth - 0.5f
            + static_cast<float>(sourceRect.x)),
        floatBits(0.5f * viewportHeight / targetHeight - 0.5f
            + static_cast<float>(sourceRect.y))
    };
    easuConstants.con[1] = {
        floatBits(1.0f / sourceWidth), floatBits(1.0f / sourceHeight),
        floatBits(1.0f / sourceWidth), floatBits(-1.0f / sourceHeight)
    };
    easuConstants.con[2] = {
        floatBits(-1.0f / sourceWidth), floatBits(2.0f / sourceHeight),
        floatBits(1.0f / sourceWidth), floatBits(2.0f / sourceHeight)
    };
    easuConstants.con[3] = {
        floatBits(0.0f), floatBits(4.0f / sourceHeight), 0, 0
    };
    easuConstants.outputSize = {outputExtent.width, outputExtent.height};

    const auto easuSet = state.descriptorSets[eye * 2];
    state.vk.cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        state.easuPipeline);
    state.vk.cmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        state.pipelineLayout, 0, 1, &easuSet, 0, nullptr);
    state.vk.cmdPushConstants(commandBuffer, state.pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(easuConstants), &easuConstants);
    state.vk.cmdDispatch(commandBuffer, (outputExtent.width + 15u) / 16u,
        (outputExtent.height + 15u) / 16u, 1);
    state.transition(commandBuffer, easu.image, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    easu.initialized = true;

    state.transition(commandBuffer, output.image,
        output.initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                           : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        output.initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
        VK_ACCESS_SHADER_WRITE_BIT);
    FsrPushConstants rcasConstants{};
    constexpr float rcasSharpnessStops = 0.2f;
    rcasConstants.con[0][0] = floatBits(std::exp2(-rcasSharpnessStops));
    rcasConstants.outputSize = {outputExtent.width, outputExtent.height};
    rcasConstants.padding[0] = state.destinationSrgb ? 1u : 0u;
    const auto rcasSet = state.descriptorSets[eye * 2 + 1];
    state.vk.cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        state.rcasPipeline);
    state.vk.cmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        state.pipelineLayout, 0, 1, &rcasSet, 0, nullptr);
    state.vk.cmdPushConstants(commandBuffer, state.pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rcasConstants), &rcasConstants);
    state.vk.cmdDispatch(commandBuffer, (outputExtent.width + 15u) / 16u,
        (outputExtent.height + 15u) / 16u, 1);
    state.transition(commandBuffer, output.image, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    output.initialized = true;
    state.processedRevision[eye] = sourceRevision;
    state.processedImage[eye]=source;state.processedLayer[eye]=sourceLayer;
    state.processedSourceRect[eye] = sourceRect;
    state.processedOutputExtent[eye] = outputExtent;
    return output.image;
}
