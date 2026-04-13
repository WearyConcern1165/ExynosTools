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

#if defined(__GNUC__)
#define EXYNOS_LAYER_EXPORT __attribute__((visibility("default")))
#else
#define EXYNOS_LAYER_EXPORT
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
    PFN_vkGetPhysicalDeviceFeatures get_physical_device_features = nullptr;
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
    PFN_vkCreateSampler create_sampler = nullptr;
    PFN_vkDestroySampler destroy_sampler = nullptr;
    PFN_vkCreateCommandPool create_command_pool = nullptr;
    PFN_vkDestroyCommandPool destroy_command_pool = nullptr;
    PFN_vkResetCommandPool reset_command_pool = nullptr;
    PFN_vkBeginCommandBuffer begin_command_buffer = nullptr;
    PFN_vkResetCommandBuffer reset_command_buffer = nullptr;
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
    PFN_vkCmdCopyImage cmd_copy_image = nullptr;
    PFN_vkCmdCopyImage2 cmd_copy_image2 = nullptr;
    PFN_vkCmdCopyBufferToImage cmd_copy_buffer_to_image = nullptr;
    PFN_vkCmdCopyBufferToImage2 cmd_copy_buffer_to_image2 = nullptr;
#ifdef VK_KHR_copy_commands2
    PFN_vkCmdCopyImage2KHR cmd_copy_image2_khr = nullptr;
    PFN_vkCmdCopyBufferToImage2KHR cmd_copy_buffer_to_image2_khr = nullptr;
#endif
};

struct DeviceRuntime {
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
    bool geometry_shader = false;
    bool tessellation_shader = false;
    bool transform_feedback = false;
    bool shader_storage_image_write_without_format = false;
    bool subgroup_size_control = false;
    uint32_t min_subgroup_size = 0;
    uint32_t max_subgroup_size = 0;
    bool descriptor_buffer_supported = false;
    bool descriptor_buffer_enabled = false;
};

struct PhysicalRuntime {
    bool is_xclipse = false;
    uint32_t vendor_id = 0;
};

struct VirtualImageInfo {
    VkFormat requested_format = VK_FORMAT_UNDEFINED;
    VkFormat real_format = VK_FORMAT_UNDEFINED;
};

struct TrackedImageInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
};

enum class DecoderShaderKind : uint32_t {
    None = 0,
    S3tc,
    Rgtc,
    Bc6,
    Bc7,
    CopyImage
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
    std::mutex descriptor_mutex;
    bool initialized = false;
    bool available = false;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptor_pools;
    uint32_t descriptor_pool_capacity = 0;
    VkSampler copy_sampler = VK_NULL_HANDLE;
    VkPipeline pipeline_s3tc = VK_NULL_HANDLE;
    VkPipeline pipeline_rgtc = VK_NULL_HANDLE;
    VkPipeline pipeline_bc6 = VK_NULL_HANDLE;
    VkPipeline pipeline_bc7 = VK_NULL_HANDLE;
    VkPipeline pipeline_copy_image = VK_NULL_HANDLE;
    uint32_t preferred_subgroup_size = 0;
};

struct StagingAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct TrackedDescriptorSet {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
};

struct DecodeImageState {
    bool blocked_passthrough = false;
    uint32_t blocked_copy_count = 0;
    uint32_t failure_count = 0;
};

struct BcnSupportKey {
    void* physical = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;

    bool operator==(const BcnSupportKey& other) const {
        return physical == other.physical &&
               format == other.format &&
               type == other.type &&
               tiling == other.tiling &&
               usage == other.usage &&
               flags == other.flags;
    }
};

struct BcnSupportKeyHash {
    size_t operator()(const BcnSupportKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.physical);
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.format) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.type) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(static_cast<uint32_t>(key.tiling) + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.usage + 0x9e3779b9u + (h << 6) + (h >> 2));
        h ^= static_cast<size_t>(key.flags + 0x9e3779b9u + (h << 6) + (h >> 2));
        return h;
    }
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
std::unordered_map<void*, TrackedImageInfo> g_tracked_images;
std::unordered_map<void*, void*> g_image_to_device;
std::unordered_map<void*, std::shared_ptr<ComputeRuntime>> g_compute_runtime;
std::unordered_map<void*, std::shared_ptr<VmaRuntime>> g_vma_runtime;
std::unordered_map<StorageViewKey, VkImageView, StorageViewKeyHash> g_storage_views;
std::unordered_map<void*, std::vector<StagingAllocation>> g_command_buffer_staging_allocations;
std::unordered_map<void*, std::vector<TrackedDescriptorSet>> g_command_buffer_descriptor_sets;
std::unordered_map<void*, DecodeImageState> g_decode_image_state;
std::unordered_map<BcnSupportKey, bool, BcnSupportKeyHash> g_bcn_native_support_cache;
std::mutex g_tracking_lock;
std::atomic<bool> g_warned_missing_cmd_buffer_map{false};
std::atomic<bool> g_warned_cmd_buffer_dispatch_fallback{false};

const char* kLayerName = "VK_LAYER_EXYNOSTOOLS_bcn";
const uint32_t kLayerImplVersion = 300u;
constexpr uint32_t kDecodePushConstantsSize = sizeof(int32_t) * 8u;
constexpr uint32_t kDescriptorPoolInitialMaxSets = 4096u;
constexpr uint32_t kDescriptorPoolMaxSetsCap = 65536u;
constexpr uint32_t kDescriptorPoolInitialMaxSetsHighEnd = 8192u;
constexpr uint32_t kDecodeBlockedRetryInterval = 64u;

std::atomic<uint64_t> g_decode_attempts{0};
std::atomic<uint64_t> g_decode_successes{0};
std::atomic<uint64_t> g_decode_failures{0};
std::atomic<uint64_t> g_decode_passthrough_activations{0};
std::atomic<uint64_t> g_decode_feature_rejects{0};
std::atomic<uint64_t> g_decode_non2d_rejects{0};
std::atomic<uint64_t> g_decode_blocked_copies{0};
std::atomic<uint64_t> g_decode_retry_attempts{0};
std::atomic<uint64_t> g_decode_stats_log_gate{0};
std::atomic<uint64_t> g_descriptor_pool_growths{0};
std::atomic<uint64_t> g_virtualized_create_images{0};
std::atomic<uint64_t> g_native_bcn_create_images{0};
std::atomic<uint64_t> g_copy_image_calls{0};
std::atomic<uint64_t> g_copy_image_virtual_hits{0};
std::atomic<uint64_t> g_copy_image_real_routes{0};
std::atomic<uint64_t> g_copy_image_special_routes{0};
std::atomic<uint64_t> g_copy_image_special_fallbacks{0};
std::atomic<uint64_t> g_wave32_pipeline_tries{0};
std::atomic<uint64_t> g_wave64_pipeline_tries{0};

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

VkFormat bcn_fallback_replacement_format(VkFormat format) {
    switch (format) {
        // Keep SRGB BCn virtualized to UNORM storage image. The shader handles SRGB decode.
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

VkFormatFeatureFlags required_format_features_for_usage(VkImageUsageFlags usage) {
    if (usage == 0) {
        usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VkFormatFeatureFlags required = 0;
    if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    }
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    }
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    }
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
        required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    }

    if (required == 0) {
        required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    }
    return required;
}

bool query_native_bcn_support_uncached(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags);

bool query_image_format_support_uncached(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (format == VK_FORMAT_UNDEFINED || !dispatch.get_physical_device_format_properties) {
        return false;
    }

    VkFormatProperties format_props{};
    dispatch.get_physical_device_format_properties(physicalDevice, format, &format_props);
    VkFormatFeatureFlags required = required_format_features_for_usage(usage);
    VkFormatFeatureFlags available = (tiling == VK_IMAGE_TILING_LINEAR)
        ? format_props.linearTilingFeatures
        : format_props.optimalTilingFeatures;
    if ((available & required) != required) {
        return false;
    }

    if (dispatch.get_physical_device_image_format_properties2) {
        VkPhysicalDeviceImageFormatInfo2 image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        image_info.format = format;
        image_info.type = type;
        image_info.tiling = tiling;
        image_info.usage = usage;
        image_info.flags = flags;

        VkImageFormatProperties2 image_props{};
        image_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
        return dispatch.get_physical_device_image_format_properties2(
            physicalDevice,
            &image_info,
            &image_props) == VK_SUCCESS;
    }

    if (dispatch.get_physical_device_image_format_properties) {
        VkImageFormatProperties image_props{};
        return dispatch.get_physical_device_image_format_properties(
            physicalDevice,
            format,
            type,
            tiling,
            usage,
            flags,
            &image_props) == VK_SUCCESS;
    }

    return true;
}

bool query_native_bcn_support_uncached(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (!is_bcn_format(format)) {
        return true;
    }
    if (!dispatch.get_physical_device_format_properties) {
        return true;
    }

    return query_image_format_support_uncached(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags);
}

VkFormat bcn_replacement_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    VkImageUsageFlags replacement_usage = usage | VK_IMAGE_USAGE_STORAGE_BIT;
    auto supports_candidate = [&](VkFormat candidate) {
        if (candidate == VK_FORMAT_UNDEFINED || physicalDevice == VK_NULL_HANDLE) {
            return false;
        }
        return query_image_format_support_uncached(
            physicalDevice,
            dispatch,
            candidate,
            type,
            tiling,
            replacement_usage,
            flags);
    };

    switch (format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8_UNORM)) {
                return VK_FORMAT_R8_UNORM;
            }
            break;
        case VK_FORMAT_BC4_SNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8_SNORM)) {
                return VK_FORMAT_R8_SNORM;
            }
            break;
        case VK_FORMAT_BC5_UNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8G8_UNORM)) {
                return VK_FORMAT_R8G8_UNORM;
            }
            break;
        case VK_FORMAT_BC5_SNORM_BLOCK:
            if (supports_candidate(VK_FORMAT_R8G8_SNORM)) {
                return VK_FORMAT_R8G8_SNORM;
            }
            break;
        default:
            break;
    }

    return bcn_fallback_replacement_format(format);
}

bool is_native_bcn_supported(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (!is_bcn_format(format)) {
        return true;
    }

    BcnSupportKey key{};
    key.physical = dispatch_key(physicalDevice);
    key.format = format;
    key.type = type;
    key.tiling = tiling;
    key.usage = usage;
    key.flags = flags;

    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_bcn_native_support_cache.find(key);
        if (it != g_bcn_native_support_cache.end()) {
            return it->second;
        }
    }

    bool native_supported = query_native_bcn_support_uncached(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags);

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        g_bcn_native_support_cache[key] = native_supported;
    }
    return native_supported;
}

bool should_virtualize_bcn_format(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags) {
    if (!is_bcn_format(format)) {
        return false;
    }

    // The current decode path records 2D storage image writes per layer.
    // Reject 3D images here so we don't advertise or create a virtual path
    // that the compute decoder cannot safely populate.
    if (type != VK_IMAGE_TYPE_2D) {
        return false;
    }

    bool xclipse_physical = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_physical_runtime.find(dispatch_key(physicalDevice));
        xclipse_physical = (it != g_physical_runtime.end()) && it->second.is_xclipse;
    }
    if (!xclipse_physical) {
        return false;
    }

    return !is_native_bcn_supported(
        physicalDevice,
        dispatch,
        format,
        type,
        tiling,
        usage,
        flags);
}

bool patch_virtualized_image_format_list(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkImageCreateInfo* original_info,
    VkImageCreateInfo* io_patched_info,
    VkImageFormatListCreateInfo* out_format_list,
    std::vector<VkFormat>* out_formats) {
#ifdef VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO
    if (!original_info || !io_patched_info || !out_format_list || !out_formats) {
        return false;
    }

    const auto* head =
        reinterpret_cast<const VkBaseInStructure*>(original_info->pNext);
    if (!head || head->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
        return false;
    }

    const auto* format_list =
        reinterpret_cast<const VkImageFormatListCreateInfo*>(head);
    if (!format_list->pViewFormats || format_list->viewFormatCount == 0) {
        return false;
    }

    out_formats->clear();
    out_formats->reserve(format_list->viewFormatCount);
    for (uint32_t i = 0; i < format_list->viewFormatCount; ++i) {
        VkFormat view_format = format_list->pViewFormats[i];
        if (is_bcn_format(view_format)) {
            view_format = bcn_replacement_format(
                physicalDevice,
                dispatch,
                view_format,
                original_info->imageType,
                original_info->tiling,
                original_info->usage,
                io_patched_info->flags);
            if (view_format == VK_FORMAT_UNDEFINED) {
                continue;
            }
        }

        if (std::find(out_formats->begin(), out_formats->end(), view_format) ==
            out_formats->end()) {
            out_formats->push_back(view_format);
        }
    }

    if (out_formats->empty()) {
        return false;
    }

    *out_format_list = *format_list;
    out_format_list->pNext = format_list->pNext;
    out_format_list->viewFormatCount =
        static_cast<uint32_t>(out_formats->size());
    out_format_list->pViewFormats = out_formats->data();
    io_patched_info->pNext = out_format_list;
    return true;
#else
    (void)physicalDevice;
    (void)dispatch;
    (void)original_info;
    (void)io_patched_info;
    (void)out_format_list;
    (void)out_formats;
    return false;
#endif
}

bool has_enabled_device_extension(
    const VkDeviceCreateInfo* pCreateInfo,
    const char* extension_name) {
    if (!pCreateInfo || !extension_name || !pCreateInfo->ppEnabledExtensionNames) {
        return false;
    }
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* name = pCreateInfo->ppEnabledExtensionNames[i];
        if (name && std::strcmp(name, extension_name) == 0) {
            return true;
        }
    }
    return false;
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

    if (!should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            requested_format,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0)) {
        return;
    }

    VkFormat replacement = bcn_replacement_format(
        physicalDevice,
        dispatch,
        requested_format,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0);
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
        case DecoderShaderKind::CopyImage:
            return "copy_image_iv.comp.spv";
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
        case DecoderShaderKind::CopyImage:
            return &runtime->pipeline_copy_image;
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

    bool tried_subgroup_variant = false;
#ifdef VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_ci{};
    if (runtime->preferred_subgroup_size == 32u || runtime->preferred_subgroup_size == 64u) {
        subgroup_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        subgroup_ci.requiredSubgroupSize = runtime->preferred_subgroup_size;
        stage_ci.pNext = &subgroup_ci;
#ifdef VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT
        stage_ci.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
#endif
        tried_subgroup_variant = true;
        if (runtime->preferred_subgroup_size == 32u) {
            g_wave32_pipeline_tries.fetch_add(1);
        } else if (runtime->preferred_subgroup_size == 64u) {
            g_wave64_pipeline_tries.fetch_add(1);
        }
    }
#endif

    VkComputePipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_ci.stage = stage_ci;
    pipeline_ci.layout = runtime->pipeline_layout;

    VkResult pipeline_result = dispatch.create_compute_pipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, pipeline_slot);

    if ((pipeline_result != VK_SUCCESS || *pipeline_slot == VK_NULL_HANDLE) && tried_subgroup_variant) {
        stage_ci.pNext = nullptr;
        stage_ci.flags = 0;
        pipeline_ci.stage = stage_ci;
        pipeline_result = dispatch.create_compute_pipelines(
            device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, pipeline_slot);
        if (pipeline_result == VK_SUCCESS && *pipeline_slot != VK_NULL_HANDLE) {
            EXYNOS_LOGW(
                "Subgroup-sized pipeline failed for %s, fallback pipeline created without required subgroup size.",
                shader_name);
        }
    }

    dispatch.destroy_shader_module(device, shader_module, nullptr);
    if (pipeline_result != VK_SUCCESS || *pipeline_slot == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create compute pipeline for %s", shader_name);
        *pipeline_slot = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool create_decode_descriptor_pool(
    VkDevice device,
    const DeviceDispatch& dispatch,
    uint32_t max_sets,
    VkDescriptorPool* out_pool) {
    if (!out_pool || !dispatch.create_descriptor_pool || max_sets == 0) {
        return false;
    }

    VkDescriptorPoolSize pool_sizes[3]{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[0].descriptorCount = max_sets;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[1].descriptorCount = max_sets;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[2].descriptorCount = max_sets;

    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.maxSets = max_sets;
    pool_ci.poolSizeCount = 3;
    pool_ci.pPoolSizes = pool_sizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult result = dispatch.create_descriptor_pool(device, &pool_ci, nullptr, &pool);
    if (result != VK_SUCCESS || pool == VK_NULL_HANDLE) {
        return false;
    }

    *out_pool = pool;
    return true;
}

bool should_retry_descriptor_set_allocation(VkResult result) {
    return result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
}

bool allocate_decode_descriptor_set(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkDescriptorPool* out_pool,
    VkDescriptorSet* out_set) {
    if (!runtime || !out_pool || !out_set || !dispatch.allocate_descriptor_sets) {
        return false;
    }

    std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
    if (runtime->descriptor_set_layout == VK_NULL_HANDLE) {
        return false;
    }
    if (runtime->descriptor_pools.empty()) {
        if (runtime->descriptor_pool_capacity == 0) {
            runtime->descriptor_pool_capacity = kDescriptorPoolInitialMaxSets;
        }
        VkDescriptorPool initial_pool = VK_NULL_HANDLE;
        if (!create_decode_descriptor_pool(device, dispatch, runtime->descriptor_pool_capacity, &initial_pool)) {
            return false;
        }
        runtime->descriptor_pools.push_back(initial_pool);
        runtime->descriptor_pool = initial_pool;
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &runtime->descriptor_set_layout;

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    for (auto it = runtime->descriptor_pools.rbegin(); it != runtime->descriptor_pools.rend(); ++it) {
        alloc_info.descriptorPool = *it;
        descriptor_set = VK_NULL_HANDLE;
        VkResult alloc_result = dispatch.allocate_descriptor_sets(device, &alloc_info, &descriptor_set);
        if (alloc_result == VK_SUCCESS && descriptor_set != VK_NULL_HANDLE) {
            *out_pool = *it;
            *out_set = descriptor_set;
            runtime->descriptor_pool = *it;
            return true;
        }
        if (!should_retry_descriptor_set_allocation(alloc_result)) {
            EXYNOS_LOGW("Descriptor set allocation failed (VkResult=%d).", static_cast<int>(alloc_result));
            return false;
        }
    }

    uint32_t base_capacity = runtime->descriptor_pool_capacity ? runtime->descriptor_pool_capacity : kDescriptorPoolInitialMaxSets;
    uint32_t next_capacity = base_capacity;
    if (next_capacity < kDescriptorPoolMaxSetsCap) {
        next_capacity = std::min(kDescriptorPoolMaxSetsCap, next_capacity * 2u);
    } else {
        next_capacity += kDescriptorPoolInitialMaxSets;
    }

    VkDescriptorPool expanded_pool = VK_NULL_HANDLE;
    if (!create_decode_descriptor_pool(device, dispatch, next_capacity, &expanded_pool)) {
        EXYNOS_LOGW("Failed to grow descriptor pool (requested maxSets=%u).", next_capacity);
        return false;
    }
    runtime->descriptor_pools.push_back(expanded_pool);
    runtime->descriptor_pool = expanded_pool;
    runtime->descriptor_pool_capacity = next_capacity;

    alloc_info.descriptorPool = expanded_pool;
    descriptor_set = VK_NULL_HANDLE;
    VkResult alloc_result = dispatch.allocate_descriptor_sets(device, &alloc_info, &descriptor_set);
    if (alloc_result != VK_SUCCESS || descriptor_set == VK_NULL_HANDLE) {
        EXYNOS_LOGW(
            "Descriptor set allocation still failed after pool growth (VkResult=%d).",
            static_cast<int>(alloc_result));
        return false;
    }

    *out_pool = expanded_pool;
    *out_set = descriptor_set;
    g_descriptor_pool_growths.fetch_add(1);
    EXYNOS_LOGI("Descriptor pool grew dynamically to maxSets=%u.", next_capacity);
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
        !dispatch.create_sampler ||
        !dispatch.destroy_sampler ||
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

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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

    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_NEAREST;
    sampler_ci.minFilter = VK_FILTER_NEAREST;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.minLod = 0.0f;
    sampler_ci.maxLod = 0.0f;
    sampler_ci.maxAnisotropy = 1.0f;
    if (dispatch.create_sampler(device, &sampler_ci, nullptr, &runtime->copy_sampler) != VK_SUCCESS ||
        runtime->copy_sampler == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Failed to create copy sampler.");
        return false;
    }

    runtime->descriptor_pool_capacity = kDescriptorPoolInitialMaxSets;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end() && it_runtime->second.descriptor_buffer_supported) {
            runtime->descriptor_pool_capacity = std::max(
                runtime->descriptor_pool_capacity,
                kDescriptorPoolInitialMaxSetsHighEnd);
        }
    }
    runtime->descriptor_pools.clear();
    runtime->descriptor_pool = VK_NULL_HANDLE;
    if (!create_decode_descriptor_pool(
            device,
            dispatch,
            runtime->descriptor_pool_capacity,
            &runtime->descriptor_pool)) {
        EXYNOS_LOGW("Failed to create compute descriptor pool.");
        return false;
    }
    runtime->descriptor_pools.push_back(runtime->descriptor_pool);

    runtime->preferred_subgroup_size = 0;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end()) {
            const DeviceRuntime& device_runtime = it_runtime->second;
            if (device_runtime.subgroup_size_control) {
                bool has_wave32 =
                    (device_runtime.min_subgroup_size != 0) &&
                    (device_runtime.min_subgroup_size <= 32u) &&
                    (device_runtime.max_subgroup_size >= 32u);
                bool has_wave64 =
                    (device_runtime.min_subgroup_size != 0) &&
                    (device_runtime.min_subgroup_size <= 64u) &&
                    (device_runtime.max_subgroup_size >= 64u);
                if (has_wave32) {
                    runtime->preferred_subgroup_size = 32u;
                } else if (has_wave64) {
                    runtime->preferred_subgroup_size = 64u;
                }
            }
        }
    }

    bool ok_s3tc = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::S3tc);
    bool ok_rgtc = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Rgtc);
    bool ok_bc6 = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Bc6);
    bool ok_bc7 = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::Bc7);
    bool ok_copy_image = create_compute_pipeline_for_kind(device, dispatch, runtime, DecoderShaderKind::CopyImage);
    runtime->available = ok_s3tc || ok_rgtc || ok_bc6 || ok_bc7 || ok_copy_image;
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
    if (runtime->pipeline_copy_image != VK_NULL_HANDLE && dispatch.destroy_pipeline) {
        dispatch.destroy_pipeline(device, runtime->pipeline_copy_image, nullptr);
        runtime->pipeline_copy_image = VK_NULL_HANDLE;
    }
    if (dispatch.destroy_descriptor_pool) {
        std::lock_guard<std::mutex> descriptor_guard(runtime->descriptor_mutex);
        for (VkDescriptorPool pool : runtime->descriptor_pools) {
            if (pool != VK_NULL_HANDLE) {
                dispatch.destroy_descriptor_pool(device, pool, nullptr);
            }
        }
        runtime->descriptor_pools.clear();
        runtime->descriptor_pool = VK_NULL_HANDLE;
        runtime->descriptor_pool_capacity = 0;
    }
    if (runtime->pipeline_layout != VK_NULL_HANDLE && dispatch.destroy_pipeline_layout) {
        dispatch.destroy_pipeline_layout(device, runtime->pipeline_layout, nullptr);
        runtime->pipeline_layout = VK_NULL_HANDLE;
    }
    if (runtime->descriptor_set_layout != VK_NULL_HANDLE && dispatch.destroy_descriptor_set_layout) {
        dispatch.destroy_descriptor_set_layout(device, runtime->descriptor_set_layout, nullptr);
        runtime->descriptor_set_layout = VK_NULL_HANDLE;
    }
    if (runtime->copy_sampler != VK_NULL_HANDLE && dispatch.destroy_sampler) {
        dispatch.destroy_sampler(device, runtime->copy_sampler, nullptr);
        runtime->copy_sampler = VK_NULL_HANDLE;
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

struct CopyImagePushConstants {
    int32_t srcOffsetX;
    int32_t srcOffsetY;
    int32_t dstOffsetX;
    int32_t dstOffsetY;
    int32_t width;
    int32_t height;
    int32_t reserved0;
    int32_t reserved1;
};
static_assert(sizeof(CopyImagePushConstants) == kDecodePushConstantsSize, "CopyImagePushConstants layout mismatch.");

struct PreparedDecodeRegion {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImageView storage_view = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    StagingAllocation staging{};
    VkDeviceSize src_offset = 0;
    VkDeviceSize byte_size = 0;
    VkImageSubresourceRange subresource_range{};
    DecodePushConstants regs{};
    uint32_t groups_x = 0;
    uint32_t groups_y = 0;
};

struct PreparedSpecialCopyRegion {
    VkImageView src_view = VK_NULL_HANDLE;
    VkImageView dst_view = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkImageSubresourceRange src_subresource_range{};
    VkImageSubresourceRange dst_subresource_range{};
    CopyImagePushConstants regs{};
    uint32_t groups_x = 0;
    uint32_t groups_y = 0;
};

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

VkPipelineStageFlags stage_mask_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
        default:
            return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}

VkAccessFlags access_mask_for_layout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_PREINITIALIZED:
            return VK_ACCESS_HOST_WRITE_BIT;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            return 0;
    }
}

uint32_t clamp_vma_api_version(uint32_t api_version) {
    if (api_version == 0) {
        return VK_API_VERSION_1_0;
    }

    uint32_t variant = VK_API_VERSION_VARIANT(api_version);
    uint32_t major = VK_API_VERSION_MAJOR(api_version);
    uint32_t minor = VK_API_VERSION_MINOR(api_version);

    if (major == 0) {
        return VK_API_VERSION_1_0;
    }
    if (major > 1) {
        return VK_MAKE_API_VERSION(variant, 1, 3, 0);
    }

    if (minor > 3) {
        minor = 3;
    }
    return VK_MAKE_API_VERSION(variant, 1, minor, 0);
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

bool shader_kind_requires_unformatted_storage(DecoderShaderKind kind) {
    return kind == DecoderShaderKind::S3tc ||
           kind == DecoderShaderKind::Rgtc ||
           kind == DecoderShaderKind::Bc7;
}

bool build_decode_region_plan(
    const ComputeRuntime& runtime,
    VkFormat requested_format,
    const VkBufferImageCopy& region,
    uint32_t layer_index,
    PreparedDecodeRegion* out_prepared) {
    if (!out_prepared || !runtime.available) {
        return false;
    }

    VkPipeline pipeline = choose_decoder_pipeline(runtime, requested_format);
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }
    if (region.imageExtent.depth != 1) {
        return false;
    }

    uint32_t blocks_x = (std::max(region.bufferRowLength, region.imageExtent.width) + 3u) / 4u;
    uint32_t rows = region.bufferImageHeight ? region.bufferImageHeight : region.imageExtent.height;
    uint32_t blocks_y = (rows + 3u) / 4u;
    VkDeviceSize layer_stride = static_cast<VkDeviceSize>(blocks_x) *
                                static_cast<VkDeviceSize>(blocks_y) *
                                static_cast<VkDeviceSize>(block_size_bytes(requested_format));
    if (layer_stride == 0) {
        return false;
    }

    if (region.imageExtent.width > static_cast<uint32_t>(INT32_MAX) ||
        region.imageExtent.height > static_cast<uint32_t>(INT32_MAX) ||
        region.bufferRowLength > static_cast<uint32_t>(INT32_MAX) ||
        region.imageOffset.x < 0 ||
        region.imageOffset.y < 0) {
        EXYNOS_LOGW("Region exceeds push constant integer range.");
        return false;
    }

    PreparedDecodeRegion prepared{};
    prepared.pipeline = pipeline;
    prepared.src_offset = region.bufferOffset + static_cast<VkDeviceSize>(layer_index) * layer_stride;
    prepared.byte_size = layer_stride;
    prepared.subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.subresource_range.baseMipLevel = region.imageSubresource.mipLevel;
    prepared.subresource_range.levelCount = 1;
    prepared.subresource_range.baseArrayLayer = region.imageSubresource.baseArrayLayer + layer_index;
    prepared.subresource_range.layerCount = 1;
    prepared.regs.format = static_cast<int32_t>(requested_format);
    prepared.regs.width = static_cast<int32_t>(region.imageExtent.width);
    prepared.regs.height = static_cast<int32_t>(region.imageExtent.height);
    prepared.regs.offset = 0;
    prepared.regs.bufferRowLength = static_cast<int32_t>(region.bufferRowLength);
    prepared.regs.offsetX = region.imageOffset.x;
    prepared.regs.offsetY = region.imageOffset.y;
    prepared.regs.reserved0 = 0;
    prepared.groups_x = (region.imageExtent.width + 7u) / 8u;
    prepared.groups_y = (region.imageExtent.height + 7u) / 8u;

    *out_prepared = prepared;
    return true;
}

bool build_special_copy_region_plan(
    const VkImageCopy& region,
    uint32_t layer_index,
    PreparedSpecialCopyRegion* out_prepared) {
    if (!out_prepared) {
        return false;
    }
    if (region.extent.depth != 1 ||
        region.srcOffset.z != 0 ||
        region.dstOffset.z != 0 ||
        region.srcSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        region.dstSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        return false;
    }
    if (region.extent.width > static_cast<uint32_t>(INT32_MAX) ||
        region.extent.height > static_cast<uint32_t>(INT32_MAX) ||
        region.srcOffset.x < 0 ||
        region.srcOffset.y < 0 ||
        region.dstOffset.x < 0 ||
        region.dstOffset.y < 0) {
        EXYNOS_LOGW("CopyImage region exceeds push constant integer range.");
        return false;
    }

    PreparedSpecialCopyRegion prepared{};
    prepared.src_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.src_subresource_range.baseMipLevel = region.srcSubresource.mipLevel;
    prepared.src_subresource_range.levelCount = 1;
    prepared.src_subresource_range.baseArrayLayer = region.srcSubresource.baseArrayLayer + layer_index;
    prepared.src_subresource_range.layerCount = 1;
    prepared.dst_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prepared.dst_subresource_range.baseMipLevel = region.dstSubresource.mipLevel;
    prepared.dst_subresource_range.levelCount = 1;
    prepared.dst_subresource_range.baseArrayLayer = region.dstSubresource.baseArrayLayer + layer_index;
    prepared.dst_subresource_range.layerCount = 1;
    prepared.regs.srcOffsetX = region.srcOffset.x;
    prepared.regs.srcOffsetY = region.srcOffset.y;
    prepared.regs.dstOffsetX = region.dstOffset.x;
    prepared.regs.dstOffsetY = region.dstOffset.y;
    prepared.regs.width = static_cast<int32_t>(region.extent.width);
    prepared.regs.height = static_cast<int32_t>(region.extent.height);
    prepared.regs.reserved0 = 0;
    prepared.regs.reserved1 = 0;
    prepared.groups_x = (region.extent.width + 7u) / 8u;
    prepared.groups_y = (region.extent.height + 7u) / 8u;

    *out_prepared = prepared;
    return true;
}

void maybe_log_decode_stats() {
    uint64_t sample = g_decode_stats_log_gate.fetch_add(1) + 1;
    if ((sample % 256u) != 0u) {
        return;
    }
    EXYNOS_LOGI(
        "BCn stats: attempts=%llu success=%llu fail=%llu passthrough=%llu featureReject=%llu non2D=%llu blockedCopies=%llu retries=%llu virtualizedCreates=%llu nativeCreates=%llu poolGrows=%llu copyImageCalls=%llu copyImageVirtual=%llu copyImageHandled=%llu wave32Tries=%llu wave64Tries=%llu",
        static_cast<unsigned long long>(g_decode_attempts.load()),
        static_cast<unsigned long long>(g_decode_successes.load()),
        static_cast<unsigned long long>(g_decode_failures.load()),
        static_cast<unsigned long long>(g_decode_passthrough_activations.load()),
        static_cast<unsigned long long>(g_decode_feature_rejects.load()),
        static_cast<unsigned long long>(g_decode_non2d_rejects.load()),
        static_cast<unsigned long long>(g_decode_blocked_copies.load()),
        static_cast<unsigned long long>(g_decode_retry_attempts.load()),
        static_cast<unsigned long long>(g_virtualized_create_images.load()),
        static_cast<unsigned long long>(g_native_bcn_create_images.load()),
        static_cast<unsigned long long>(g_descriptor_pool_growths.load()),
        static_cast<unsigned long long>(g_copy_image_calls.load()),
        static_cast<unsigned long long>(g_copy_image_virtual_hits.load()),
        static_cast<unsigned long long>(g_copy_image_real_routes.load() + g_copy_image_special_routes.load()),
        static_cast<unsigned long long>(g_wave32_pipeline_tries.load()),
        static_cast<unsigned long long>(g_wave64_pipeline_tries.load()));
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

    uint32_t vma_api_version = VK_API_VERSION_1_0;
    if (instance_dispatch.get_physical_device_properties) {
        VkPhysicalDeviceProperties props{};
        instance_dispatch.get_physical_device_properties(physical_device, &props);
        vma_api_version = clamp_vma_api_version(props.apiVersion);
    }

    VmaAllocatorCreateInfo allocator_ci{};
    allocator_ci.physicalDevice = physical_device;
    allocator_ci.device = device;
    allocator_ci.instance = instance;
    allocator_ci.pVulkanFunctions = &runtime->vulkan_functions;
    allocator_ci.vulkanApiVersion = vma_api_version;

    VkResult vma_result = vmaCreateAllocator(&allocator_ci, &runtime->allocator);
    if (vma_result != VK_SUCCESS || runtime->allocator == VK_NULL_HANDLE) {
        runtime->allocator = VK_NULL_HANDLE;
        EXYNOS_LOGW("VMA init failed (VkResult=%d).", static_cast<int>(vma_result));
        return false;
    }

    EXYNOS_LOGI(
        "VMA allocator initialized for BCn staging with Vulkan API %u.%u.",
        VK_API_VERSION_MAJOR(vma_api_version),
        VK_API_VERSION_MINOR(vma_api_version));
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
    std::vector<TrackedDescriptorSet>* out_sets) {
    if (!out_sets) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    auto it = g_command_buffer_descriptor_sets.find(command_buffer_key);
    if (it == g_command_buffer_descriptor_sets.end()) {
        return;
    }
    auto& src = it->second;
    out_sets->insert(
        out_sets->end(),
        std::make_move_iterator(src.begin()),
        std::make_move_iterator(src.end()));
    g_command_buffer_descriptor_sets.erase(it);
}

void track_descriptor_set(
    VkCommandBuffer command_buffer,
    VkDescriptorPool descriptor_pool,
    VkDescriptorSet descriptor_set) {
    if (descriptor_pool == VK_NULL_HANDLE || descriptor_set == VK_NULL_HANDLE) {
        return;
    }
    std::lock_guard<std::mutex> guard(g_tracking_lock);
    g_command_buffer_descriptor_sets[dispatch_key(command_buffer)].push_back(
        TrackedDescriptorSet{descriptor_pool, descriptor_set});
}

void release_descriptor_sets(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    std::vector<TrackedDescriptorSet>* descriptor_sets) {
    if (!descriptor_sets || descriptor_sets->empty()) {
        return;
    }
    if (dispatch.free_descriptor_sets && runtime) {
        std::lock_guard<std::mutex> pool_guard(runtime->descriptor_mutex);
        std::unordered_map<void*, VkDescriptorPool> pools_by_key;
        std::unordered_map<void*, std::vector<VkDescriptorSet>> sets_by_pool;
        for (const TrackedDescriptorSet& tracked : *descriptor_sets) {
            if (tracked.pool == VK_NULL_HANDLE || tracked.set == VK_NULL_HANDLE) {
                continue;
            }
            void* key = dispatch_key(tracked.pool);
            pools_by_key[key] = tracked.pool;
            sets_by_pool[key].push_back(tracked.set);
        }
        for (auto& kv : sets_by_pool) {
            auto pool_it = pools_by_key.find(kv.first);
            if (pool_it == pools_by_key.end() || pool_it->second == VK_NULL_HANDLE || kv.second.empty()) {
                continue;
            }
            VkResult free_result = dispatch.free_descriptor_sets(
                device,
                pool_it->second,
                static_cast<uint32_t>(kv.second.size()),
                kv.second.data());
            if (free_result != VK_SUCCESS) {
                EXYNOS_LOGW(
                    "Descriptor set free failed (pool=%p, sets=%u, VkResult=%d).",
                    static_cast<void*>(pool_it->second),
                    static_cast<unsigned>(kv.second.size()),
                    static_cast<int>(free_result));
            }
        }
    }
    descriptor_sets->clear();
}

void release_command_buffer_resources(
    VkDevice device,
    VkCommandBuffer command_buffer,
    const DeviceDispatch& dispatch) {
    if (device == VK_NULL_HANDLE || command_buffer == VK_NULL_HANDLE) {
        return;
    }

    std::vector<StagingAllocation> staging_allocations_to_release;
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto vma_it = g_vma_runtime.find(dispatch_key(device));
        if (vma_it != g_vma_runtime.end() && vma_it->second) {
            allocator = vma_it->second->allocator;
        }
        auto compute_it = g_compute_runtime.find(dispatch_key(device));
        if (compute_it != g_compute_runtime.end() && compute_it->second) {
            compute_runtime = compute_it->second;
        }

        void* cb_key = dispatch_key(command_buffer);
        take_command_buffer_staging_allocations_locked(cb_key, &staging_allocations_to_release);
        take_command_buffer_descriptor_sets_locked(cb_key, &descriptor_sets_to_release);
    }

    release_staging_allocations(allocator, &staging_allocations_to_release);
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
}

bool create_staging_copy_for_region(
    VmaAllocator allocator,
    VkDeviceSize byte_size,
    StagingAllocation* out_staging) {
    if (!out_staging || !allocator) {
        return false;
    }
    if (byte_size == 0) {
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

    *out_staging = staging;
    return true;
}

void release_prepared_decode_regions(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VmaAllocator allocator,
    std::vector<PreparedDecodeRegion>* prepared_regions) {
    if (!prepared_regions) {
        return;
    }

    std::vector<StagingAllocation> staging_allocations;
    std::vector<TrackedDescriptorSet> descriptor_sets;
    staging_allocations.reserve(prepared_regions->size());
    descriptor_sets.reserve(prepared_regions->size());

    for (PreparedDecodeRegion& prepared : *prepared_regions) {
        if (prepared.staging.buffer != VK_NULL_HANDLE &&
            prepared.staging.allocation != VK_NULL_HANDLE) {
            staging_allocations.push_back(prepared.staging);
            prepared.staging = {};
        }
        if (prepared.descriptor_pool != VK_NULL_HANDLE &&
            prepared.descriptor_set != VK_NULL_HANDLE) {
            descriptor_sets.push_back(
                TrackedDescriptorSet{prepared.descriptor_pool, prepared.descriptor_set});
            prepared.descriptor_pool = VK_NULL_HANDLE;
            prepared.descriptor_set = VK_NULL_HANDLE;
        }
    }

    release_staging_allocations(allocator, &staging_allocations);
    release_descriptor_sets(device, dispatch, runtime, &descriptor_sets);
    prepared_regions->clear();
}

void release_prepared_special_copy_regions(
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    std::vector<PreparedSpecialCopyRegion>* prepared_regions) {
    if (!prepared_regions) {
        return;
    }

    std::vector<TrackedDescriptorSet> descriptor_sets;
    descriptor_sets.reserve(prepared_regions->size());
    for (PreparedSpecialCopyRegion& prepared : *prepared_regions) {
        if (prepared.descriptor_pool != VK_NULL_HANDLE &&
            prepared.descriptor_set != VK_NULL_HANDLE) {
            descriptor_sets.push_back(
                TrackedDescriptorSet{prepared.descriptor_pool, prepared.descriptor_set});
            prepared.descriptor_pool = VK_NULL_HANDLE;
            prepared.descriptor_set = VK_NULL_HANDLE;
        }
    }

    release_descriptor_sets(device, dispatch, runtime, &descriptor_sets);
    prepared_regions->clear();
}

void record_special_copy_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkImage src_image,
    VkImageLayout src_layout,
    VkImage dst_image,
    VkImageLayout dst_layout,
    PreparedSpecialCopyRegion* prepared) {
    if (!runtime || !runtime->available || !prepared) {
        return;
    }
    if (runtime->pipeline_copy_image == VK_NULL_HANDLE ||
        runtime->copy_sampler == VK_NULL_HANDLE ||
        prepared->src_view == VK_NULL_HANDLE ||
        prepared->dst_view == VK_NULL_HANDLE ||
        prepared->descriptor_pool == VK_NULL_HANDLE ||
        prepared->descriptor_set == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Prepared special image copy region is incomplete; skipping command recording.");
        return;
    }

    VkDescriptorImageInfo dst_image_info{};
    dst_image_info.imageView = prepared->dst_view;
    dst_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo src_image_info{};
    src_image_info.sampler = runtime->copy_sampler;
    src_image_info.imageView = prepared->src_view;
    src_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = prepared->descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &dst_image_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = prepared->descriptor_set;
    writes[1].dstBinding = 2;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &src_image_info;
    dispatch.update_descriptor_sets(device, 2, writes, 0, nullptr);

    VkImageMemoryBarrier src_to_sampled{};
    src_to_sampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_sampled.srcAccessMask = access_mask_for_layout(src_layout);
    src_to_sampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_to_sampled.oldLayout = src_layout;
    src_to_sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_sampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_sampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_sampled.image = src_image;
    src_to_sampled.subresourceRange = prepared->src_subresource_range;

    VkImageMemoryBarrier dst_to_general{};
    dst_to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_to_general.srcAccessMask = access_mask_for_layout(dst_layout);
    dst_to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dst_to_general.oldLayout = dst_layout;
    dst_to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_to_general.image = dst_image;
    dst_to_general.subresourceRange = prepared->dst_subresource_range;

    VkImageMemoryBarrier to_compute[2]{src_to_sampled, dst_to_general};
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        stage_mask_for_layout(src_layout) | stage_mask_for_layout(dst_layout),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        2, to_compute);

    dispatch.cmd_bind_pipeline(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        runtime->pipeline_copy_image);
    dispatch.cmd_bind_descriptor_sets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        runtime->pipeline_layout,
        0,
        1,
        &prepared->descriptor_set,
        0,
        nullptr);
    dispatch.cmd_push_constants(
        command_buffer,
        runtime->pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(CopyImagePushConstants),
        &prepared->regs);
    dispatch.cmd_dispatch(command_buffer, prepared->groups_x, prepared->groups_y, 1);

    VkImageMemoryBarrier src_restore{};
    src_restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_restore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    src_restore.dstAccessMask = access_mask_for_layout(src_layout);
    if (src_restore.dstAccessMask == 0) {
        src_restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    src_restore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_restore.newLayout = src_layout;
    src_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_restore.image = src_image;
    src_restore.subresourceRange = prepared->src_subresource_range;

    VkImageMemoryBarrier dst_restore{};
    dst_restore.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dst_restore.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dst_restore.dstAccessMask = access_mask_for_layout(dst_layout);
    if (dst_restore.dstAccessMask == 0) {
        dst_restore.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    dst_restore.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_restore.newLayout = dst_layout;
    dst_restore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_restore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dst_restore.image = dst_image;
    dst_restore.subresourceRange = prepared->dst_subresource_range;

    VkImageMemoryBarrier from_compute[2]{src_restore, dst_restore};
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        stage_mask_for_layout(src_layout) | stage_mask_for_layout(dst_layout),
        0,
        0, nullptr,
        0, nullptr,
        2, from_compute);

    track_descriptor_set(command_buffer, prepared->descriptor_pool, prepared->descriptor_set);
    prepared->descriptor_pool = VK_NULL_HANDLE;
    prepared->descriptor_set = VK_NULL_HANDLE;
}

void record_decode_region(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    ComputeRuntime* runtime,
    VkImage dst_image,
    VkImageLayout dst_layout,
    VkBuffer src_buffer,
    PreparedDecodeRegion* prepared) {
    if (!runtime || !runtime->available || !prepared) {
        return;
    }
    if (prepared->pipeline == VK_NULL_HANDLE ||
        prepared->storage_view == VK_NULL_HANDLE ||
        prepared->descriptor_pool == VK_NULL_HANDLE ||
        prepared->descriptor_set == VK_NULL_HANDLE ||
        prepared->staging.buffer == VK_NULL_HANDLE ||
        prepared->staging.allocation == VK_NULL_HANDLE ||
        src_buffer == VK_NULL_HANDLE) {
        EXYNOS_LOGW("Prepared decode region is incomplete; skipping command recording.");
        return;
    }

    VkDescriptorImageInfo image_info{};
    image_info.imageView = prepared->storage_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = prepared->staging.buffer;
    buffer_info.offset = 0;
    buffer_info.range = prepared->byte_size;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = prepared->descriptor_set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &image_info;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = prepared->descriptor_set;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &buffer_info;
    dispatch.update_descriptor_sets(device, 2, writes, 0, nullptr);

    VkBufferCopy copy_region{};
    copy_region.srcOffset = prepared->src_offset;
    copy_region.dstOffset = 0;
    copy_region.size = prepared->byte_size;
    dispatch.cmd_copy_buffer(command_buffer, src_buffer, prepared->staging.buffer, 1, &copy_region);

    VkBufferMemoryBarrier buffer_barrier{};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = prepared->staging.buffer;
    buffer_barrier.offset = 0;
    buffer_barrier.size = prepared->byte_size;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &buffer_barrier,
        0, nullptr);

    VkImageMemoryBarrier to_general{};
    to_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_general.srcAccessMask = access_mask_for_layout(dst_layout);
    to_general.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_general.oldLayout = dst_layout;
    to_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    to_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_general.image = dst_image;
    to_general.subresourceRange = prepared->subresource_range;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        stage_mask_for_layout(dst_layout),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &to_general);

    dispatch.cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, prepared->pipeline);
    dispatch.cmd_bind_descriptor_sets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        runtime->pipeline_layout,
        0,
        1,
        &prepared->descriptor_set,
        0,
        nullptr);
    dispatch.cmd_push_constants(
        command_buffer,
        runtime->pipeline_layout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(DecodePushConstants),
        &prepared->regs);

    dispatch.cmd_dispatch(command_buffer, prepared->groups_x, prepared->groups_y, 1);

    VkImageMemoryBarrier from_general{};
    from_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    from_general.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    from_general.dstAccessMask = access_mask_for_layout(dst_layout);
    if (from_general.dstAccessMask == 0) {
        from_general.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    }
    from_general.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    from_general.newLayout = dst_layout;
    from_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    from_general.image = dst_image;
    from_general.subresourceRange = prepared->subresource_range;
    dispatch.cmd_pipeline_barrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        stage_mask_for_layout(dst_layout),
        0,
        0, nullptr,
        0, nullptr,
        1, &from_general);

    track_staging_allocation(command_buffer, std::move(prepared->staging));
    prepared->staging = {};
    track_descriptor_set(command_buffer, prepared->descriptor_pool, prepared->descriptor_set);
    prepared->descriptor_pool = VK_NULL_HANDLE;
    prepared->descriptor_set = VK_NULL_HANDLE;
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
    if (device == VK_NULL_HANDLE || src_buffer == VK_NULL_HANDLE) {
        return false;
    }

    g_decode_attempts.fetch_add(1);
    void* image_key = dispatch_key(dst_image);
    VirtualImageInfo virtual_info{};
    bool is_virtual_image = false;
    bool shader_storage_write_without_format = false;
    DecodeImageState image_state{};
    bool has_image_state = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it = g_virtual_images.find(image_key);
        if (it != g_virtual_images.end()) {
            virtual_info = it->second;
            is_virtual_image = true;
        }
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end()) {
            shader_storage_write_without_format = it_runtime->second.shader_storage_image_write_without_format;
        }
        auto it_state = g_decode_image_state.find(image_key);
        if (it_state != g_decode_image_state.end()) {
            image_state = it_state->second;
            has_image_state = true;
        }
    }
    if (!is_virtual_image) {
        maybe_log_decode_stats();
        return false;
    }

    auto mark_image_blocked = [&]() {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto& state = g_decode_image_state[image_key];
        state.blocked_passthrough = true;
        state.blocked_copy_count = 0;
        state.failure_count += 1;
    };

    if (has_image_state && image_state.blocked_passthrough) {
        uint32_t next_block_count = image_state.blocked_copy_count + 1u;
        if (next_block_count < kDecodeBlockedRetryInterval) {
            {
                std::lock_guard<std::shared_mutex> guard(g_lock);
                auto& state = g_decode_image_state[image_key];
                state.blocked_passthrough = true;
                state.blocked_copy_count = next_block_count;
            }
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return false;
        }
        {
            std::lock_guard<std::shared_mutex> guard(g_lock);
            auto& state = g_decode_image_state[image_key];
            state.blocked_passthrough = false;
            state.blocked_copy_count = 0;
        }
        g_decode_retry_attempts.fetch_add(1);
        EXYNOS_LOGI(
            "Retrying BCn decode for blocked image %p after %u blocked copies.",
            static_cast<void*>(dst_image),
            kDecodeBlockedRetryInterval);
    }

    DecoderShaderKind shader_kind = shader_kind_for_format(virtual_info.requested_format);
    if (shader_kind_requires_unformatted_storage(shader_kind) &&
        !shader_storage_write_without_format) {
        EXYNOS_LOGW(
            "Decode disabled for image %p: shaderStorageImageWriteWithoutFormat unsupported for format=%d.",
            static_cast<void*>(dst_image),
            static_cast<int>(virtual_info.requested_format));
        mark_image_blocked();
        g_decode_feature_rejects.fetch_add(1);
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return false;
    }

    auto runtime = get_or_create_compute_runtime(dispatch_key(device));
    auto vma_runtime = get_or_create_vma_runtime(dispatch_key(device));
    {
        std::lock_guard<std::mutex> init_guard(runtime->init_mutex);
        if (!initialize_compute_runtime(device, dispatch, runtime.get())) {
            EXYNOS_LOGW("Compute decoder runtime unavailable for virtual BCn image. Marking image as blocked passthrough.");
            mark_image_blocked();
            g_decode_failures.fetch_add(1);
            g_decode_passthrough_activations.fetch_add(1);
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> init_guard(vma_runtime->init_mutex);
        if (!initialize_vma_runtime(dispatch_key(device), device, dispatch, vma_runtime.get())) {
            EXYNOS_LOGW("VMA runtime unavailable for BCn decode path. Marking image as blocked passthrough.");
            mark_image_blocked();
            g_decode_failures.fetch_add(1);
            g_decode_passthrough_activations.fetch_add(1);
            g_decode_blocked_copies.fetch_add(1);
            maybe_log_decode_stats();
            return false;
        }
    }
    if (vma_runtime->allocator == VK_NULL_HANDLE) {
        EXYNOS_LOGW("VMA allocator is null for BCn decode path. Marking image as blocked passthrough.");
        mark_image_blocked();
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return false;
    }

    if (dst_layout == VK_IMAGE_LAYOUT_UNDEFINED || dst_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        EXYNOS_LOGW("Unsupported dst layout for BCn decode path (%d).", static_cast<int>(dst_layout));
        mark_image_blocked();
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return false;
    }

    size_t planned_region_count = 0;
    for (uint32_t r = 0; r < region_count; ++r) {
        planned_region_count +=
            regions[r].imageSubresource.layerCount ? regions[r].imageSubresource.layerCount : 1u;
    }

    std::vector<PreparedDecodeRegion> prepared_regions;
    prepared_regions.reserve(planned_region_count);

    bool all_regions_prevalidated = true;
    for (uint32_t r = 0; r < region_count; ++r) {
        const VkBufferImageCopy& region = regions[r];
        if (region.imageExtent.depth != 1) {
            all_regions_prevalidated = false;
            g_decode_non2d_rejects.fetch_add(1);
            break;
        }

        uint32_t layer_count = region.imageSubresource.layerCount ? region.imageSubresource.layerCount : 1u;
        for (uint32_t layer = 0; layer < layer_count; ++layer) {
            PreparedDecodeRegion prepared{};
            if (!build_decode_region_plan(
                    *runtime,
                    virtual_info.requested_format,
                    region,
                    layer,
                    &prepared)) {
                all_regions_prevalidated = false;
                break;
            }
            prepared_regions.push_back(std::move(prepared));
        }
        if (!all_regions_prevalidated) {
            break;
        }
    }

    if (!all_regions_prevalidated) {
        EXYNOS_LOGW("BCn decode dispatch failed for virtual image. Marking image as blocked passthrough.");
        mark_image_blocked();
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return false;
    }

    bool all_resources_reserved = true;
    for (PreparedDecodeRegion& prepared : prepared_regions) {
        if (!create_staging_copy_for_region(
                vma_runtime->allocator,
                prepared.byte_size,
                &prepared.staging)) {
            all_resources_reserved = false;
            break;
        }
        if (!get_or_create_storage_view(
                device,
                dispatch,
                dst_image,
                prepared.subresource_range.baseMipLevel,
                prepared.subresource_range.baseArrayLayer,
                virtual_info.real_format,
                &prepared.storage_view)) {
            all_resources_reserved = false;
            break;
        }
        if (!allocate_decode_descriptor_set(
                device,
                dispatch,
                runtime.get(),
                &prepared.descriptor_pool,
                &prepared.descriptor_set)) {
            all_resources_reserved = false;
            break;
        }
    }

    if (!all_resources_reserved) {
        release_prepared_decode_regions(
            device,
            dispatch,
            runtime.get(),
            vma_runtime->allocator,
            &prepared_regions);
        EXYNOS_LOGW("BCn decode resource reservation failed for virtual image. Marking image as blocked passthrough.");
        mark_image_blocked();
        g_decode_failures.fetch_add(1);
        g_decode_passthrough_activations.fetch_add(1);
        g_decode_blocked_copies.fetch_add(1);
        maybe_log_decode_stats();
        return false;
    }

    for (PreparedDecodeRegion& prepared : prepared_regions) {
        record_decode_region(
            command_buffer,
            device,
            dispatch,
            runtime.get(),
            dst_image,
            dst_layout,
            src_buffer,
            &prepared);
    }
    release_prepared_decode_regions(
        device,
        dispatch,
        runtime.get(),
        vma_runtime->allocator,
        &prepared_regions);

    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto it_state = g_decode_image_state.find(image_key);
        if (it_state != g_decode_image_state.end()) {
            it_state->second.blocked_passthrough = false;
            it_state->second.blocked_copy_count = 0;
        }
    }
    g_decode_successes.fetch_add(1);
    maybe_log_decode_stats();
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
    dispatch.get_physical_device_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(
        next_gipa(*pInstance, "vkGetPhysicalDeviceFeatures"));
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
                for (auto cache_it = g_bcn_native_support_cache.begin(); cache_it != g_bcn_native_support_cache.end();) {
                    if (cache_it->first.physical == phys_it->first) {
                        cache_it = g_bcn_native_support_cache.erase(cache_it);
                    } else {
                        ++cache_it;
                    }
                }
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
    if (should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            format,
            type,
            tiling,
            usage,
            flags)) {
        VkFormat replacement = bcn_replacement_format(
            physicalDevice,
            dispatch,
            format,
            type,
            tiling,
            usage,
            flags);
        if (replacement != VK_FORMAT_UNDEFINED) {
            query_format = replacement;
        }
    }
    return dispatch.get_physical_device_image_format_properties(
        physicalDevice, query_format, type, tiling, usage, flags, pImageFormatProperties);
}

VkResult get_image_format_properties2_via_v1_fallback(
    VkPhysicalDevice physicalDevice,
    const InstanceDispatch& dispatch,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    if (!pImageFormatInfo || !pImageFormatProperties) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.get_physical_device_image_format_properties) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkImageFormatProperties legacy_props{};
    VkResult result = dispatch.get_physical_device_image_format_properties(
        physicalDevice,
        pImageFormatInfo->format,
        pImageFormatInfo->type,
        pImageFormatInfo->tiling,
        pImageFormatInfo->usage,
        pImageFormatInfo->flags,
        &legacy_props);
    if (result == VK_SUCCESS) {
        pImageFormatProperties->imageFormatProperties = legacy_props;
    }
    return result;
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

    VkPhysicalDeviceImageFormatInfo2 query_info = *pImageFormatInfo;
    if (should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            query_info.format,
            query_info.type,
            query_info.tiling,
            query_info.usage,
            query_info.flags)) {
        VkFormat replacement = bcn_replacement_format(
            physicalDevice,
            dispatch,
            query_info.format,
            query_info.type,
            query_info.tiling,
            query_info.usage,
            query_info.flags);
        if (replacement != VK_FORMAT_UNDEFINED) {
            query_info.format = replacement;
        }
    }
    if (dispatch.get_physical_device_image_format_properties2) {
        return dispatch.get_physical_device_image_format_properties2(
            physicalDevice,
            &query_info,
            pImageFormatProperties);
    }
    return get_image_format_properties2_via_v1_fallback(
        physicalDevice,
        dispatch,
        &query_info,
        pImageFormatProperties);
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
    if (should_virtualize_bcn_format(
            physicalDevice,
            dispatch,
            query_info.format,
            query_info.type,
            query_info.tiling,
            query_info.usage,
            query_info.flags)) {
        VkFormat replacement = bcn_replacement_format(
            physicalDevice,
            dispatch,
            query_info.format,
            query_info.type,
            query_info.tiling,
            query_info.usage,
            query_info.flags);
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
    return get_image_format_properties2_via_v1_fallback(
        physicalDevice,
        dispatch,
        &query_info,
        pImageFormatProperties);
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
    device_dispatch.create_sampler = reinterpret_cast<PFN_vkCreateSampler>(
        next_gdpa(*pDevice, "vkCreateSampler"));
    device_dispatch.destroy_sampler = reinterpret_cast<PFN_vkDestroySampler>(
        next_gdpa(*pDevice, "vkDestroySampler"));
    device_dispatch.create_command_pool = reinterpret_cast<PFN_vkCreateCommandPool>(
        next_gdpa(*pDevice, "vkCreateCommandPool"));
    device_dispatch.destroy_command_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(
        next_gdpa(*pDevice, "vkDestroyCommandPool"));
    device_dispatch.reset_command_pool = reinterpret_cast<PFN_vkResetCommandPool>(
        next_gdpa(*pDevice, "vkResetCommandPool"));
    device_dispatch.begin_command_buffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(
        next_gdpa(*pDevice, "vkBeginCommandBuffer"));
    device_dispatch.reset_command_buffer = reinterpret_cast<PFN_vkResetCommandBuffer>(
        next_gdpa(*pDevice, "vkResetCommandBuffer"));
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
    device_dispatch.cmd_copy_image = reinterpret_cast<PFN_vkCmdCopyImage>(
        next_gdpa(*pDevice, "vkCmdCopyImage"));
    device_dispatch.cmd_copy_image2 = reinterpret_cast<PFN_vkCmdCopyImage2>(
        next_gdpa(*pDevice, "vkCmdCopyImage2"));
    device_dispatch.cmd_copy_buffer_to_image = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage"));
    device_dispatch.cmd_copy_buffer_to_image2 = reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2"));
#ifdef VK_KHR_copy_commands2
    device_dispatch.cmd_copy_image2_khr = reinterpret_cast<PFN_vkCmdCopyImage2KHR>(
        next_gdpa(*pDevice, "vkCmdCopyImage2KHR"));
    device_dispatch.cmd_copy_buffer_to_image2_khr = reinterpret_cast<PFN_vkCmdCopyBufferToImage2KHR>(
        next_gdpa(*pDevice, "vkCmdCopyBufferToImage2KHR"));
#endif

    DeviceRuntime runtime{};
    if (instance != VK_NULL_HANDLE && instance_dispatch.get_instance_proc_addr) {
        runtime.descriptor_buffer_enabled = has_enabled_device_extension(
            pCreateInfo,
            "VK_EXT_descriptor_buffer");

        if (instance_dispatch.get_physical_device_properties) {
            VkPhysicalDeviceProperties props{};
            instance_dispatch.get_physical_device_properties(physicalDevice, &props);
            runtime.vendor_id = props.vendorID;
            runtime.is_xclipse = (props.vendorID == 0x144D) || (std::strstr(props.deviceName, "Xclipse") != nullptr);
        }
        if (instance_dispatch.get_physical_device_features) {
            VkPhysicalDeviceFeatures features{};
            instance_dispatch.get_physical_device_features(physicalDevice, &features);
            runtime.geometry_shader = (features.geometryShader == VK_TRUE);
            runtime.tessellation_shader = (features.tessellationShader == VK_TRUE);
            runtime.shader_storage_image_write_without_format =
                (features.shaderStorageImageWriteWithoutFormat == VK_TRUE);
        }

        auto get_physical_device_features2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceFeatures2"));
        if (get_physical_device_features2) {
            VkPhysicalDeviceFeatures2 features2{};
            features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
            VkPhysicalDeviceTransformFeedbackFeaturesEXT tf_features{};
            tf_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
            tf_features.pNext = features2.pNext;
            features2.pNext = &tf_features;
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES
            VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_size_features{};
            subgroup_size_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
            subgroup_size_features.pNext = features2.pNext;
            features2.pNext = &subgroup_size_features;
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
            VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features{};
            descriptor_buffer_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
            descriptor_buffer_features.pNext = features2.pNext;
            features2.pNext = &descriptor_buffer_features;
#endif

            get_physical_device_features2(physicalDevice, &features2);
            runtime.geometry_shader = (features2.features.geometryShader == VK_TRUE);
            runtime.tessellation_shader = (features2.features.tessellationShader == VK_TRUE);
            runtime.shader_storage_image_write_without_format =
                (features2.features.shaderStorageImageWriteWithoutFormat == VK_TRUE);

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT
            runtime.transform_feedback = (tf_features.transformFeedback == VK_TRUE);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES
            runtime.subgroup_size_control = (subgroup_size_features.subgroupSizeControl == VK_TRUE);
#endif

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT
            runtime.descriptor_buffer_supported =
                runtime.descriptor_buffer_enabled &&
                (descriptor_buffer_features.descriptorBuffer == VK_TRUE);
#endif
        }

        auto get_physical_device_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            instance_dispatch.get_instance_proc_addr(instance, "vkGetPhysicalDeviceProperties2"));
        if (get_physical_device_properties2) {
            VkPhysicalDeviceProperties2 properties2{};
            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES
            VkPhysicalDeviceSubgroupSizeControlProperties subgroup_size_props{};
            subgroup_size_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
            subgroup_size_props.pNext = properties2.pNext;
            properties2.pNext = &subgroup_size_props;
#endif

            get_physical_device_properties2(physicalDevice, &properties2);
            runtime.vendor_id = properties2.properties.vendorID;
            runtime.is_xclipse =
                (properties2.properties.vendorID == 0x144D) ||
                (std::strstr(properties2.properties.deviceName, "Xclipse") != nullptr);

#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES
            runtime.min_subgroup_size = subgroup_size_props.minSubgroupSize;
            runtime.max_subgroup_size = subgroup_size_props.maxSubgroupSize;
#endif
        }
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
            "Xclipse device detected (vendor=0x%04x, geom=%d, tess=%d, tfb=%d, storageWriteNoFormat=%d, subgroupCtrl=%d, subgroupRange=%u..%u, descriptorBuffer=%d/%d)",
            runtime.vendor_id,
            static_cast<int>(runtime.geometry_shader),
            static_cast<int>(runtime.tessellation_shader),
            static_cast<int>(runtime.transform_feedback),
            static_cast<int>(runtime.shader_storage_image_write_without_format),
            static_cast<int>(runtime.subgroup_size_control),
            runtime.min_subgroup_size,
            runtime.max_subgroup_size,
            static_cast<int>(runtime.descriptor_buffer_supported),
            static_cast<int>(runtime.descriptor_buffer_enabled));
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
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
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
                g_tracked_images.erase(image_it->first);
                g_decode_image_state.erase(image_it->first);
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
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
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
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        void* key = dispatch_key(device);
        auto it = g_device_dispatch.find(key);
        if (it == g_device_dispatch.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        dispatch = it->second;
        auto it_phys = g_device_to_physical_handle.find(key);
        if (it_phys != g_device_to_physical_handle.end()) {
            physical_device = it_phys->second;
        }
    }

    if (!dispatch.create_image) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (physical_device != VK_NULL_HANDLE && pCreateInfo && is_bcn_format(pCreateInfo->format)) {
        InstanceDispatch instance_dispatch{};
        if (get_instance_dispatch_for_physical(physical_device, &instance_dispatch, nullptr) &&
            should_virtualize_bcn_format(
                physical_device,
                instance_dispatch,
                pCreateInfo->format,
                pCreateInfo->imageType,
                pCreateInfo->tiling,
                pCreateInfo->usage,
                pCreateInfo->flags)) {
            VkFormat replacement = bcn_replacement_format(
                physical_device,
                instance_dispatch,
                pCreateInfo->format,
                pCreateInfo->imageType,
                pCreateInfo->tiling,
                pCreateInfo->usage,
                pCreateInfo->flags);
            if (replacement != VK_FORMAT_UNDEFINED) {
                VkImageCreateInfo patched_info = *pCreateInfo;
                VkImageFormatListCreateInfo patched_format_list{};
                std::vector<VkFormat> patched_view_formats;
                patched_info.format = replacement;
                // The decode path writes into the replacement image via storage image.
                patched_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                patched_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                patched_info.flags &= ~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                patch_virtualized_image_format_list(
                    physical_device,
                    instance_dispatch,
                    pCreateInfo,
                    &patched_info,
                    &patched_format_list,
                    &patched_view_formats);
                VkResult result = dispatch.create_image(device, &patched_info, pAllocator, pImage);
                if (result == VK_SUCCESS && pImage && *pImage != VK_NULL_HANDLE) {
                    std::lock_guard<std::shared_mutex> guard(g_lock);
                    auto image_key = dispatch_key(*pImage);
                    g_virtual_images[image_key] = VirtualImageInfo{pCreateInfo->format, replacement};
                    g_tracked_images[image_key] =
                        TrackedImageInfo{replacement, patched_info.imageType, patched_info.usage, patched_info.flags};
                    g_image_to_device[image_key] = dispatch_key(device);
                    g_decode_image_state.erase(image_key);
                }
                g_virtualized_create_images.fetch_add(1);
                EXYNOS_LOGI(
                    "Virtualized BCn image create (requested=%d, replacement=%d, usage=0x%x, flags=0x%x)",
                    static_cast<int>(pCreateInfo->format),
                    static_cast<int>(replacement),
                    patched_info.usage,
                    patched_info.flags);
                return result;
            }
        } else {
            g_native_bcn_create_images.fetch_add(1);
        }
    }

    VkResult result = dispatch.create_image(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS && pImage && *pImage != VK_NULL_HANDLE) {
        std::lock_guard<std::shared_mutex> guard(g_lock);
        auto image_key = dispatch_key(*pImage);
        g_tracked_images[image_key] =
            TrackedImageInfo{pCreateInfo->format, pCreateInfo->imageType, pCreateInfo->usage, pCreateInfo->flags};
        g_image_to_device[image_key] = dispatch_key(device);
        g_decode_image_state.erase(image_key);
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
        g_tracked_images.erase(image_key);
        g_decode_image_state.erase(image_key);
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

bool get_dispatch_for_command_buffer(
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device);

bool resolve_dispatch_for_command_buffer(
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device);

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
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::shared_ptr<ComputeRuntime> compute_runtime;
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
            compute_runtime = compute_it->second;
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
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
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
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::shared_ptr<ComputeRuntime> compute_runtime;
    {
        std::lock_guard<std::shared_mutex> guard(g_lock);
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
            compute_runtime = compute_it->second;
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
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    if (!dispatch.reset_command_pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return dispatch.reset_command_pool(device, commandPool, flags);
}

VKAPI_ATTR VkResult VKAPI_CALL layer_BeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!get_dispatch_for_command_buffer(commandBuffer, "vkBeginCommandBuffer", &dispatch, &device)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.begin_command_buffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.begin_command_buffer(commandBuffer, pBeginInfo);
    if (result == VK_SUCCESS) {
        release_command_buffer_resources(device, commandBuffer, dispatch);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL layer_ResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!get_dispatch_for_command_buffer(commandBuffer, "vkResetCommandBuffer", &dispatch, &device)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!dispatch.reset_command_buffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = dispatch.reset_command_buffer(commandBuffer, flags);
    if (result == VK_SUCCESS) {
        release_command_buffer_resources(device, commandBuffer, dispatch);
    }
    return result;
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
    std::vector<TrackedDescriptorSet> descriptor_sets_to_release;
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::shared_ptr<ComputeRuntime> compute_runtime;
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
            compute_runtime = compute_it->second;
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
    release_descriptor_sets(device, dispatch, compute_runtime.get(), &descriptor_sets_to_release);
    if (dispatch.free_command_buffers) {
        dispatch.free_command_buffers(device, commandPool, commandBufferCount, pCommandBuffers);
    }
}

bool get_dispatch_for_command_buffer(
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device) {
    if (!out_dispatch || !out_device) {
        return false;
    }
    *out_device = VK_NULL_HANDLE;

    std::shared_lock<std::shared_mutex> guard(g_lock);
    auto it_cb = g_command_buffer_to_device.find(dispatch_key(command_buffer));
    if (it_cb == g_command_buffer_to_device.end()) {
        if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
            EXYNOS_LOGW("Command buffer mapping missing for %s.", api_name);
        }
        return false;
    }

    auto it_dev = g_device_dispatch.find(it_cb->second);
    if (it_dev == g_device_dispatch.end()) {
        if (!g_warned_missing_cmd_buffer_map.exchange(true)) {
            EXYNOS_LOGW("Device mapping missing for %s.", api_name);
        }
        return false;
    }
    *out_dispatch = it_dev->second;

    auto it_dev_handle = g_command_buffer_device_handle.find(dispatch_key(command_buffer));
    if (it_dev_handle != g_command_buffer_device_handle.end()) {
        *out_device = it_dev_handle->second;
    }
    return true;
}

bool get_any_device_dispatch(DeviceDispatch* out_dispatch) {
    if (!out_dispatch) {
        return false;
    }
    std::shared_lock<std::shared_mutex> guard(g_lock);
    if (g_device_dispatch.size() != 1) {
        return false;
    }
    *out_dispatch = g_device_dispatch.begin()->second;
    return true;
}

bool resolve_dispatch_for_command_buffer(
    VkCommandBuffer command_buffer,
    const char* api_name,
    DeviceDispatch* out_dispatch,
    VkDevice* out_device) {
    if (get_dispatch_for_command_buffer(command_buffer, api_name, out_dispatch, out_device)) {
        return true;
    }
    if (!get_any_device_dispatch(out_dispatch)) {
        return false;
    }
    if (out_device) {
        *out_device = VK_NULL_HANDLE;
    }
    if (!g_warned_cmd_buffer_dispatch_fallback.exchange(true)) {
        EXYNOS_LOGW(
            "Falling back to single-device dispatch for %s without command buffer mapping.",
            api_name);
    }
    return true;
}

struct CopyImageRouteInfo {
    bool involves_virtual = false;
    bool can_copy_real_images = false;
    bool needs_special_path = false;
    bool can_use_special_path = false;
    VkFormat src_actual_format = VK_FORMAT_UNDEFINED;
    VkFormat dst_actual_format = VK_FORMAT_UNDEFINED;
    VkImageType src_type = VK_IMAGE_TYPE_2D;
    VkImageType dst_type = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags src_usage = 0;
    VkImageUsageFlags dst_usage = 0;
};

CopyImageRouteInfo classify_copy_image_route(VkImage src_image, VkImage dst_image) {
    CopyImageRouteInfo route{};

    std::shared_lock<std::shared_mutex> guard(g_lock);
    void* src_key = dispatch_key(src_image);
    void* dst_key = dispatch_key(dst_image);

    auto src_virtual_it = g_virtual_images.find(src_key);
    auto dst_virtual_it = g_virtual_images.find(dst_key);
    route.involves_virtual =
        (src_virtual_it != g_virtual_images.end()) || (dst_virtual_it != g_virtual_images.end());

    auto src_info_it = g_tracked_images.find(src_key);
    if (src_info_it != g_tracked_images.end()) {
        route.src_actual_format = src_info_it->second.format;
        route.src_type = src_info_it->second.type;
        route.src_usage = src_info_it->second.usage;
    } else if (src_virtual_it != g_virtual_images.end()) {
        route.src_actual_format = src_virtual_it->second.real_format;
    }

    auto dst_info_it = g_tracked_images.find(dst_key);
    if (dst_info_it != g_tracked_images.end()) {
        route.dst_actual_format = dst_info_it->second.format;
        route.dst_type = dst_info_it->second.type;
        route.dst_usage = dst_info_it->second.usage;
    } else if (dst_virtual_it != g_virtual_images.end()) {
        route.dst_actual_format = dst_virtual_it->second.real_format;
    }

    if (!route.involves_virtual) {
        route.can_copy_real_images = true;
        return route;
    }

    if (route.src_actual_format != VK_FORMAT_UNDEFINED &&
        route.dst_actual_format != VK_FORMAT_UNDEFINED &&
        route.src_actual_format == route.dst_actual_format) {
        route.can_copy_real_images = true;
        return route;
    }

    route.needs_special_path = true;
    route.can_use_special_path =
        route.src_actual_format != VK_FORMAT_UNDEFINED &&
        route.dst_actual_format != VK_FORMAT_UNDEFINED &&
        route.src_type == VK_IMAGE_TYPE_2D &&
        route.dst_type == VK_IMAGE_TYPE_2D &&
        (route.src_usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0 &&
        (route.dst_usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    return route;
}

void note_copy_image_route(const char* api_name, const CopyImageRouteInfo& route) {
    g_copy_image_calls.fetch_add(1);
    if (!route.involves_virtual) {
        maybe_log_decode_stats();
        return;
    }

    g_copy_image_virtual_hits.fetch_add(1);
    if (route.can_copy_real_images) {
        g_copy_image_real_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }

    uint64_t fallback_count = g_copy_image_special_fallbacks.fetch_add(1) + 1;
    if (fallback_count <= 4u || (fallback_count % 64u) == 0u) {
        EXYNOS_LOGW(
            "%s hit a virtual image copy with mismatched actual formats (src=%d dst=%d). "
            "No translated special path exists yet, so the layer is falling back to the native copy.",
            api_name,
            static_cast<int>(route.src_actual_format),
            static_cast<int>(route.dst_actual_format));
    }
    maybe_log_decode_stats();
}

bool is_virtual_image(VkImage image) {
    std::shared_lock<std::shared_mutex> guard(g_lock);
    return g_virtual_images.find(dispatch_key(image)) != g_virtual_images.end();
}

bool try_special_copy_image_regions(
    VkCommandBuffer command_buffer,
    VkDevice device,
    const DeviceDispatch& dispatch,
    VkImage src_image,
    VkImageLayout src_layout,
    VkImage dst_image,
    VkImageLayout dst_layout,
    const CopyImageRouteInfo& route,
    uint32_t region_count,
    const VkImageCopy* regions) {
    if (!regions || region_count == 0 || device == VK_NULL_HANDLE) {
        return false;
    }
    if (!route.needs_special_path || !route.can_use_special_path) {
        return false;
    }
    if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        src_layout == VK_IMAGE_LAYOUT_PREINITIALIZED ||
        dst_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        dst_layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        return false;
    }

    bool shader_storage_write_without_format = false;
    {
        std::shared_lock<std::shared_mutex> guard(g_lock);
        auto it_runtime = g_device_runtime.find(dispatch_key(device));
        if (it_runtime != g_device_runtime.end()) {
            shader_storage_write_without_format =
                it_runtime->second.shader_storage_image_write_without_format;
        }
    }
    if (!shader_storage_write_without_format) {
        return false;
    }

    auto runtime = get_or_create_compute_runtime(dispatch_key(device));
    {
        std::lock_guard<std::mutex> init_guard(runtime->init_mutex);
        if (!initialize_compute_runtime(device, dispatch, runtime.get())) {
            return false;
        }
    }
    if (!runtime->available ||
        runtime->pipeline_copy_image == VK_NULL_HANDLE ||
        runtime->copy_sampler == VK_NULL_HANDLE) {
        return false;
    }

    size_t planned_region_count = 0;
    for (uint32_t r = 0; r < region_count; ++r) {
        planned_region_count +=
            regions[r].srcSubresource.layerCount ? regions[r].srcSubresource.layerCount : 1u;
    }

    std::vector<PreparedSpecialCopyRegion> prepared_regions;
    prepared_regions.reserve(planned_region_count);
    for (uint32_t r = 0; r < region_count; ++r) {
        const VkImageCopy& region = regions[r];
        uint32_t src_layers = region.srcSubresource.layerCount ? region.srcSubresource.layerCount : 1u;
        uint32_t dst_layers = region.dstSubresource.layerCount ? region.dstSubresource.layerCount : 1u;
        if (src_layers != dst_layers) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return false;
        }
        for (uint32_t layer = 0; layer < src_layers; ++layer) {
            PreparedSpecialCopyRegion prepared{};
            if (!build_special_copy_region_plan(region, layer, &prepared)) {
                release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
                return false;
            }
            prepared_regions.push_back(std::move(prepared));
        }
    }

    for (PreparedSpecialCopyRegion& prepared : prepared_regions) {
        if (!get_or_create_storage_view(
                device,
                dispatch,
                src_image,
                prepared.src_subresource_range.baseMipLevel,
                prepared.src_subresource_range.baseArrayLayer,
                route.src_actual_format,
                &prepared.src_view)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return false;
        }
        if (!get_or_create_storage_view(
                device,
                dispatch,
                dst_image,
                prepared.dst_subresource_range.baseMipLevel,
                prepared.dst_subresource_range.baseArrayLayer,
                route.dst_actual_format,
                &prepared.dst_view)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return false;
        }
        if (!allocate_decode_descriptor_set(
                device,
                dispatch,
                runtime.get(),
                &prepared.descriptor_pool,
                &prepared.descriptor_set)) {
            release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
            return false;
        }
    }

    for (PreparedSpecialCopyRegion& prepared : prepared_regions) {
        record_special_copy_region(
            command_buffer,
            device,
            dispatch,
            runtime.get(),
            src_image,
            src_layout,
            dst_image,
            dst_layout,
            &prepared);
    }
    release_prepared_special_copy_regions(device, dispatch, runtime.get(), &prepared_regions);
    return true;
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyImage", &dispatch, &device)) {
        return;
    }
    (void)device;

    if (!dispatch.cmd_copy_image) {
        return;
    }
    CopyImageRouteInfo route = classify_copy_image_route(srcImage, dstImage);
    if (try_special_copy_image_regions(
            commandBuffer,
            device,
            dispatch,
            srcImage,
            srcImageLayout,
            dstImage,
            dstImageLayout,
            route,
            regionCount,
            pRegions)) {
        g_copy_image_calls.fetch_add(1);
        if (route.involves_virtual) {
            g_copy_image_virtual_hits.fetch_add(1);
        }
        g_copy_image_special_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }
    note_copy_image_route("vkCmdCopyImage", route);
    dispatch.cmd_copy_image(
        commandBuffer,
        srcImage,
        srcImageLayout,
        dstImage,
        dstImageLayout,
        regionCount,
        pRegions);
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2* pCopyImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyImage2", &dispatch, &device)) {
        return;
    }
    (void)device;
    if (!dispatch.cmd_copy_image2 || !pCopyImageInfo) {
        return;
    }

    CopyImageRouteInfo route =
        classify_copy_image_route(pCopyImageInfo->srcImage, pCopyImageInfo->dstImage);
    std::vector<VkImageCopy> regions;
    regions.reserve(pCopyImageInfo->regionCount);
    for (uint32_t i = 0; i < pCopyImageInfo->regionCount; ++i) {
        const VkImageCopy2& src_region = pCopyImageInfo->pRegions[i];
        VkImageCopy region{};
        region.srcSubresource = src_region.srcSubresource;
        region.srcOffset = src_region.srcOffset;
        region.dstSubresource = src_region.dstSubresource;
        region.dstOffset = src_region.dstOffset;
        region.extent = src_region.extent;
        regions.push_back(region);
    }
    if (try_special_copy_image_regions(
            commandBuffer,
            device,
            dispatch,
            pCopyImageInfo->srcImage,
            pCopyImageInfo->srcImageLayout,
            pCopyImageInfo->dstImage,
            pCopyImageInfo->dstImageLayout,
            route,
            static_cast<uint32_t>(regions.size()),
            regions.data())) {
        g_copy_image_calls.fetch_add(1);
        if (route.involves_virtual) {
            g_copy_image_virtual_hits.fetch_add(1);
        }
        g_copy_image_special_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }
    note_copy_image_route("vkCmdCopyImage2", route);
    dispatch.cmd_copy_image2(commandBuffer, pCopyImageInfo);
}

#ifdef VK_KHR_copy_commands2
VKAPI_ATTR void VKAPI_CALL layer_CmdCopyImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2KHR* pCopyImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyImage2KHR", &dispatch, &device)) {
        return;
    }
    (void)device;
    if (!dispatch.cmd_copy_image2_khr || !pCopyImageInfo) {
        return;
    }

    CopyImageRouteInfo route =
        classify_copy_image_route(pCopyImageInfo->srcImage, pCopyImageInfo->dstImage);
    std::vector<VkImageCopy> regions;
    regions.reserve(pCopyImageInfo->regionCount);
    for (uint32_t i = 0; i < pCopyImageInfo->regionCount; ++i) {
        const VkImageCopy2KHR& src_region = pCopyImageInfo->pRegions[i];
        VkImageCopy region{};
        region.srcSubresource = src_region.srcSubresource;
        region.srcOffset = src_region.srcOffset;
        region.dstSubresource = src_region.dstSubresource;
        region.dstOffset = src_region.dstOffset;
        region.extent = src_region.extent;
        regions.push_back(region);
    }
    if (try_special_copy_image_regions(
            commandBuffer,
            device,
            dispatch,
            pCopyImageInfo->srcImage,
            pCopyImageInfo->srcImageLayout,
            pCopyImageInfo->dstImage,
            pCopyImageInfo->dstImageLayout,
            route,
            static_cast<uint32_t>(regions.size()),
            regions.data())) {
        g_copy_image_calls.fetch_add(1);
        if (route.involves_virtual) {
            g_copy_image_virtual_hits.fetch_add(1);
        }
        g_copy_image_special_routes.fetch_add(1);
        maybe_log_decode_stats();
        return;
    }
    note_copy_image_route("vkCmdCopyImage2KHR", route);
    dispatch.cmd_copy_image2_khr(commandBuffer, pCopyImageInfo);
}
#endif

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyBufferToImage", &dispatch, &device)) {
        return;
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
                pRegions) &&
            !is_virtual_image(dstImage)) {
            dispatch.cmd_copy_buffer_to_image(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
        }
    }
}

VKAPI_ATTR void VKAPI_CALL layer_CmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    DeviceDispatch dispatch{};
    VkDevice device = VK_NULL_HANDLE;
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyBufferToImage2", &dispatch, &device)) {
        return;
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
                regions.data()) &&
            !is_virtual_image(pCopyBufferToImageInfo->dstImage)) {
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
    if (!resolve_dispatch_for_command_buffer(commandBuffer, "vkCmdCopyBufferToImage2KHR", &dispatch, &device)) {
        return;
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
                regions.data()) &&
            !is_virtual_image(pCopyBufferToImageInfo->dstImage)) {
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

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    return enumerate_layer_props(pPropertyCount, pProperties);
}

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
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

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
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

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
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

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
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
    if (std::strcmp(pName, "vkBeginCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_BeginCommandBuffer);
    if (std::strcmp(pName, "vkResetCommandBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_ResetCommandBuffer);
    if (std::strcmp(pName, "vkAllocateCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_AllocateCommandBuffers);
    if (std::strcmp(pName, "vkFreeCommandBuffers") == 0) return reinterpret_cast<PFN_vkVoidFunction>(layer_FreeCommandBuffers);
    if (std::strcmp(pName, "vkCmdCopyImage") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage);
    }
    if (std::strcmp(pName, "vkCmdCopyImage2") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage2);
    }
#ifdef VK_KHR_copy_commands2
    if (std::strcmp(pName, "vkCmdCopyImage2KHR") == 0 && should_intercept_copy_path()) {
        return reinterpret_cast<PFN_vkVoidFunction>(layer_CmdCopyImage2KHR);
    }
#endif
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

extern "C" EXYNOS_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
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
