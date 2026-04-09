#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "exynostools_embedded_spirv.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define EXYNOS_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ExynosToolsLayer", __VA_ARGS__)
#define EXYNOS_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "ExynosToolsLayer", __VA_ARGS__)
#else
#include <cstdio>
#define EXYNOS_LOGI(...) std::fprintf(stderr, "[ExynosToolsLayer][I] " __VA_ARGS__), std::fprintf(stderr, "\n")
#define EXYNOS_LOGW(...) std::fprintf(stderr, "[ExynosToolsLayer][W] " __VA_ARGS__), std::fprintf(stderr, "\n")
#endif

namespace {

template <typename T>
void* dispatch_key(T handle) {
    return *reinterpret_cast<void**>(handle);
}

struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkCreateDevice create_device = nullptr;
    PFN_vkEnumeratePhysicalDevices enumerate_physical_devices = nullptr;
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties get_physical_device_format_properties = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties get_physical_device_image_format_properties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties2 get_physical_device_format_properties2 = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 get_physical_device_image_format_properties2 = nullptr;
#ifdef VK_KHR_get_physical_device_properties2
    PFN_vkGetPhysicalDeviceFormatProperties2KHR get_physical_device_format_properties2_khr = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2KHR get_physical_device_image_format_properties2_khr = nullptr;
#endif
};

struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkCreateImage create_image = nullptr;
    PFN_vkDestroyImage destroy_image = nullptr;
    PFN_vkCreateImageView create_image_view = nullptr;
    PFN_vkDestroyImageView destroy_image_view = nullptr;
    PFN_vkCreateCommandPool create_command_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
    PFN_vkResetCommandPool reset_command_pool = nullptr;
    PFN_vkAllocateCommandBuffers allocate_command_buffers = nullptr;
    PFN_vkFreeCommandBuffers free_command_buffers = nullptr;
    PFN_vkCreateShaderModule create_shader_module = nullptr;
    PFN_vkDestroyShaderModule destroy_shader_module = nullptr;
    PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout = nullptr;
    PFN_vkCreatePipelineLayout create_pipeline_layout = nullptr;
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout = nullptr;
    PFN_vkCreateComputePipelines create_compute_pipelines = nullptr;
    PFN_vkDestroyPipeline destroy_pipeline = nullptr;
    PFN_vkCreateDescriptorPool create_descriptor_pool = nullptr;
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool = nullptr;
    PFN_vkAllocateDescriptorSets allocate_descriptor_sets = nullptr;
    PFN_vkFreeDescriptorSets free_descriptor_sets = nullptr;
    PFN_vkUpdateDescriptorSets update_descriptor_sets = nullptr;
    PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmd_bind_descriptor_sets = nullptr;
    PFN_vkCmdPushConstants cmd_push_constants = nullptr;
    PFN_vkCmdDispatch cmd_dispatch = nullptr;
    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier = nullptr;
    PFN_vkCmdCopyBuffer cmd_copy_buffer = nullptr;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image = nullptr;
    PFN_vkCmdCopyBufferToImage2 cmd_copy_buffer_to_image2 = nullptr;
#ifdef VK_KHR_copy_commands2
    PFN_vkCmdCopyBufferToImage2KHR cmd_copy_buffer_to_image2_khr = nullptr;
#endif
};

struct DeviceRuntime {
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
    bool geometry_shader = false;
    bool tessellation_shader = false;
    bool transform_feedback = false;
};

struct PhysicalRuntime {
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
};

struct VirtualImageInfo {
    VkFormat requested_format = VK_FORMAT_UNDEFINED;
    VkFormat real_format = VK_FORMAT_UNDEFINED;
};

enum class DecoderShaderKind : uint32_t {
    None = 0,
    S3tc,
    Rgtc,
    Bc6,
    Bc7
};

struct StorageViewKey {
    void* image = nullptr;
    uint32_t mip_level = 0;
    uint32_t layer = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    bool operator==(const StorageViewKey& other) const {
        return image == other.image &&
               mip_level == other.mip_level &&
               layer == other.layer &&
               format == other.format;
    }
};

struct StorageViewKeyHash {
    size_t operator()(const StorageViewKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.image);
        h ^= static_cast<size_t>(key.mip_level + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.layer + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.format) + 0x9e3779b9u + (h << 6) + (h >> 2));
        return h;
    }
};

struct ComputeRuntime {
    std::mutex init_mutex;
    bool initialized = false;
    bool available = false;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipeline pipeline_s3tc = VK_NULL_HANDLE;
    VkPipeline pipeline_rgtc = VK_NULL_HANDLE;
    VkPipeline pipeline_bc6 = VK_NULL_HANDLE;
    VkPipeline pipeline_bc7 = VK_NULL_HANDLE;
};

struct StagingAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct VmaRuntime {
    std::mutex init_mutex;
    bool initialized = false;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaVulkanFunctions vulkan_functions{};
};

std::shared_mutex g_lock;
std::unordered_map<void*, InstanceDispatch> g_instance_dispatch;
std::unordered_map<void*, DeviceDispatch> g_device_dispatch;
std::unordered_map<void*, void*> g_physical_to_instance;
std::unordered_map<void*, VkInstance> g_physical_to_instance_handle;
std::unordered_map<void*, PhysicalRuntime> g_physical_runtime;
std::unordered_map<void*, DeviceRuntime> g_device_runtime;
std::unordered_map<void*, void*> g_command_buffer_to_device;
std::unordered_map<void*, VkDevice> g_command_buffer_device_handle;
std::unordered_map<void*, void*> g_command_buffer_to_pool;
std::unordered_map<void*, void*> g_command_pool_to_device;
std::unordered_map<void*, VkInstance> g_device_to_instance_handle;
std::unordered_map<void*, VkPhysicalDevice> g_device_to_physical_handle;
std::unordered_map<void*, VirtualImageInfo> g_virtual_images;
std::unordered_map<void*, void*> g_image_to_device;
std::unordered_map<void*, std::shared_ptr<ComputeRuntime>> g_compute_runtime;
std::unordered_map<void*, std::shared_ptr<VmaRuntime>> g_vma_runtime;
std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash> g_storage_views;
std::unordered_map<void*, std::vector<StagingAllocation>> g_command_buffer_staging_allocations;
std::unordered_map<void*, std::vector<VkDescriptorSet>> g_command_buffer_descriptor_sets;
std::mutex g_tracking_lock;
std::atomic<bool> g_warned_missing_cmd_buffer_map{false};

const char* kLayerName = "VK_LAYER_EXYNOSTOOLS_bcn";
const uint32_t kLayerImplVersion = 300u;
constexpr uint32_t kDecodePushConstantsSize = sizeof(int32_t) * 8u;

bool is_bcn_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

VkFormat bcn_replacement_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

bool is_xclipse_device(void* device_key) {
    auto it = g_device_runtime.find(device_key);
    return it != g_device_runtime.end() && it->second.is_xclipse;
}

bool is_xclipse_physical(void* physical_key) {
    auto it = g_physical_runtime.find(physical_key);
    return it != g_physical_runtime.end() && it->second.is_xclipse;
}

bool get_instance_dispatch_for_physical(
    VkPhysicalDevice physicalDevice,
    InstanceDispatch* out_dispatch,
    VkInstance* out_instance) {
    if (!out_dispatch) {
        return false;
    }

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto phys_key = dispatch_key(physicalDevice);
    auto it_map = g_physical_to_instance.find(phys_key);
    if (it_map == g_physical_to_instance.end()) {
        return false;
    }
    auto it_inst = g_instance_dispatch.find(it_map->second);
    if (it_inst == g_instance_dispatch.end()) {
        return false;
    }
    *out_dispatch = it_inst->second;
    if (out_instance) {
        auto it_inst_handle = g_physical_to_instance_handle.find(phys_key);
        *out_instance = (it_inst_handle != g_physical_to_instance_handle.end()) ? it_inst_handle->second : VK_NULL_HANDLE;
    }
    return true;
}

void virtualize_format_properties_if_needed(
    VkPhysicalDevice physicalDevice,
    VkFormat requested_format,
    VkFormatProperties* io_props) {
    if (!io_props || !is_bcn_format(requested_format)) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_format_properties) {
        return;
    }

    bool xclipse_physical = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        xclipse_physical = is_xclipse_physical(dispatch_key(physicalDevice));
    }
    if (!xclipse_physical) {
        return;
    }

    VkFormat replacement = bcn_replacement_format(requested_format);
    if (replacement == VK_FORMAT_UNDEFINED) {
        return;
    }

    VkFormatProperties replacement_props{};
    dispatch.get_physical_device_format_properties(physicalDevice, replacement, &replacement_props);
    io_props->linearTilingFeatures = replacement_props.linearTilingFeatures;
    io_props->optimalTilingFeatures = replacement_props.optimalTilingFeatures;
    io_props->bufferFeatures = replacement_props.bufferFeatures;
}

DecoderShaderKind shader_kind_for_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            return DecoderShaderKind::S3tc;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
            return DecoderShaderKind::Rgtc;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return DecoderShaderKind::Bc6;
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return DecoderShaderKind::Bc7;
        default:
            return DecoderShaderKind::None;
    }
}

const char* shader_file_for_kind(DecoderShaderKind kind) {
    switch (kind) {
        case DecoderShaderKind::S3tc:
            return "s3tc_iv.comp.spv";
        case DecoderShaderKind::Rgtc:
            return "rgtc_iv.comp.spv";
        case DecoderShaderKind::Bc6:
            return "bc6_iv.comp.spv";
        case DecoderShaderKind::Bc7:
            return "bc7_iv.comp.spv";
        default:
            return nullptr;
    }
}

VkPipeline* pipeline_slot_for_kind(ComputeRuntime* runtime, DecoderShaderKind kind) {
    if (!runtime) {
        return nullptr;
    }
    switch (kind) {
        case DecoderShaderKind::S3tc:
            return &runtime->pipeline_s3tc;
        case DecoderShaderKind::Rgtc:
            return &runtime->pipeline_rgtc;
        case DecoderShaderKind::Bc6:
            return &runtime->pipeline_bc6;
        case DecoderShaderKind::Bc7:
            return &runtime->pipeline_bc7;
        default:
            return nullptr;
    }
}

bool read_spirv_file(const std::string& filename, std::vector<uint32_t>* out_words) {
    if (!out_words || filename.empty()) {
        return false;
    }
    const exynostools_embedded_spirv::ShaderBlob* blob =
        exynostools_embedded_spirv::find_shader_blob(filename.c_str());
    if (!blob || !blob->words || blob->word_count == 0) {
        return false;
    }
    out_words->assign(blob->words, blob->words + blob->word_count);
    return true;
}

bool create_compute_pipeline_for_kind(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    DecoderShaderKind kind) {
    if (!runtime) {
        return false;
    }

    VkPipeline* pipeline_slot = pipeline_slot_for_kind(runtime, kind);
    const char* shader_name = shader_file_for_kind(kind);
    if (!pipeline_slot || !shader_name) {
        return false;
    }
    if (*pipeline_slot != VK_NULL_HANDLE) {
        return true;
    }

    std::vector<uint32_t> spirv_words;
    if (!read_spirv_file(shader_name, &spirv_words)) {
        EXYNOS_LOGW("Compute shader SPIR-V not found: %s", shader_name);
        return false;
    }

    VkShaderModuleCreateInfo shader_ci{};
    shader_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_ci.codeSize = spirv_words.size() * sizeof(uint32_t);
    shader_ci.pCode = spirv_words.data();

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (dispatch.create_shader_module(device, &shader_ci, nullptr, &shader_module) != VK_SUCCESS ||
        shader_module == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create shader module for %s", shader_name);
        return false;
    }

    VkPipelineShaderStageCreateInfo stage_ci{};
    stage_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_ci.module = shader_module;
    stage_ci.pName = "main";

    VkComputePipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_ci.stage = stage_ci;
    pipeline_ci.layout = runtime->pipeline_layout;

    VkResult pipeline_result = dispatch.create_compute_pipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, pipeline_slot);

    dispatch.destroy_shader_module(device, shader_module, nullptr);
    if (pipeline_result != VK_SUCCESS || *pipeline_slot == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create compute pipeline for %s", shader_name);
        *pipeline_slot = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool initialize_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime) {
    if (!runtime) {
        return false;
    }
    if (runtime->initialized) {
        return runtime->available;
    }

    runtime->initialized = true;
    runtime->available = false;

    if (!dispatch.create_descriptor_set_layout ||
        !dispatch.destroy_descriptor_set_layout ||
        !dispatch.create_pipeline_layout ||
        !dispatch.destroy_pipeline_layout ||
        !dispatch.create_compute_pipelines ||
        !dispatch.destroy_pipeline ||
        !dispatch.create_descriptor_pool ||
        !dispatch.destroy_descriptor_pool ||
        !dispatch.allocate_descriptor_sets ||
        !dispatch.update_descriptor_sets ||
        !dispatch.create_shader_module ||
        !dispatch.destroy_shader_module ||
        !dispatch.cmd_bind_pipeline ||
        !dispatch.cmd_bind_descriptor_sets ||
        !dispatch.cmd_push_constants ||
        !dispatch.cmd_dispatch ||
        !dispatch.cmd_pipeline_barrier) {
        EXYNOS_LOGW("Compute runtime unavailable: missing required Vulkan entry points.");
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci{};
    dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 2;
    dsl_ci.pBindings = bindings;
    if (dispatch.create_descriptor_set_layout(device, &dsl_ci, nullptr, &runtime->descriptor_set_layout) != VK_SUCCESS ||
        runtime->descriptor_set_layout == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create compute descriptor set layout.");
        return false;
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push_range.offset = 0;
    push_range.size = kDecodePushConstantsSize;

    VkPipelineLayoutCreateInfo pl_ci{};
    pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &runtime->descriptor_set_layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &push_range;
    if (dispatch.create_pipeline_layout(device, &pl_ci, nullptr, &runtime->pipeline_layout) != VK_SUCCESS ||
        runtime->pipeline_layout == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create compute pipeline layout.");
        return false;
    }

    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[0].descriptorCount = 4096;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = 4096;

    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = 4096;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes = pool_sizes;
    if (dispatch.create_descriptor_pool(device, &pool_ci, nullptr, &runtime->descriptor_pool) != VK_SUCCESS ||
        runtime->descriptor_pool == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create compute descriptor pool.");
        return false;
    }

    bool ok_s3tc = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::S3tc);
    bool ok_rgtc = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Rgtc);
    bool ok_bc6 = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Bc6);
    bool ok_bc7 = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Bc7);
    runtime->available = ok_s3tc || ok_rgtc || ok_bc6 || ok_bc7;
    if (!runtime->available) {
        EXYNOS_LOGW("No compute decoder pipelines are available.");
    }
    return runtime->available;
}

void destroy_compute_runtime(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime) {
    if (!runtime) {
        return;
    }
    if (runtime->pipeline_s3tc != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
        dispatch.destroy_pipeline(device, runtime->pipeline_s3tc, nullptr);
        runtime->pipeline_s3tc = VK_NULL_HANDLE;
    }
    if (runtime->pipeline_rgtc != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
        dispatch.destroy_pipeline(device, runtime->pipeline_rgtc, nullptr);
        runtime->pipeline_rgtc = VK_NULL_HANDLE;
    }
    if (runtime->pipeline_bc6 != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
        dispatch.destroy_pipeline(device, runtime->pipeline_bc6, nullptr);
        runtime->pipeline_bc6 = VK_NULL_HANDLE;
    }
    if (runtime->pipeline_bc7 != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
        dispatch.destroy_pipeline(device, runtime->pipeline_bc7, nullptr);
        runtime->pipeline_bc7 = VK_NULL_HANDLE;
    }
    if (runtime->descriptor_pool != VK_NULL_HANDLE && dispatch.destroy_descriptor_pool) {
        dispatch.destroy_descriptor_pool(device, runtime->descriptor_pool, nullptr);
        runtime->descriptor_pool = VK_NULL_HANDLE;
    }
    if (runtime->pipeline_layout != VK_NULL_HANDLE && dispatch.destroy_pipeline_layout) {
        dispatch.destroy_pipeline_layout(device, runtime->pipeline_layout, nullptr);
        runtime->pipeline_layout = VK_NULL_HANDLE;
    }
    if (runtime->descriptor_set_layout != VK_NULL_HANDLE && dispatch.destroy_descriptor_set_layout) {
        dispatch.destroy_descriptor_set_layout(device, runtime->descriptor_set_layout, nullptr);
        runtime->descriptor_set_layout = VK_NULL_HANDLE;
    }
    runtime->initialized = false;
    runtime->available = false;
}

struct DecodePushConstants {
    int32_t format;
    int32_t width;
    int32_t height;
    int32_t offset;
    int32_t bufferRowLength;
    int32_t offsetX;
    int32_t offsetY;
    int32_t reserved0;
};
static_assert(sizeof(DecodePushConstants) == kDecodePushConstantsSize, "DecodePushConstants layout mismatch.");

uint32_t block_size_bytes(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8u;
        default:
            return 16u;
    }
}

bool get_or_create_storage_view(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkImage image,
    uint32_t mip_level,
    uint32_t layer,
    VkFormat format,
    VkImageView* out_view) {
    if (!out_view) {
        return false;
    }

    StorageViewKey key{};
    key.image = dispatch_key(image);
    key.mip_level = mip_level;
    key.layer = layer;
    key.format = format;

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_storage_views.find(key);
        if (it != g_storage_views.end()) {
            *out_view = it->second;
            return true;
        }
    }

    if (!dispatch.create_image_view) {
        return false;
    }

    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.baseMipLevel = mip_level;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.baseArrayLayer = layer;
    view_ci.subresourceRange.layerCount = 1;

    VkImageView created_view = VK_NULL_HANDLE;
    if (dispatch.create_image_view(device, &view_ci, nullptr, &created_view) != VK_SUCCESS ||
        created_view == VK_NULL_HANDLE) {
        return false;
    }

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto insert_result = g_storage_views.emplace(key, created_view);
        if (!insert_result.second) {
            if (dispatch.destroy_image_view) {
                dispatch.destroy_image_view(device, created_view, nullptr);
            }
            created_view = insert_result.first->second;
        }
    }

    *out_view = created_view;
    return true;
}

VkPipeline choose_decoder_pipeline(
    const ComputeRuntime& runtime,
    VkFormat requested_format) {
    switch (shader_kind_for_format(requested_format)) {
        case DecoderShaderKind::S3tc:
            return runtime.pipeline_s3tc;
        case DecoderShaderKind::Rgtc:
            return runtime.pipeline_rgtc;
        case DecoderShaderKind::Bc6:
            return runtime.pipeline_bc6;
        case DecoderShaderKind::Bc7:
            return runtime.pipeline_bc7;
        default:
            return VK_NULL_HANDLE;
    }
}

std::shared_ptr<ComputeRuntime> get_or_create_compute_runtime(void* device_key) {
    {
        std::shared_lock<std::shared_mutex> read_guard(g_lock);
        auto it = g_compute_runtime.find(device_key);
        if (it != g_compute_runtime.end()) {
            return it->second;
        }
    }

    std::lock_guard<std::shared_mutex> write_guard(g_lock);
    auto it = g_compute_runtime.find(device_key);
    if (it != g_compute_runtime.end()) {
        return it->second;
    }
    auto runtime = std::make_shared<ComputeRuntime>();
    g_compute_runtime[device_key] = runtime;
    return runtime;
}

std::shared_ptr<VmaRuntime> get_or_create_vma_runtime(void* device_key) {
    {
        std::shared_lock<std::shared_mutex> read_guard(g_lock);
        auto it = g_vma_runtime.find(device_key);
        if (it != g_vma_runtime.end()) {
            return it->second;
        }
    }

    std::lock_guard<std::shared_mutex> write_guard(g_lock);
    auto it = g_vma_runtime.find(device_key);
    if (it != g_vma_runtime.end()) {
        return it->second;
    }
    auto runtime = std::make_shared<VmaRuntime>();
    g_vma_runtime[device_key] = runtime;
    return runtime;
}

bool initialize_vma_runtime(
    void* device_key,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VmaRuntime* runtime) {
    if (!runtime) {
        return false;
    }
    if (runtime->initialized) {
        return runtime->allocator != VK_NULL_HANDLE;
    }
    runtime->initialized = true;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    InstanceDispatch instance_dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_instance_handle = g_device_to_instance_handle.find(device_key);
        auto it_physical_handle = g_device_to_physical_handle.find(device_key);
        if (it_instance_handle == g_device_to_instance_handle.end() ||
            it_physical_handle == g_device_to_physical_handle.end()) {
            EXYNOS_LOGW("VMA init failed: device missing instance/physical mapping.");
            return false;
        }
        instance = it_instance_handle->second;
        physical_device = it_physical_handle->second;

        auto it_instance_dispatch = g_instance_dispatch.find(dispatch_key(instance));
        if (it_instance_dispatch == g_instance_dispatch.end()) {
            EXYNOS_LOGW("VMA init failed: instance dispatch missing.");
            return false;
        }
        instance_dispatch = it_instance_dispatch->second;
    }

    if (!dispatch.get_device_proc_addr || !instance_dispatch.get_instance_proc_addr) {
        EXYNOS_LOGW("VMA init failed: missing get-proc-address entry points.");
        return false;
    }

    runtime->vulkan_functions = {};
    runtime->vulkan_functions.vkGetInstanceProcAddr = instance_dispatch.get_instance_proc_addr;
    runtime->vulkan_functions.vkGetDeviceProcAddr = dispatch.get_device_proc_addr;

    VmaAllocatorCreateInfo allocator_ci{};
    allocator_ci.physicalDevice = physical_device;
    allocator_ci.device = device;
    allocator_ci.instance = instance;
    allocator_ci.pVulkanFunctions = &runtime->vulkan_functions;
    allocator_ci.vulkanApiVersion = VK_API_VERSION_1_0;

    VkResult vma_result = vmaCreateAllocator(&allocator_ci, &runtime->allocator);
    if (vma_result != VK_SUCCESS || runtime->allocator == VK_NULL_HANDLE) {
        runtime->allocator = VK_NULL_HANDLE;
        EXYNOS_LOGW("VMA init failed (VkResult=%d).", static_cast<int>(vma_result));
        return false;
    }

    EXYNOS_LOGI("VMA allocator initialized for BCn staging.");
    return true;
}

void release_staging_allocations(
    VmaAllocator allocator,
    std::vector<StagingAllocation>* allocations) {
    if (!allocator || !allocations) {
        return;
    }
    for (const StagingAllocation& staging : *allocations) {
        if (staging.buffer != VK_NULL_HANDLE && staging.allocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
        }
    }
    allocations->clear();
}

void take_command_buffer_staging_allocations_locked(
    void* command_buffer_key,
    std::vector<StagingAllocation>* out_allocations) {
    if (!out_allocations) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    auto it = g_command_buffer_staging_allocations.find(command_buffer_key);
    if (it == g_command_buffer_staging_allocations.end()) {
        return;
    }
    auto& src = it->second;
    out_allocations->insert(
        out_allocations->end(),
        std::make_move_iterator(src.begin()),
        std::make_move_iterator(src.end()));
    g_command_buffer_staging_allocations.erase(it);
}

void track_staging_allocation(
    VkCommandBuffer command_buffer,
    StagingAllocation&& staging) {
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    g_command_buffer_staging_allocations[dispatch_key(command_buffer)].push_back(std::move(staging));
}

void take_command_buffer_descriptor_sets_locked(
    void* command_buffer_key,
    std::vector<VkDescriptorSet>* out_sets) {
    if (!out_sets) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    auto it = g_command_buffer_descriptor_sets.find(command_buffer_key);
    if (it == g_command_buffer_descriptor_sets.end()) {
        return;
    }
    auto& src = it->second;
    out_sets->insert(out_sets->end(), src.begin(), src.end());
    g_command_buffer_descriptor_sets.erase(it);
}

void track_descriptor_set(
    VkCommandBuffer command_buffer,
    VkDescriptorSet descriptor_set) {
    if (descriptor_set == VK_NULL_HANDLE) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    g_command_buffer_descriptor_sets[dispatch_key(command_buffer)].push_back(descriptor_set);
}

void release_descriptor_sets(
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkDescriptorPool descriptor_pool,
    std::vector<VkDescriptorSet>* descriptor_sets) {
    if (!descriptor_sets || descriptor_sets->empty()) {
        return;
    }
    if (dispatch.free_descriptor_sets && descriptor_pool != VK_NULL_HANDLE) {
        dispatch.free_descriptor_sets(
            device,
            descriptor_pool,
            static_cast<uint32_t>(descriptor_sets->size()),
            descriptor_sets->data());
    }
    descriptor_sets->clear();
}

bool create_staging_copy_for_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VmaAllocator allocator,
    VkBuffer src_buffer,
    VkDeviceSize src_offset,
    VkDeviceSize byte_size,
    StagingAllocation* out_staging) {
    if (!out_staging || !allocator || !dispatch.cmd_copy_buffer || !dispatch.cmd_pipeline_barrier) {
        return false;
    }
    (void)device;
    if (byte_size == 0 || src_buffer == VK_NULL_HANDLE) {
        return false;
    }

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.size = byte_size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    StagingAllocation staging{};
    VkResult create_result = vmaCreateBuffer(
        allocator,
        &buffer_ci,
        &alloc_ci,
        &staging.buffer,
        &staging.allocation,
        nullptr);
    if (create_result != VK_SUCCESS ||
        staging.buffer == VK_NULL_HANDLE ||
        staging.allocation == VK_NULL_HANDLE) {
        return false;
    }

    VkBufferCopy copy_region{};
    copy_region.srcOffset = src_offset;
    copy_region.dstOffset = 0;
    copy_region.size = byte_size;
    dispatch.cmd_copy_buffer(command_buffer, src_buffer, staging.buffer, 1, &copy_region);

    VkBufferMemoryBarrier buffer_barrier{};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = staging.buffer;
    buffer_barrier.offset = 0;
    buffer_barrier.size = byte_size;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &buffer_barrier,
        0, nullptr);

    *out_staging = staging;
    return true;
}

bool record_decode_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaAllocator allocator,
    VkImage dst_image,
    VkImageLayout dst_layout,
    VkBuffer src_buffer,
    VkFormat requested_format,
    VkFormat real_format,
    const VkBufferImageCopy& region,
    uint32_t layer_index) {
    if (!runtime || !runtime->available) {
        return false;
    }

    VkPipeline pipeline = choose_decoder_pipeline(*runtime, requested_format);
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }

    uint32_t blocks_x = (std::max(region.bufferRowLength, region.imageExtent.width) + 3u) / 4u;
    uint32_t rows = region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height;
    uint32_t blocks_y = (rows + 3u) / 4u;
    VkDeviceSize layer_stride = static_cast<VkDeviceSize>(blocks_x) *
                                static_cast<VkDeviceSize>(blocks_y) *
                                static_cast<VkDeviceSize>(block_size_bytes(requested_format));
    VkDeviceSize buffer_offset = region.bufferOffset + static_cast<VkDeviceSize>(layer_index) * layer_stride;
    if (layer_stride == 0) {
        return false;
    }

    StagingAllocation staging{};
    if (!create_staging_copy_for_region(
            command_buffer,
            device,
            dispatch,
            allocator,
            src_buffer,
            buffer_offset,
            layer_stride,
            &staging)) {
        EXYNOS_LOGW("Failed to create BCn staging copy buffer.");
        return false;
    }
    VkBuffer staging_buffer = staging.buffer;
    track_staging_allocation(command_buffer, std::move(staging));

    VkImageView storage_view = VK_NULL_HANDLE;
    if (!get_or_create_storage_view(
            device,
            dispatch,
            dst_image,
            region.imageSubresource.mipLevel,
            region.imageSubresource.baseArrayLayer + layer_index,
            real_format,
            &storage_view)) {
        return false;
    }

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = runtime->descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &runtime->descriptor_set_layout;
    if (dispatch.allocate_descriptor_sets(device, &alloc_info, &descriptor_set) != VK_SUCCESS ||
        descriptor_set == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Descriptor set allocation failed for BCn decode.");
        return false;
    }
    track_descriptor_set(command_buffer, descriptor_set);

    VkDescriptorImageInfo image_info{};
    image_info.imageView = storage_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = staging_buffer;
    buffer_info.offset = 0;
    buffer_info.range = layer_stride;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &image_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &buffer_info;
    dispatch.update_descriptor_sets(device, 2, writes, 0, nullptr);

    VkImageMemoryBarrier to_general{};
    to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_general.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_general.oldLayout = dst_layout;
    to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.image = dst_image;
    to_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_general.subresourceRange.baseMipLevel = region.imageSubresource.mipLevel;
    to_general.subresourceRange.levelCount = 1;
    to_general.subresourceRange.baseArrayLayer = region.imageSubresource.baseArrayLayer + layer_index;
    to_general.subresourceRange.layerCount = 1;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &to_general);

    DecodePushConstants regs{};
    if (region.imageExtent.width > static_cast<uint32_t>(INT32_MAX) ||
        region.imageExtent.height > static_cast<uint32_t>(INT32_MAX) ||
        region.bufferRowLength > static_cast<uint32_t>(INT32_MAX) ||
        region.imageOffset.x < 0 ||
        region.imageOffset.y < 0) {
        EXYNOS_LOGW("Region exceeds push constant integer range.");
        return false;
    }
    regs.format = static_cast<int32_t>(requested_format);
    regs.width = static_cast<int32_t>(region.imageExtent.width);
    regs.height = static_cast<int32_t>(region.imageExtent.height);
    regs.offset = 0;
    regs.bufferRowLength = static_cast<int32_t>(region.bufferRowLength);
    regs.offsetX = region.imageOffset.x;
    regs.offsetY = region.imageOffset.y;
    regs.reserved0 = 0;

    dispatch.cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    dispatch.cmd_bind_descriptor_sets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        runtime->pipeline_layout,
        0,
        1,
        &descriptor_set,
        0,
        nullptr);
    dispatch.cmd_push_constants(
        command_buffer,
        runtime->pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(DecodePushConstants),
        &regs);

    uint32_t groups_x = (region.imageExtent.width + 7u) / 8u;
    uint32_t groups_y = (region.imageExtent.height + 7u) / 8u;
    dispatch.cmd_dispatch(command_buffer, groups_x, groups_y, 1);

    VkImageMemoryBarrier from_general{};
    from_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    from_general.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    from_general.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    from_general.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    from_general.newLayout = dst_layout;
    from_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.image = dst_image;
    from_general.subresourceRange = to_general.subresourceRange;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &from_general);

    return true;
}

bool try_decode_copy_regions(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkBuffer src_buffer,
    VkImage dst_image,
    VkImageLayout dst_layout,
    uint32_t region_count,
    const VkBufferImageCopy* regions) {
    if (!regions || region_count == 0) {
        return false;
    }
    if (device == VK_NULL_HANDLE) {
        return false;
    }

    VirtualImageInfo virtual_info{};
    bool is_virtual_image = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_virtual_images.find(dispatch_key(dst_image));
        if (it != g_virtual_images.end()) {
            virtual_info = it->second;
            is_virtual_image = true;
        }
    }
    if (!is_virtual_image) {
        return false;
    }

    auto runtime = get_or_create_compute_runtime(dispatch_key(device));
    auto vma_runtime = get_or_create_vma_runtime(dispatch_key(device));
    {
        std::lock_guard<std::mutex> init_guard(runtime->init_mutex);
        if (!initialize_compute_runtime(device, dispatch, runtime.get())) {
            EXYNOS_LOGW("Compute decoder runtime unavailable for virtual BCn image. Dropping native copy fallback.");
            return true;
        }
    }
    {
        std::lock_guard<std::mutex> init_guard(vma_runtime->init_mutex);
        if (!initialize_vma_runtime(dispatch_key(device), device, dispatch, vma_runtime.get())) {
            EXYNOS_LOGW("VMA runtime unavailable for BCn decode path. Dropping native copy fallback.");
            return true;
        }
    }
    if (vma_runtime->allocator == VK_NULL_HANDLE) {
        EXYNOS_LOGW("VMA allocator is null for BCn decode path.");
        return true;
    }

    if (dst_layout == VK_IMAGE_LAYOUT_UNDEFINED || dst_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        EXYNOS_LOGW("Unsupported dst layout for BCn decode path (%d).", static_cast<int>(dst_layout));
        return true;
    }

    bool all_regions_decoded = true;
    for (uint32_t r = 0; r < region_count; ++r) {
        const VkBufferImageCopy& region = regions[r];
        if (region.imageExtent.depth != 1) {
            all_regions_decoded = false;
            break;
        }

        uint32_t layer_count = region.imageSubresource.layerCount ? region.imageSubresource.layerCount : 1u;
        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            if (!record_decode_region(
                command_buffer,
                device,
                dispatch,
                runtime.get(),
                vma_runtime->allocator,
                dst_image,
                dst_layout,
                src_buffer,
                    virtual_info.requested_format,
                    virtual_info.real_format,
                    region,
                    layer)) {
                all_regions_decoded = false;
                break;
            }
        }
        if (!all_regions_decoded) {
            break;
        }
    }

    if (!all_regions_decoded) {
        EXYNOS_LOGW("BCn decode dispatch failed for virtual image. Native fallback blocked to avoid invalid copy.");
    }
    return true;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    auto* chain_info = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain_info &&
           (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
            chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain_info->pNext));
    }
    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkCreateInstance next_create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(next_gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    if (!next_create_instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = next_create_instance(pCreateInfo, pAllocator, pInstance);
    if (result != VK_SUCCESS || !pInstance || *pInstance == VK_NULL_HANDLE) {
        return result;
    }

    InstanceDispatch dispatch{};
    dispatch.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        next_gipa(*pInstance, "vkGetInstanceProcAddr"));
    dispatch.destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(
        next_gipa(*pInstance, "vkDestroyInstance"));
    dispatch.create_device = reinterpret_cast<PFN_vkCreateDevice>(
        next_gipa(*pInstance, "vkCreateDevice"));
    dispatch.enumerate_physical_devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        next_gipa(*pInstance, "vkEnumeratePhysicalDevices"));
    dispatch.get_physical_device_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceProperties"));
    dispatch.get_physical_device_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties"));
    dispatch.get_physical_device_image_format_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties"));
    dispatch.get_physical_device_format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2"));
    dispatch.get_physical_device_image_format_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2"));
#ifdef VK_KHR_get_physical_device_properties2
    dispatch.get_physical_device_format_properties2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties2KHR"));
    dispatch.get_physical_device_image_format_properties2_khr = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2KHR>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2KHR"));
#endif

    std::lock_guard<std::shared_mutex> guard(g_lock);
    g_instance_dispatch[dispatch_key(*pInstance)] = dispatch;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {
    InstanceDispatch dispatch{};
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        g_instance_dispatch.erase(it);
        for (auto phys_it = g_physical_to_instance.begin(); phys_it != g_physical_to_instance.end();) {
            if (phys_it->second == dispatch_key(instance)) {
                g_physical_to_instance_handle.erase(phys_it->first);
                g_physical_runtime.erase(phys_it->first);
                phys_it = g_physical_to_instance.erase(phys_it);
            } else {
                ++phys_it;
            }
        }
    }

    if (dispatch.destroy_instance) {
        dispatch.destroy_instance(instance, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_EnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {
    InstanceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.enumerate_physical_devices) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.enumerate_physical_devices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if (result == VK_SUCCESS && pPhysicalDevices && pPhysicalDeviceCount) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            auto phys_key = dispatch_key(pPhysicalDevices[i]);
            g_physical_to_instance[phys_key] = dispatch_key(instance);
            g_physical_to_instance_handle[phys_key] = instance;
            PhysicalRuntime phys_runtime{};
            if (dispatch.get_physical_device_properties) {
                VkPhysicalDeviceProperties props{};
                dispatch.get_physical_device_properties(pPhysicalDevices[i], &props);
                phys_runtime.vendor_id = props.vendorID;
                phys_runtime.is_xclipse = (props.vendorID == 0x144D) || (std::strstr(props.deviceName, "Xclipse") != nullptr);
            }
            g_physical_runtime[phys_key] = phys_runtime;
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_format_properties) {
        std::memset(pFormatProperties, 0, sizeof(*pFormatProperties));
        return;
    }

    dispatch.get_physical_device_format_properties(physicalDevice, format, pFormatProperties);
    virtualize_format_properties_if_needed(physicalDevice, format, pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
        return;
    }

    if (dispatch.get_physical_device_format_properties2) {
        dispatch.get_physical_device_format_properties2(physicalDevice, format, pFormatProperties);
    } else if (dispatch.get_physical_device_format_properties) {
        dispatch.get_physical_device_format_properties(physicalDevice, format, &pFormatProperties->formatProperties);
    } else {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
    }

    virtualize_format_properties_if_needed(physicalDevice, format, &pFormatProperties->formatProperties);
}

#ifdef VK_KHR_get_physical_device_properties2
VKAPI_ATTR void VKAPI_CALL layer_GetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2KHR* pFormatProperties) {
    if (!pFormatProperties) {
        return;
    }

    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr)) {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
        return;
    }

    if (dispatch.get_physical_device_format_properties2_khr) {
        dispatch.get_physical_device_format_properties2_khr(physicalDevice, format, pFormatProperties);
    } else if (dispatch.get_physical_device_format_properties2) {
        dispatch.get_physical_device_format_properties2(
            physicalDevice,
            format,
            reinterpret_cast<VkFormatProperties2*>(pFormatProperties));
    } else if (dispatch.get_physical_device_format_properties) {
        dispatch.get_physical_device_format_properties(physicalDevice, format, &pFormatProperties->formatProperties);
    } else {
        std::memset(&pFormatProperties->formatProperties, 0, sizeof(pFormatProperties->formatProperties));
    }

    virtualize_format_properties_if_needed(physicalDevice, format, &pFormatProperties->formatProperties);
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !dispatch.get_physical_device_image_format_properties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkFormat query_format = format;
    bool xclipse_physical = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        xclipse_physical = is_xclipse_physical(dispatch_key(physicalDevice));
    }
    if (xclipse_physical && is_bcn_format(format)) {
        VkFormat replacement = bcn_replacement_format(format);
        if (replacement != VK_FORMAT_UNDEFINED) {
            query_format = replacement;
        }
    }
    return dispatch.get_physical_device_image_format_properties(
        physicalDevice, query_format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !pImageFormatInfo ||
        !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!dispatch.get_physical_device_image_format_properties2) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkPhysicalDeviceImageFormatInfo2 query_info = *pImageFormatInfo;
    bool xclipse_physical = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        xclipse_physical = is_xclipse_physical(dispatch_key(physicalDevice));
    }
    if (xclipse_physical && is_bcn_format(query_info.format)) {
        VkFormat replacement = bcn_replacement_format(query_info.format);
        if (replacement != VK_FORMAT_UNDEFINED) {
            query_info.format = replacement;
        }
    }
    return dispatch.get_physical_device_image_format_properties2(physicalDevice, &query_info, pImageFormatProperties);
}

#ifdef VK_KHR_get_physical_device_properties2
VKAPI_ATTR VkResult VKAPI_CALL layer_GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    InstanceDispatch dispatch{};
    if (!get_instance_dispatch_for_physical(physicalDevice, &dispatch, nullptr) ||
        !pImageFormatInfo ||
        !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDeviceImageFormatInfo2 query_info = *pImageFormatInfo;
    bool xclipse_physical = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        xclipse_physical = is_xclipse_physical(dispatch_key(physicalDevice));
    }
    if (xclipse_physical && is_bcn_format(query_info.format)) {
        VkFormat replacement = bcn_replacement_format(query_info.format);
        if (replacement != VK_FORMAT_UNDEFINED) {
            query_info.format = replacement;
        }
    }

    if (dispatch.get_physical_device_image_format_properties2_khr) {
        return dispatch.get_physical_device_image_format_properties2_khr(
            physicalDevice,
            &query_info,
            pImageFormatProperties);
    }
    if (dispatch.get_physical_device_image_format_properties2) {
        return dispatch.get_physical_device_image_format_properties2(physicalDevice, &query_info, pImageFormatProperties);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    InstanceDispatch instance_dispatch{};
    VkInstance instance = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_map = g_physical_to_instance.find(dispatch_key(physicalDevice));
        if (it_map == g_physical_to_instance.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        auto it_inst = g_instance_dispatch.find(it_map->second);
        if (it_inst == g_instance_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        auto it_inst_handle = g_physical_to_instance_handle.find(dispatch_key(physicalDevice));
        if (it_inst_handle != g_physical_to_instance_handle.end()) {
            instance = it_inst_handle->second;
        }
        instance_dispatch = it_inst->second;
    }

    if (!instance_dispatch.create_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto* chain_info = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(pCreateInfo->pNext));
    while (chain_info &&
           (chain_info->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
            chain_info->function != VK_LAYER_LINK_INFO)) {
        chain_info = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain_info->pNext));
    }
    if (!chain_info || !chain_info->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr next_gipa = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    VkResult result = instance_dispatch.create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS || !pDevice || *pDevice == VK_NULL_HANDLE) {
        return result;
    }

    DeviceDispatch device_dispatch{};
    device_dispatch.get_device_proc_addr = next_gdpa;
    device_dispatch.destroy_device = reinterpret_cast<PFN_vkDestroyDevice>(
        next_gdpa(*pDevice, "vkDestroyDevice"));
    device_dispatch.create_image = reinterpret_cast<PFN_vkCreateImage>(
        next_gdpa(*pDevice, "vkCreateImage"));
    device_dispatch.destroy_image = reinterpret_cast<PFN_vkDestroyImage>(
        next_gdpa(*pDevice, "vkDestroyImage"));
    device_dispatch.create_image_view = reinterpret_cast<PFN_vkCreateImageView>(
        next_gdpa(*pDevice, "vkCreateImageView"));
    device_dispatch.destroy_image_view = reinterpret_cast<PFN_vkDestroyImageView>(
        next_gdpa(*pDevice, "vkDestroyImageView"));
    device_dispatch.create_command_pool = reinterpret_cast<PFN_vkCreateCommandPool>(
        next_gdpa(*pDevice, "vkCreateCommandPool"));
    device_dispatch.destroy_command_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(
        next_gdpa(*pDevice, "vkDestroyCommandPool"));
    device_dispatch.reset_command_pool = reinterpret_cast<PFN_vkResetCommandPool>(
        next_gdpa(*pDevice, "vkResetCommandPool"));
    device_dispatch.allocate_command_buffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
        next_gdpa(*pDevice, "vkAllocateCommandBuffers"));
    device_dispatch.free_command_buffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(
        next_gdpa(*pDevice, "vkFreeCommandBuffers"));
    device_dispatch.create_shader_module = reinterpret_cast<PFN_vkCreateShaderModule>(
        next_gdpa(*pDevice, "vkCreateShaderModule"));
    device_dispatch.destroy_shader_module = reinterpret_cast<PFN_vkDestroyShaderModule>(
        next_gdpa(*pDevice, "vkDestroyShaderModule"));
    device_dispatch.create_descriptor_set_layout = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(
        next_gdpa(*pDevice, "vkCreateDescriptorSetLayout"));
    device_dispatch.destroy_descriptor_set_layout = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(
        next_gdpa(*pDevice, "vkDestroyDescriptorSetLayout"));
    device_dispatch.create_pipeline_layout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
        next_gdpa(*pDevice, "vkCreatePipelineLayout"));
    device_dispatch.destroy_pipeline_layout = reinterpret_cast<PFN_vkDestroyPipelineLayout>(
        next_gdpa(*pDevice, "vkDestroyPipelineLayout"));
    device_dispatch.create_compute_pipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(
        next_gdpa(*pDevice, "vkCreateComputePipelines"));
    device_dispatch.destroy_pipeline = reinterpret_cast<PFN_vkDestroyPipeline>(
        next_gdpa(*pDevice, "vkDestroyPipeline"));
    device_dispatch.create_descriptor_pool = reinterpret_cast<PFN_vkCreateDescriptorPool>(
        next_gdpa(*pDevice, "vkCreateDescriptorPool"));
    device_dispatch.destroy_descriptor_pool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(
        next_gdpa(*pDevice, "vkDestroyDescriptorPool"));
    device_dispatch.allocate_descriptor_sets = reinterpret_cast<PFN_vkAllocateDescriptorSets>(
        next_gdpa(*pDevice, "vkAllocateDescriptorSets"));
    device_dispatch.free_descriptor_sets = reinterpret_cast<PFN_vkFreeDescriptorSets>(
        next_gdpa(*pDevice, "vkFreeDescriptorSets"));
    device_dispatch.update_descriptor_sets = reinterpret_cast<PFN_vkUpdateDescriptorSets>(
        next_gdpa(*pDevice, "vkUpdateDescriptorSets"));
    device_dispatch.cmd_bind_pipeline = reinterpret_cast<PFN_vkCmdBindPipeline>(
        next_gdpa(*pDevice, "vkCmdBindPipeline"));
    device_dispatch.cmd_bind_descriptor_sets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
        next_gdpa(*pDevice, "vkCmdBindDescriptorSets"));
    device_dispatch.cmd_push_constants = reinterpret_cast<PFN_vkCmdPushConstants>(
        next_gdpa(*pDevice, "vkCmdPushConstants"));
    device_dispatch.cmd_dispatch = reinterpret_cast<PFN_vkCmdDispatch>(
        next_gdpa(*pDevice, "vkCmdDispatch"));
    device_dispatch.cmd_pipeline_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(
        next_gdpa(*pDevice, "vkCmdPipelineBarrier"));
    device_dispatch.cmd_copy_buffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(
        next_gdpa(*pDevice, "vkCmdCopyBuffer"));
    device_dispatch.cmd_copy_buffer_to_image = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage"));
    device_dispatch.cmd_copy_buffer_to_image2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2"));
#ifdef VK_KHR_copy_commands2
    device_dispatch.cmd_copy_buffer_to_image2_khr = reinterpret_cast<PFN_vkCmdCopyBufferToImage2KHR>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2KHR"));
#endif

    DeviceRuntime runtime{};
    if (instance != VK_NULL_HANDLE && instance_dispatch.get_instance_proc_addr) {
        auto get_physical_device_properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceProperties"));
        auto get_physical_device_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures"));

        if (get_physical_device_properties) {
            VkPhysicalDeviceProperties props{};
            get_physical_device_properties(physicalDevice, &props);
            runtime.vendor_id = props.vendorID;
            runtime.is_xclipse = (props.vendorID == 0x144D) || (std::strstr(props.deviceName, "Xclipse") != nullptr);
        }
        if (get_physical_device_features) {
            VkPhysicalDeviceFeatures features{};
            get_physical_device_features(physicalDevice, &features);
            runtime.geometry_shader = (features.geometryShader == VK_TRUE);
            runtime.tessellation_shader = (features.tessellationShader == VK_TRUE);
        }

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
        auto get_physical_device_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures2"));
        if (get_physical_device_features2) {
            VkPhysicalDeviceTransformFeedbackFeaturesEXT tf_features{};
            tf_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.pNext = &tf_features;
            get_physical_device_features2(physicalDevice, &features2);
            runtime.transform_feedback = (tf_features.transformFeedback == VK_TRUE);
        }
#endif
    }

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_device_dispatch[dispatch_key(*pDevice)] = device_dispatch;
        g_device_runtime[dispatch_key(*pDevice)] = runtime;
        g_device_to_instance_handle[dispatch_key(*pDevice)] = instance;
        g_device_to_physical_handle[dispatch_key(*pDevice)] = physicalDevice;
    }
    (void)next_gipa;
    if (runtime.is_xclipse) {
        EXYNOS_LOGI(
            "Xclipse device detected (vendor=0x%04x, geom=%d, tess=%d, tfb=%d)",
            runtime.vendor_id,
            static_cast<int>(runtime.geometry_shader),
            static_cast<int>(runtime.tessellation_shader),
            static_cast<int>(runtime.transform_feedback));
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    DeviceDispatch dispatch{};
    std::shared_ptr<ComputeRuntime> compute_runtime;
    std::shared_ptr<VmaRuntime> vma_runtime;
    std::vector<VkImageView> storage_views_to_destroy;
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<VkDescriptorSet> descriptor_sets_to_release;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        void* device_key = dispatch_key(device);
        auto it = g_device_dispatch.find(device_key);
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        g_device_dispatch.erase(it);
        g_device_runtime.erase(device_key);
        g_device_to_instance_handle.erase(device_key);
        g_device_to_physical_handle.erase(device_key);
        for (auto pool_it = g_command_pool_to_device.begin(); pool_it != g_command_pool_to_device.end();) {
            if (pool_it->second == device_key) {
                pool_it = g_command_pool_to_device.erase(pool_it);
            } else {
                ++pool_it;
            }
        }
        for (auto image_it = g_image_to_device.begin(); image_it != g_image_to_device.end();) {
            if (image_it->second == device_key) {
                g_virtual_images.erase(image_it->first);
                image_it = g_image_to_device.erase(image_it);
            } else {
                ++image_it;
            }
        }
        for (auto view_it = g_storage_views.begin(); view_it != g_storage_views.end();) {
            auto image_owner_it = g_image_to_device.find(view_it->first.image);
            if (image_owner_it == g_image_to_device.end() || image_owner_it->second == device_key) {
                storage_views_to_destroy.push_back(view_it->second);
                view_it = g_storage_views.erase(view_it);
            } else {
                ++view_it;
            }
        }
        auto compute_it = g_compute_runtime.find(device_key);
        if (compute_it != g_compute_runtime.end()) {
            compute_runtime = compute_it->second;
            if (compute_runtime) {
                descriptor_pool = compute_runtime->descriptor_pool;
            }
            g_compute_runtime.erase(compute_it);
        }
        auto vma_it = g_vma_runtime.find(device_key);
        if (vma_it != g_vma_runtime.end()) {
            vma_runtime = vma_it->second;
            g_vma_runtime.erase(vma_it);
        }
        for (auto cb_it = g_command_buffer_to_device.begin(); cb_it != g_command_buffer_to_device.end();) {
            if (cb_it->second == device_key) {
                take_command_buffer_staging_allocations_locked(cb_it->first, &staging_allocations_to_release);
                take_command_buffer_descriptor_sets_locked(cb_it->first, &descriptor_sets_to_release);
                g_command_buffer_to_pool.erase(cb_it->first);
                g_command_buffer_device_handle.erase(cb_it->first);
                cb_it = g_command_buffer_to_device.erase(cb_it);
            } else {
                ++cb_it;
            }
        }
    }

    if (vma_runtime && vma_runtime->allocator != VK_NULL_HANDLE) {
        release_staging_allocations(vma_runtime->allocator, &staging_allocations_to_release);
        vmaDestroyAllocator(vma_runtime->allocator);
        vma_runtime->allocator = VK_NULL_HANDLE;
        vma_runtime->initialized = false;
    } else {
        staging_allocations_to_release.clear();
    }
    release_descriptor_sets(device, dispatch, descriptor_pool, &descriptor_sets_to_release);
    if (dispatch.destroy_image_view) {
        for (VkImageView view : storage_views_to_destroy) {
            dispatch.destroy_image_view(device, view, nullptr);
        }
    }
    if (compute_runtime) {
        destroy_compute_runtime(device, dispatch, compute_runtime.get());
    }
    if (dispatch.destroy_device) {
        dispatch.destroy_device(device, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {
    DeviceDispatch dispatch{};
    bool xclipse_device = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        void* key = dispatch_key(device);
        auto it = g_device_dispatch.find(key);
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        xclipse_device = is_xclipse_device(key);
    }

    if (!dispatch.create_image) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (xclipse_device && pCreateInfo && is_bcn_format(pCreateInfo->format)) {
        VkFormat replacement = bcn_replacement_format(pCreateInfo->format);
        if (replacement != VK_FORMAT_UNDEFINED) {
            VkImageCreateInfo patched_info = *pCreateInfo;
            patched_info.format = replacement;
            VkResult result = dispatch.create_image(device, &patched_info, pAllocator, pImage);
            if (result == VK_SUCCESS && pImage && *pImage != VK_NULL_HANDLE) {
                std::lock_guard<std::shared_mutex> guard(g_lock);
                auto image_key = dispatch_key(*pImage);
                g_virtual_images[image_key] = VirtualImageInfo{pCreateInfo->format, replacement};
                g_image_to_device[image_key] = dispatch_key(device);
            }
            EXYNOS_LOGI(
                "Virtualized BCn image create (requested=%d, replacement=%d)",
                static_cast<int>(pCreateInfo->format),
                static_cast<int>(replacement));
            return result;
        }
    }

    VkResult result = dispatch.create_image(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS && pImage && *pImage != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_image_to_device[dispatch_key(*pImage)] = dispatch_key(device);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {
    DeviceDispatch dispatch{};
    std::vector<VkImageView> storage_views_to_destroy;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        auto image_key = dispatch_key(image);
        g_virtual_images.erase(image_key);
        g_image_to_device.erase(image_key);
        for (auto view_it = g_storage_views.begin(); view_it != g_storage_views.end();) {
            if (view_it->first.image == image_key) {
                storage_views_to_destroy.push_back(view_it->second);
                view_it = g_storage_views.erase(view_it);
            } else {
                ++view_it;
            }
        }
    }

    if (dispatch.destroy_image_view) {
        for (VkImageView view : storage_views_to_destroy) {
            dispatch.destroy_image_view(device, view, nullptr);
        }
    }
    if (dispatch.destroy_image) {
        dispatch.destroy_image(device, image, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView) {
    DeviceDispatch dispatch{};
    VirtualImageInfo virtual_info{};
    bool has_virtual_image = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        if (pCreateInfo) {
            auto it_image = g_virtual_images.find(dispatch_key(pCreateInfo->image));
            if (it_image != g_virtual_images.end()) {
                virtual_info = it_image->second;
                has_virtual_image = true;
            }
        }
    }

    if (!dispatch.create_image_view) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (has_virtual_image && pCreateInfo) {
        VkImageViewCreateInfo patched_view = *pCreateInfo;
        if (patched_view.format == virtual_info.requested_format || is_bcn_format(patched_view.format)) {
            patched_view.format = virtual_info.real_format;
        }
        return dispatch.create_image_view(device, &patched_view, pAllocator, pView);
    }

    return dispatch.create_image_view(device, pCreateInfo, pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_CreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.create_command_pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.create_command_pool(device, pCreateInfo, pAllocator, pCommandPool);
    if (result == VK_SUCCESS && pCommandPool && *pCommandPool != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_command_pool_to_device[dispatch_key(*pCommandPool)] = dispatch_key(device);
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_DestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<VkDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        auto vma_it = g_vma_runtime.find(dispatch_key(device));
        if (vma_it != g_vma_runtime.end() && vma_it->second) {
            allocator = vma_it->second->allocator;
        }
        auto compute_it = g_compute_runtime.find(dispatch_key(device));
        if (compute_it != g_compute_runtime.end() && compute_it->second) {
            descriptor_pool = compute_it->second->descriptor_pool;
        }
        g_command_pool_to_device.erase(dispatch_key(commandPool));
        for (auto cb_it = g_command_buffer_to_pool.begin(); cb_it != g_command_buffer_to_pool.end();) {
            if (cb_it->second == dispatch_key(commandPool)) {
                take_command_buffer_staging_allocations_locked(cb_it->first, &staging_allocations_to_release);
                take_command_buffer_descriptor_sets_locked(cb_it->first, &descriptor_sets_to_release);
                g_command_buffer_to_device.erase(cb_it->first);
                g_command_buffer_device_handle.erase(cb_it->first);
                cb_it = g_command_buffer_to_pool.erase(cb_it);
            } else {
                ++cb_it;
            }
        }
    }

    release_staging_allocations(allocator, &staging_allocations_to_release);
    release_descriptor_sets(device, dispatch, descriptor_pool, &descriptor_sets_to_release);
    if (dispatch.destroy_command_pool) {
        dispatch.destroy_command_pool(device, commandPool, pAllocator);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL layer_ResetCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandPoolResetFlags flags) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<VkDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        auto vma_it = g_vma_runtime.find(dispatch_key(device));
        if (vma_it != g_vma_runtime.end() && vma_it->second) {
            allocator = vma_it->second->allocator;
        }
        auto compute_it = g_compute_runtime.find(dispatch_key(device));
        if (compute_it != g_compute_runtime.end() && compute_it->second) {
            descriptor_pool = compute_it->second->descriptor_pool;
        }
        for (auto cb_it = g_command_buffer_to_pool.begin(); cb_it != g_command_buffer_to_pool.end();) {
            if (cb_it->second == dispatch_key(commandPool)) {
                take_command_buffer_staging_allocations_locked(cb_it->first, &staging_allocations_to_release);
                take_command_buffer_descriptor_sets_locked(cb_it->first, &descriptor_sets_to_release);
                ++cb_it;
            } else {
                ++cb_it;
            }
        }
    }

    release_staging_allocations(allocator, &staging_allocations_to_release);
    release_descriptor_sets(device, dispatch, descriptor_pool, &descriptor_sets_to_release);
    if (!dispatch.reset_command_pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return dispatch.reset_command_pool(device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_AllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {
    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
    }

    if (!dispatch.allocate_command_buffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.allocate_command_buffers(device, pAllocateInfo, pCommandBuffers);
    if (result == VK_SUCCESS && pAllocateInfo && pCommandBuffers) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_command_pool_to_device[dispatch_key(pAllocateInfo->commandPool)] = dispatch_key(device);
        for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
            const void* command_buffer_key = dispatch_key(pCommandBuffers[i]);
            g_command_buffer_to_device[const_cast<void*>(command_buffer_key)] = dispatch_key(device);
            g_command_buffer_device_handle[const_cast<void*>(command_buffer_key)] = device;
            g_command_buffer_to_pool[const_cast<void*>(command_buffer_key)] = dispatch_key(pAllocateInfo->commandPool);
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL layer_FreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers) {
    DeviceDispatch dispatch{};
    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<VkDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return;
        }
        dispatch = it->second;
        auto vma_it = g_vma_runtime.find(dispatch_key(device));
        if (vma_it != g_vma_runtime.end() && vma_it->second) {
            allocator = vma_it->second->allocator;
        }
        auto compute_it = g_compute_runtime.find(dispatch_key(device));
        if (compute_it != g_compute_runtime.end() && compute_it->second) {
            descriptor_pool = compute_it->second->descriptor_pool;
        }
        if (pCommandBuffers) {
            for (uint32_t i = 0; i < commandBufferCount; ++i) {
                void* cb_key = dispatch_key(pCommandBuffers[i]);
                take_command_buffer_staging_allocations_locked(cb_key, &staging_allocations_to_release);
                take_command_buffer_descriptor_sets_locked(cb_key, &descriptor_sets_to_release);
                g_command_buffer_to_device.erase(cb_key);
                g_command_buffer_device_handle.erase(cb_key);
                g_command_buffer_to_pool.erase(cb_key);
            }
        }
    }

    release_staging_allocations(allocator, &staging_allocations_to_release);
    release_descriptor_sets(device, dispatch, descriptor_pool, &descriptor_sets_to_release);
    if (dispatch.free_command_buffers) {
        dispatch.free_command_buffers(device, commandPool, commandBufferCount, pCommandBuffers);
    }
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_cb = g_command_buffer_to_device.find(dispatch_key(commandBuffer));
        if (it_cb == g_command_buffer_to_device.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Command buffer mapping missing for vkCmdCopyBufferToImage. Skipping interception.");
            }
            return;
        }
        auto it_dev = g_device_dispatch.find(it_cb->second);
        if (it_dev == g_device_dispatch.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Device mapping missing for vkCmdCopyBufferToImage. Skipping interception.");
            }
            return;
        }
        dispatch = it_dev->second;
        auto it_dev_handle = g_command_buffer_device_handle.find(dispatch_key(commandBuffer));
        if (it_dev_handle != g_command_buffer_device_handle.end()) {
            device = it_dev_handle->second;
        }
    }

    if (dispatch.cmd_copy_buffer_to_image) {
        if (!try_decode_copy_regions(
                commandBuffer,
                device,
                dispatch,
                srcBuffer,
                dstImage,
                dstImageLayout,
                regionCount,
                pRegions)) {
            dispatch.cmd_copy_buffer_to_image(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
        }
    }
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_cb = g_command_buffer_to_device.find(dispatch_key(commandBuffer));
        if (it_cb == g_command_buffer_to_device.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Command buffer mapping missing for vkCmdCopyBufferToImage2. Skipping interception.");
            }
            return;
        }
        auto it_dev = g_device_dispatch.find(it_cb->second);
        if (it_dev == g_device_dispatch.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Device mapping missing for vkCmdCopyBufferToImage2. Skipping interception.");
            }
            return;
        }
        dispatch = it_dev->second;
        auto it_dev_handle = g_command_buffer_device_handle.find(dispatch_key(commandBuffer));
        if (it_dev_handle != g_command_buffer_device_handle.end()) {
            device = it_dev_handle->second;
        }
    }

    if (dispatch.cmd_copy_buffer_to_image2 && pCopyBufferToImageInfo) {
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(pCopyBufferToImageInfo->regionCount);
        for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; ++i) {
            const VkBufferImageCopy2& src_region = pCopyBufferToImageInfo->pRegions[i];
            VkBufferImageCopy dst_region{};
            dst_region.bufferOffset = src_region.bufferOffset;
            dst_region.bufferRowLength = src_region.bufferRowLength;
            dst_region.bufferImageHeight = src_region.bufferImageHeight;
            dst_region.imageSubresource = src_region.imageSubresource;
            dst_region.imageOffset = src_region.imageOffset;
            dst_region.imageExtent = src_region.imageExtent;
            regions.push_back(dst_region);
        }

        if (!try_decode_copy_regions(
                commandBuffer,
                device,
                dispatch,
                pCopyBufferToImageInfo->srcBuffer,
                pCopyBufferToImageInfo->dstImage,
                pCopyBufferToImageInfo->dstImageLayout,
                static_cast<uint32_t>(regions.size()),
                regions.data())) {
            dispatch.cmd_copy_buffer_to_image2(commandBuffer, pCopyBufferToImageInfo);
        }
    }
}

#ifdef VK_KHR_copy_commands2
VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2KHR* pCopyBufferToImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_cb = g_command_buffer_to_device.find(dispatch_key(commandBuffer));
        if (it_cb == g_command_buffer_to_device.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Command buffer mapping missing for vkCmdCopyBufferToImage2KHR. Skipping interception.");
            }
            return;
        }
        auto it_dev = g_device_dispatch.find(it_cb->second);
        if (it_dev == g_device_dispatch.end()) {
            if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
                EXYNOS_LOGW("Device mapping missing for vkCmdCopyBufferToImage2KHR. Skipping interception.");
            }
            return;
        }
        dispatch = it_dev->second;
        auto it_dev_handle = g_command_buffer_device_handle.find(dispatch_key(commandBuffer));
        if (it_dev_handle != g_command_buffer_device_handle.end()) {
            device = it_dev_handle->second;
        }
    }

    if (dispatch.cmd_copy_buffer_to_image2_khr && pCopyBufferToImageInfo) {
        std::vector<VkBufferImageCopy> regions;
        regions.reserve(pCopyBufferToImageInfo->regionCount);
        for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; ++i) {
            const VkBufferImageCopy2KHR& src_region = pCopyBufferToImageInfo->pRegions[i];
            VkBufferImageCopy dst_region{};
            dst_region.bufferOffset = src_region.bufferOffset;
            dst_region.bufferRowLength = src_region.bufferRowLength;
            dst_region.bufferImageHeight = src_region.bufferImageHeight;
            dst_region.imageSubresource = src_region.imageSubresource;
            dst_region.imageOffset = src_region.imageOffset;
            dst_region.imageExtent = src_region.imageExtent;
            regions.push_back(dst_region);
        }

        if (!try_decode_copy_regions(
                commandBuffer,
                device,
                dispatch,
                pCopyBufferToImageInfo->srcBuffer,
                pCopyBufferToImageInfo->dstImage,
                pCopyBufferToImageInfo->dstImageLayout,
                static_cast<uint32_t>(regions.size()),
                regions.data())) {
            dispatch.cmd_copy_buffer_to_image2_khr(commandBuffer, pCopyBufferToImageInfo);
        }
    }
}
#endif

void fill_layer_property(VkLayerProperties* p) {
    std::memset(p, 0, sizeof(*p));
    std::strncpy(p->layerName, kLayerName, sizeof(p->layerName) - 1);
    std::strncpy(p->description, "ExynosTools BCn Layer (clean bootstrap)", sizeof(p->description) - 1);
    p->specVersion = VK_API_VERSION_1_3;
    p->implementationVersion = kLayerImplVersion;
}

VkResult enumerate_layer_props(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    if (*pPropertyCount == 0) {
        return VK_INCOMPLETE;
    }

    fill_layer_property(&pProperties[0]);
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

} // namespace

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (pLayerName && std::strcmp(pLayerName, kLayerName) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    if (pLayerName && std::strcmp(pLayerName, kLayerName) != 0) {
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    if (!pName) {
        return nullptr;
    }

    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateInstance);
    if (std::strcmp(pName, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyInstance);
    if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_EnumeratePhysicalDevices);
    if (std::strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateDevice);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties);
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties2);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties2);
#ifdef VK_KHR_get_physical_device_properties2
    if (std::strcmp(pName, "vkGetPhysicalDeviceFormatProperties2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceFormatProperties2KHR);
    if (std::strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_GetPhysicalDeviceImageFormatProperties2KHR);
#endif

    if (instance == VK_NULL_HANDLE) {
        return nullptr;
    }

    InstanceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_instance_dispatch.find(dispatch_key(instance));
        if (it == g_instance_dispatch.end()) {
            return nullptr;
        }
        dispatch = it->second;
    }

    if (!dispatch.get_instance_proc_addr) {
        return nullptr;
    }
    return dispatch.get_instance_proc_addr(instance, pName);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName) {
    if (!pName) {
        return nullptr;
    }

    auto should_intercept_copy_path = [&]() -> bool {
        if (device == VK_NULL_HANDLE) {
            return true;
        }
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_runtime.find(dispatch_key(device));
        if (it == g_device_runtime.end()) {
            return true;
        }
        return it->second.is_xclipse;
    };

    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyDevice);
    if (std::strcmp(pName, "vkCreateImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateImage);
    if (std::strcmp(pName, "vkDestroyImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyImage);
    if (std::strcmp(pName, "vkCreateImageView") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateImageView);
    if (std::strcmp(pName, "vkCreateCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_CreateCommandPool);
    if (std::strcmp(pName, "vkDestroyCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_DestroyCommandPool);
    if (std::strcmp(pName, "vkResetCommandPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_ResetCommandPool);
    if (std::strcmp(pName, "vkAllocateCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_AllocateCommandBuffers);
    if (std::strcmp(pName, "vkFreeCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_FreeCommandBuffers);
    if (std::strcmp(pName, "vkCmdCopyBufferToImage") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage);
    }
    if (std::strcmp(pName, "vkCmdCopyBufferToImage2") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage2);
    }
#ifdef VK_KHR_copy_commands2
    if (std::strcmp(pName, "vkCmdCopyBufferToImage2KHR") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyBufferToImage2KHR);
    }
#endif

    if (device == VK_NULL_HANDLE) {
        return nullptr;
    }

    DeviceDispatch dispatch{};
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_device_dispatch.find(dispatch_key(device));
        if (it == g_device_dispatch.end()) {
            return nullptr;
        }
        dispatch = it->second;
    }

    if (!dispatch.get_device_proc_addr) {
        return nullptr;
    }
    return dispatch.get_device_proc_addr(device, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct) {
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion > CURRENT_LOADER_LAYER_INTERFACE_VERSION) {
        pVersionStruct->loaderLayerInterfaceVersion = CURRENT_LOADER_LAYER_INTERFACE_VERSION;
    }

    pVersionStruct->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    EXYNOS_LOGI("Layer negotiation complete. Interface version=%u", pVersionStruct->loaderLayerInterfaceVersion);
    return VK_SUCCESS;
}
