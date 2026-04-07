#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools — Multi-Profile Xclipse Extension Database
//
// Supports:
//   • Xclipse 920 (Exynos 2200, RDNA 2)
//   • Xclipse 940 (Exynos 2400, RDNA 3)
//   • Xclipse 950 (Exynos 2500, RDNA 3.5)
//
// We declare extensions ourselves so Android's linker namespace can't block us.
// ═══════════════════════════════════════════════════════════════════════════

#include <vulkan/vulkan.h>
#include <cstring>

namespace bcn {

// ─── GPU Profile Enum ───────────────────────────────────────────────────
enum class XclipseProfile {
    Unknown = 0,
    Xclipse920_RDNA2,     // Exynos 2200
    Xclipse940_RDNA3,     // Exynos 2400
    Xclipse950_RDNA35,    // Exynos 2500
};

// ─── Helper ─────────────────────────────────────────────────────────────
static inline VkExtensionProperties makeExt(const char* name, uint32_t ver) {
    VkExtensionProperties p = {};
    strncpy(p.extensionName, name, VK_MAX_EXTENSION_NAME_SIZE - 1);
    p.specVersion = ver;
    return p;
}

// ═══════════════════════════════════════════════════════════════════════
// Instance Extensions (shared across all Xclipse profiles)
// ═══════════════════════════════════════════════════════════════════════
static const VkExtensionProperties XCLIPSE_INSTANCE_EXTENSIONS[] = {
    makeExt("VK_KHR_surface", 25),
    makeExt("VK_KHR_android_surface", 6),
    makeExt("VK_KHR_display", 23),
    makeExt("VK_KHR_get_physical_device_properties2", 2),
    makeExt("VK_KHR_get_surface_capabilities2", 1),
    makeExt("VK_KHR_external_fence_capabilities", 1),
    makeExt("VK_KHR_external_memory_capabilities", 1),
    makeExt("VK_KHR_external_semaphore_capabilities", 1),
    makeExt("VK_KHR_device_group_creation", 1),
    makeExt("VK_KHR_get_display_properties2", 1),
    makeExt("VK_KHR_surface_protected_capabilities", 1),
    makeExt("VK_EXT_debug_report", 10),
    makeExt("VK_EXT_debug_utils", 2),
    makeExt("VK_EXT_swapchain_colorspace", 4),
};
static const uint32_t XCLIPSE_INSTANCE_EXT_COUNT =
    sizeof(XCLIPSE_INSTANCE_EXTENSIONS) / sizeof(XCLIPSE_INSTANCE_EXTENSIONS[0]);

// ═══════════════════════════════════════════════════════════════════════
// Device Extensions — BASE set (shared by ALL Xclipse GPUs)
// ═══════════════════════════════════════════════════════════════════════
static const VkExtensionProperties XCLIPSE_DEVICE_EXT_BASE[] = {
    // --- KHR (Khronos Standard) ---
    makeExt("VK_KHR_swapchain", 70),
    makeExt("VK_KHR_multiview", 1),
    makeExt("VK_KHR_maintenance1", 2),
    makeExt("VK_KHR_maintenance2", 1),
    makeExt("VK_KHR_maintenance3", 1),
    makeExt("VK_KHR_maintenance4", 2),
    makeExt("VK_KHR_shader_draw_parameters", 1),
    makeExt("VK_KHR_push_descriptor", 2),
    makeExt("VK_KHR_descriptor_update_template", 1),
    makeExt("VK_KHR_create_renderpass2", 1),
    makeExt("VK_KHR_depth_stencil_resolve", 1),
    makeExt("VK_KHR_draw_indirect_count", 1),
    makeExt("VK_KHR_timeline_semaphore", 2),
    makeExt("VK_KHR_buffer_device_address", 1),
    makeExt("VK_KHR_deferred_host_operations", 4),
    makeExt("VK_KHR_spirv_1_4", 1),
    makeExt("VK_KHR_shader_float_controls", 4),
    makeExt("VK_KHR_shader_float16_int8", 1),
    makeExt("VK_KHR_shader_atomic_int64", 1),
    makeExt("VK_KHR_shader_subgroup_extended_types", 1),
    makeExt("VK_KHR_shader_terminate_invocation", 1),
    makeExt("VK_KHR_shader_integer_dot_product", 1),
    makeExt("VK_KHR_shader_non_semantic_info", 1),
    makeExt("VK_KHR_8bit_storage", 1),
    makeExt("VK_KHR_16bit_storage", 1),
    makeExt("VK_KHR_storage_buffer_storage_class", 1),
    makeExt("VK_KHR_relaxed_block_layout", 1),
    makeExt("VK_KHR_bind_memory2", 1),
    makeExt("VK_KHR_get_memory_requirements2", 1),
    makeExt("VK_KHR_image_format_list", 1),
    makeExt("VK_KHR_imageless_framebuffer", 1),
    makeExt("VK_KHR_sampler_mirror_clamp_to_edge", 3),
    makeExt("VK_KHR_sampler_ycbcr_conversion", 14),
    makeExt("VK_KHR_separate_depth_stencil_layouts", 1),
    makeExt("VK_KHR_uniform_buffer_standard_layout", 1),
    makeExt("VK_KHR_variable_pointers", 1),
    makeExt("VK_KHR_vulkan_memory_model", 3),
    makeExt("VK_KHR_zero_initialize_workgroup_memory", 1),
    makeExt("VK_KHR_dynamic_rendering", 1),
    makeExt("VK_KHR_synchronization2", 1),
    makeExt("VK_KHR_copy_commands2", 1),
    makeExt("VK_KHR_format_feature_flags2", 2),
    makeExt("VK_KHR_shader_clock", 1),
    makeExt("VK_KHR_driver_properties", 1),
    makeExt("VK_KHR_dedicated_allocation", 3),
    makeExt("VK_KHR_external_memory", 1),
    makeExt("VK_KHR_external_memory_fd", 1),
    makeExt("VK_KHR_external_semaphore", 1),
    makeExt("VK_KHR_external_semaphore_fd", 1),
    makeExt("VK_KHR_external_fence", 1),
    makeExt("VK_KHR_external_fence_fd", 1),
    makeExt("VK_KHR_incremental_present", 2),
    makeExt("VK_KHR_shared_presentable_image", 1),
    makeExt("VK_KHR_device_group", 4),
    makeExt("VK_KHR_performance_query", 1),
    makeExt("VK_KHR_pipeline_executable_properties", 1),

    // --- EXT (Vendor-Neutral) ---
    makeExt("VK_EXT_scalar_block_layout", 1),
    makeExt("VK_EXT_host_query_reset", 1),
    makeExt("VK_EXT_shader_demote_to_helper_invocation", 1),
    makeExt("VK_EXT_subgroup_size_control", 2),
    makeExt("VK_EXT_extended_dynamic_state", 1),
    makeExt("VK_EXT_extended_dynamic_state2", 1),
    makeExt("VK_EXT_private_data", 1),
    makeExt("VK_EXT_pipeline_creation_cache_control", 3),
    makeExt("VK_EXT_image_robustness", 1),
    makeExt("VK_EXT_inline_uniform_block", 1),
    makeExt("VK_EXT_texture_compression_astc_hdr", 1),
    makeExt("VK_EXT_texel_buffer_alignment", 1),
    makeExt("VK_EXT_tooling_info", 1),
    makeExt("VK_EXT_ycbcr_image_arrays", 1),
    makeExt("VK_EXT_custom_border_color", 12),
    makeExt("VK_EXT_robustness2", 1),
    makeExt("VK_EXT_line_rasterization", 1),
    makeExt("VK_EXT_vertex_attribute_divisor", 3),
    makeExt("VK_EXT_vertex_input_dynamic_state", 2),
    makeExt("VK_EXT_color_write_enable", 1),
    makeExt("VK_EXT_conditional_rendering", 2),
    makeExt("VK_EXT_transform_feedback", 1),
    makeExt("VK_EXT_depth_clip_enable", 1),
    makeExt("VK_EXT_depth_clip_control", 1),
    makeExt("VK_EXT_memory_budget", 1),
    makeExt("VK_EXT_memory_priority", 1),
    makeExt("VK_EXT_index_type_uint8", 1),
    makeExt("VK_EXT_primitive_topology_list_restart", 1),
    makeExt("VK_EXT_primitives_generated_query", 1),
    makeExt("VK_EXT_shader_viewport_index_layer", 1),
    makeExt("VK_EXT_multi_draw", 1),
    makeExt("VK_EXT_load_store_op_none", 1),
    makeExt("VK_EXT_image_view_min_lod", 1),
    makeExt("VK_EXT_4444_formats", 1),
    makeExt("VK_EXT_shader_stencil_export", 1),
    makeExt("VK_EXT_queue_family_foreign", 1),
    makeExt("VK_EXT_global_priority", 2),
    makeExt("VK_EXT_global_priority_query", 1),
    makeExt("VK_EXT_provoking_vertex", 1),
    makeExt("VK_EXT_descriptor_indexing", 2),
    makeExt("VK_EXT_sampler_filter_minmax", 2),
    makeExt("VK_EXT_shader_subgroup_ballot", 1),
    makeExt("VK_EXT_shader_subgroup_vote", 1),
    makeExt("VK_EXT_post_depth_coverage", 1),
    makeExt("VK_EXT_sample_locations", 1),
    makeExt("VK_EXT_blend_operation_advanced", 2),
    makeExt("VK_EXT_image_drm_format_modifier", 2),
    makeExt("VK_EXT_physical_device_drm", 1),
    makeExt("VK_EXT_fragment_shader_interlock", 1),
    makeExt("VK_EXT_non_seamless_cube_map", 1),
    makeExt("VK_EXT_border_color_swizzle", 1),
    makeExt("VK_EXT_attachment_feedback_loop_layout", 2),
    makeExt("VK_EXT_pipeline_creation_feedback", 1),
    makeExt("VK_EXT_shader_module_identifier", 1),
    makeExt("VK_EXT_rasterization_order_attachment_access", 1),
    makeExt("VK_EXT_dynamic_rendering_unused_attachments", 1),
    makeExt("VK_EXT_depth_bias_control", 1),
    makeExt("VK_EXT_calibrated_timestamps", 2),

    // --- ANDROID ---
    makeExt("VK_ANDROID_external_memory_android_hardware_buffer", 5),
    makeExt("VK_ANDROID_native_buffer", 8),

    // --- Google ---
    makeExt("VK_GOOGLE_display_timing", 1),
    makeExt("VK_GOOGLE_decorate_string", 1),
    makeExt("VK_GOOGLE_hlsl_functionality1", 1),

    // --- IMG (PowerVR compat) ---
    makeExt("VK_IMG_format_pvrtc", 1),
};
static const uint32_t XCLIPSE_DEVICE_EXT_BASE_COUNT =
    sizeof(XCLIPSE_DEVICE_EXT_BASE) / sizeof(XCLIPSE_DEVICE_EXT_BASE[0]);

// ═══════════════════════════════════════════════════════════════════════
// RDNA 3.5 EXCLUSIVE extensions (Xclipse 950 / Exynos 2500 only)
// ═══════════════════════════════════════════════════════════════════════
static const VkExtensionProperties XCLIPSE_950_EXTRA_EXTENSIONS[] = {
    // --- Ray Tracing (hardware accelerated on RDNA 3.5) ---
    makeExt("VK_KHR_ray_query", 1),
    makeExt("VK_KHR_ray_tracing_pipeline", 1),
    makeExt("VK_KHR_acceleration_structure", 13),
    makeExt("VK_KHR_ray_tracing_maintenance1", 1),
    makeExt("VK_KHR_pipeline_library", 1),

    // --- Mesh Shaders (native RDNA 3+ support) ---
    makeExt("VK_EXT_mesh_shader", 1),

    // --- New RDNA 3.5 features ---
    makeExt("VK_KHR_maintenance5", 1),
    makeExt("VK_KHR_maintenance6", 1),
    makeExt("VK_KHR_fragment_shader_barycentric", 2),
    makeExt("VK_EXT_extended_dynamic_state3", 2),
    makeExt("VK_EXT_graphics_pipeline_library", 1),
    makeExt("VK_EXT_shader_object", 1),
    makeExt("VK_EXT_descriptor_buffer", 1),
    makeExt("VK_EXT_depth_clamp_zero_one", 1),
    makeExt("VK_EXT_image_compression_control", 1),
    makeExt("VK_EXT_nested_command_buffer", 1),
};
static const uint32_t XCLIPSE_950_EXTRA_COUNT =
    sizeof(XCLIPSE_950_EXTRA_EXTENSIONS) / sizeof(XCLIPSE_950_EXTRA_EXTENSIONS[0]);

// ═══════════════════════════════════════════════════════════════════════
// Backward compat aliases (used by older code paths)
// ═══════════════════════════════════════════════════════════════════════
static const VkExtensionProperties* XCLIPSE_DEVICE_EXTENSIONS = XCLIPSE_DEVICE_EXT_BASE;
static const uint32_t XCLIPSE_DEVICE_EXT_COUNT = XCLIPSE_DEVICE_EXT_BASE_COUNT;

// ═══════════════════════════════════════════════════════════════════════
// GPU Detection Utilities
// ═══════════════════════════════════════════════════════════════════════

/// Detect which Xclipse profile the physical device corresponds to.
/// Uses vendorID (Samsung = 0x144d) and deviceName heuristics.
static inline XclipseProfile detectXclipseProfile(
    uint32_t vendorID, uint32_t deviceID, const char* deviceName)
{
    // Samsung vendorID
    const uint32_t SAMSUNG_VENDOR_ID = 0x144d;

    // Also accept AMD vendorID since Xclipse uses RDNA
    const uint32_t AMD_VENDOR_ID = 0x1002;

    if (vendorID != SAMSUNG_VENDOR_ID && vendorID != AMD_VENDOR_ID)
        return XclipseProfile::Unknown;

    // Try to identify by name
    if (deviceName) {
        // Check for known Xclipse GPU names
        if (strstr(deviceName, "950") || strstr(deviceName, "RDNA 3.5") ||
            strstr(deviceName, "gfx1150") || strstr(deviceName, "gfx1151"))
            return XclipseProfile::Xclipse950_RDNA35;

        if (strstr(deviceName, "940") || strstr(deviceName, "RDNA 3") ||
            strstr(deviceName, "gfx1100") || strstr(deviceName, "gfx1103"))
            return XclipseProfile::Xclipse940_RDNA3;

        if (strstr(deviceName, "920") || strstr(deviceName, "RDNA 2") ||
            strstr(deviceName, "gfx1030") || strstr(deviceName, "gfx1035"))
            return XclipseProfile::Xclipse920_RDNA2;

        // Generic Samsung/Xclipse detection
        if (strstr(deviceName, "Xclipse") || strstr(deviceName, "xclipse") ||
            strstr(deviceName, "Samsung") || strstr(deviceName, "Exynos"))
            return XclipseProfile::Xclipse940_RDNA3; // Default to most common
    }

    // If Samsung vendor but unknown name, default to RDNA 3
    if (vendorID == SAMSUNG_VENDOR_ID)
        return XclipseProfile::Xclipse940_RDNA3;

    return XclipseProfile::Unknown;
}

/// Get the optimal compute workgroup size for BCn decode shaders.
/// RDNA 3.5 has 8 WGPs so can use larger workgroups.
static inline uint32_t getOptimalWorkgroupSize(XclipseProfile profile) {
    switch (profile) {
        case XclipseProfile::Xclipse950_RDNA35:  return 128; // 16x8 (RDNA 3.5 native dual-issue wide)
        case XclipseProfile::Xclipse940_RDNA3:   return 64;  // 8x8  (RDNA 3 standard dual-issue)
        case XclipseProfile::Xclipse920_RDNA2:   return 32;  // 8x4  (RDNA 2 native Wave32 to prevent sync errors)
        default:                                  return 64;
    }
}

/// Returns true if this GPU generation has hardware BCn support
/// that the driver MIGHT expose. Must still be verified at runtime
/// via vkGetPhysicalDeviceFormatProperties.
static inline bool hasHardwareBcnPotential(XclipseProfile profile) {
    // RDNA 3.5 silicon supports BCn in hardware
    // RDNA 3 silicon also supports it but Samsung driver may not expose it
    // RDNA 2 does NOT support BCn in hardware
    switch (profile) {
        case XclipseProfile::Xclipse950_RDNA35:  return true;
        case XclipseProfile::Xclipse940_RDNA3:   return true;  // maybe
        case XclipseProfile::Xclipse920_RDNA2:   return false;
        default:                                  return false;
    }
}

/// Get the profile name as a human-readable string.
static inline const char* profileName(XclipseProfile profile) {
    switch (profile) {
        case XclipseProfile::Xclipse950_RDNA35:  return "Xclipse 950 (RDNA 3.5)";
        case XclipseProfile::Xclipse940_RDNA3:   return "Xclipse 940 (RDNA 3)";
        case XclipseProfile::Xclipse920_RDNA2:   return "Xclipse 920 (RDNA 2)";
        default:                                  return "Unknown Xclipse";
    }
}

} // namespace bcn
