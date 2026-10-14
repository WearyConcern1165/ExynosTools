#include "layer_settings_runtime.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>

#include "layer_global_state.h"
#include "layer_logging.h"
#include "layer_settings_utils.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace {

void apply_bool_setting_by_name(const std::string& section,
                                const std::string& key,
                                bool value,
                                LayerSettingsSnapshot* io_settings) {
    if (!io_settings) {
        return;
    }
    if (section == "layer") {
        if (key == "enabled") {
            io_settings->enabled = value;
        } else if (key == "xclipse_only") {
            io_settings->xclipse_only = value;
        } else if (key == "bcn_intercept") {
            io_settings->bcn_intercept = value;
        } else if (key == "force_bcn_emulation") {
            io_settings->force_bcn_emulation = value;
        } else if (key == "cpu_decode_primary") {
            io_settings->cpu_decode_primary = value;
        }
    } else if (section == "safety") {
        if (key == "strict_dispatch") {
            io_settings->strict_dispatch = value;
        } else if (key == "drop_on_missing_commandbuffer_map") {
            io_settings->drop_on_missing_commandbuffer_map = value;
        } else if (key == "block_incompatible_virtual_copies") {
            io_settings->block_incompatible_virtual_copies = value;
        }
    } else if (section == "telemetry") {
        if (key == "microbenchmark_enabled") {
            io_settings->microbenchmark_enabled = value;
        }
    }
}

bool parse_uint32_setting(const std::string& raw_value, uint32_t* out_value) {
    if (!out_value) {
        return false;
    }
    try {
        size_t parsed_chars = 0;
        unsigned long value = std::stoul(raw_value, &parsed_chars, 0);
        if (parsed_chars == 0 || value > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        *out_value = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

int parse_depth_override_mode_string(const std::string& raw_value) {
    std::string value = lower_copy(trim_copy(raw_value));
    if (value == "none" || value == "off" || value == "false" || value == "0" ||
        value == "disabled" || value == "disable") {
        return DEPTH_OVERRIDE_NONE;
    }
    if (value == "block" || value == "block_depth_creation") {
        return DEPTH_OVERRIDE_DISABLED;
    }
    if (value == "safe" || value == "2") {
        return DEPTH_OVERRIDE_SAFE;
    }
    if (value == "aggressive" || value == "agressive" || value == "3") {
        return DEPTH_OVERRIDE_AGGRESSIVE;
    }
    return DEPTH_OVERRIDE_NONE;
}

bool apply_ini_setting_line(const std::string& section,
                            const std::string& key,
                            const std::string& raw_value,
                            LayerSettingsSnapshot* io_settings) {
    if (!io_settings) {
        return false;
    }

    if (section == "layer" && key == "log_level") {
        io_settings->log_level = parse_log_level_string(raw_value);
        return true;
    }
    if (section == "layer" && key == "disabled_bcn_mask") {
        uint32_t value = 0;
        if (!parse_uint32_setting(raw_value, &value)) {
            return false;
        }
        io_settings->disabled_bcn_mask = value;
        return true;
    }
    if (section == "layer" && key == "cpu_fallback_max_upload_mb") {
        uint32_t value = 0;
        if (!parse_uint32_setting(raw_value, &value)) {
            return false;
        }
        io_settings->cpu_fallback_max_upload_mb = std::clamp<uint32_t>(
            value,
            16u,
            512u);
        return true;
    }
    if (section == "layer" && key == "depth_override") {
        io_settings->depth_override_mode = parse_depth_override_mode_string(raw_value);
        return true;
    }
    if (section == "layer" && key == "depth_override_mode") {
        io_settings->depth_override_mode = parse_depth_override_mode_string(raw_value);
        return true;
    }

    bool bool_value = false;
    if (!parse_bool_string(raw_value, &bool_value)) {
        return false;
    }
    apply_bool_setting_by_name(section, key, bool_value, io_settings);
    return true;
}

bool apply_layer_setting_from_ext(const VkLayerSettingEXT& setting, LayerSettingsSnapshot* io_settings) {
    if (!io_settings || setting.valueCount == 0 || !setting.pValues || !setting.pSettingName) {
        return false;
    }
    std::string key = lower_copy(setting.pSettingName);
    auto apply_bool = [&](bool value) {
        if (key == "enabled") {
            io_settings->enabled = value;
        } else if (key == "xclipse_only") {
            io_settings->xclipse_only = value;
        } else if (key == "bcn_intercept") {
            io_settings->bcn_intercept = value;
        } else if (key == "force_bcn_emulation") {
            io_settings->force_bcn_emulation = value;
        } else if (key == "strict_dispatch") {
            io_settings->strict_dispatch = value;
        } else if (key == "drop_on_missing_commandbuffer_map") {
            io_settings->drop_on_missing_commandbuffer_map = value;
        } else if (key == "block_incompatible_virtual_copies") {
            io_settings->block_incompatible_virtual_copies = value;
        } else if (key == "microbenchmark_enabled") {
            io_settings->microbenchmark_enabled = value;
        } else if (key == "cpu_decode_primary") {
            io_settings->cpu_decode_primary = value;
        }
    };
    auto apply_uint = [&](uint32_t value) {
        if (key == "disabled_bcn_mask") {
            io_settings->disabled_bcn_mask = value;
        } else if (key == "cpu_fallback_max_upload_mb") {
            io_settings->cpu_fallback_max_upload_mb = std::clamp<uint32_t>(value, 16u, 512u);
        } else if (key == "depth_override" || key == "depth_override_mode") {
            io_settings->depth_override_mode = std::clamp<int>(static_cast<int>(value), 0, 3);
        }
    };

    switch (setting.type) {
        case VK_LAYER_SETTING_TYPE_BOOL32_EXT:
            apply_bool(reinterpret_cast<const VkBool32*>(setting.pValues)[0] != VK_FALSE);
            return true;
        case VK_LAYER_SETTING_TYPE_INT32_EXT:
            if (key == "log_level") {
                io_settings->log_level = static_cast<int>(std::clamp<int32_t>(
                    reinterpret_cast<const int32_t*>(setting.pValues)[0],
                    0,
                    2));
            } else if (key == "disabled_bcn_mask" || key == "cpu_fallback_max_upload_mb" ||
                       key == "depth_override" || key == "depth_override_mode") {
                int32_t value = reinterpret_cast<const int32_t*>(setting.pValues)[0];
                apply_uint(value < 0 ? 0u : static_cast<uint32_t>(value));
            } else {
                apply_bool(reinterpret_cast<const int32_t*>(setting.pValues)[0] != 0);
            }
            return true;
        case VK_LAYER_SETTING_TYPE_UINT32_EXT:
            if (key == "log_level") {
                io_settings->log_level = std::clamp(
                    static_cast<int>(reinterpret_cast<const uint32_t*>(setting.pValues)[0]),
                    0,
                    2);
            } else if (key == "disabled_bcn_mask" || key == "cpu_fallback_max_upload_mb" ||
                       key == "depth_override" || key == "depth_override_mode") {
                apply_uint(reinterpret_cast<const uint32_t*>(setting.pValues)[0]);
            } else {
                apply_bool(reinterpret_cast<const uint32_t*>(setting.pValues)[0] != 0u);
            }
            return true;
        case VK_LAYER_SETTING_TYPE_STRING_EXT: {
            const auto* values = reinterpret_cast<const char* const*>(setting.pValues);
            if (!values || !values[0]) {
                return false;
            }
            if (key == "log_level") {
                io_settings->log_level = parse_log_level_string(values[0]);
                return true;
            }
            if (key == "depth_override" || key == "depth_override_mode") {
                io_settings->depth_override_mode = parse_depth_override_mode_string(values[0]);
                return true;
            }
            if (key == "disabled_bcn_mask" || key == "cpu_fallback_max_upload_mb") {
                uint32_t value = 0;
                if (!parse_uint32_setting(values[0], &value)) {
                    return false;
                }
                apply_uint(value);
                return true;
            }
            bool bool_value = false;
            if (!parse_bool_string(values[0], &bool_value)) {
                return false;
            }
            apply_bool(bool_value);
            return true;
        }
        default:
            return false;
    }
}

std::string dirname_copy(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return std::string();
    }
    return path.substr(0, slash);
}

std::string join_path_copy(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    char last = lhs.back();
    if (last == '/' || last == '\\') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

}  // namespace

bool load_layer_settings_from_ini(const std::string& path, LayerSettingsSnapshot* io_settings) {
    if (!io_settings || path.empty()) {
        return false;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        size_t comment_pos = line.find_first_of("#;");
        if (comment_pos != std::string::npos) {
            line.erase(comment_pos);
        }
        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower_copy(trim_copy(line.substr(1, line.size() - 2)));
            continue;
        }

        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }
        std::string key = lower_copy(trim_copy(line.substr(0, equals_pos)));
        std::string value = trim_copy(line.substr(equals_pos + 1));
        apply_ini_setting_line(section, key, value, io_settings);
    }

    EXYNOS_LOGI("Loaded layer settings from %s", path.c_str());
    return true;
}

void apply_layer_settings_from_create_info(const VkInstanceCreateInfo* pCreateInfo, LayerSettingsSnapshot* io_settings) {
    if (!pCreateInfo || !io_settings) {
        return;
    }

#ifdef VK_EXT_layer_settings
    auto* current = reinterpret_cast<const VkBaseInStructure*>(pCreateInfo->pNext);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT) {
            auto* settings_info = reinterpret_cast<const VkLayerSettingsCreateInfoEXT*>(current);
            for (uint32_t i = 0; i < settings_info->settingCount; ++i) {
                const VkLayerSettingEXT& setting = settings_info->pSettings[i];
                if (setting.pLayerName && iequals(setting.pLayerName, "VK_LAYER_VORTEK_XCLIPSE")) {
                    apply_layer_setting_from_ext(setting, io_settings);
                }
            }
        }
        current = current->pNext;
    }
#else
    (void)pCreateInfo;
    (void)io_settings;
#endif
}

void maybe_load_layer_settings_ini(LayerSettingsSnapshot* io_settings) {
    // Modern Vortek Xclipse packages intentionally avoid sidecar .ini files.
    // Runtime overrides can still use VkLayerSettingsCreateInfoEXT.
    (void)io_settings;
}

LayerSettingsSnapshot snapshot_layer_settings() {
    LayerSettingsSnapshot snapshot;
    const LayerSettingsState& settings = layer_settings_state();
    snapshot.enabled = settings.enabled.load(std::memory_order_relaxed);
    snapshot.xclipse_only = settings.xclipse_only.load(std::memory_order_relaxed);
    snapshot.bcn_intercept = settings.bcn_intercept.load(std::memory_order_relaxed);
    snapshot.force_bcn_emulation = settings.force_bcn_emulation.load(std::memory_order_relaxed);
    snapshot.strict_dispatch = settings.strict_dispatch.load(std::memory_order_relaxed);
    snapshot.drop_on_missing_commandbuffer_map =
        settings.drop_on_missing_commandbuffer_map.load(std::memory_order_relaxed);
    snapshot.block_incompatible_virtual_copies =
        settings.block_incompatible_virtual_copies.load(std::memory_order_relaxed);
    snapshot.microbenchmark_enabled = settings.microbenchmark_enabled.load(std::memory_order_relaxed);
    snapshot.cpu_decode_primary = settings.cpu_decode_primary.load(std::memory_order_relaxed);
    snapshot.depth_override_mode = settings.depth_override_mode.load(std::memory_order_relaxed);
    snapshot.disabled_bcn_mask = settings.disabled_bcn_mask.load(std::memory_order_relaxed);
    snapshot.cpu_fallback_max_upload_mb = settings.cpu_fallback_max_upload_mb.load(std::memory_order_relaxed);
    snapshot.log_level = exynos_layer_log_level_storage().load(std::memory_order_relaxed);
    return snapshot;
}

void commit_layer_settings(const LayerSettingsSnapshot& settings) {
    LayerSettingsState& state = layer_settings_state();
    state.enabled.store(settings.enabled, std::memory_order_relaxed);
    state.xclipse_only.store(settings.xclipse_only, std::memory_order_relaxed);
    state.bcn_intercept.store(settings.bcn_intercept, std::memory_order_relaxed);
    state.force_bcn_emulation.store(settings.force_bcn_emulation, std::memory_order_relaxed);
    state.strict_dispatch.store(settings.strict_dispatch, std::memory_order_relaxed);
    state.drop_on_missing_commandbuffer_map.store(
        settings.drop_on_missing_commandbuffer_map,
        std::memory_order_relaxed);
    state.block_incompatible_virtual_copies.store(
        settings.block_incompatible_virtual_copies,
        std::memory_order_relaxed);
    state.microbenchmark_enabled.store(settings.microbenchmark_enabled, std::memory_order_relaxed);
    state.cpu_decode_primary.store(settings.cpu_decode_primary, std::memory_order_relaxed);
    state.depth_override_mode.store(settings.depth_override_mode, std::memory_order_relaxed);
    state.disabled_bcn_mask.store(settings.disabled_bcn_mask, std::memory_order_relaxed);
    state.cpu_fallback_max_upload_mb.store(settings.cpu_fallback_max_upload_mb, std::memory_order_relaxed);
    exynos_layer_log_level_storage().store(settings.log_level, std::memory_order_relaxed);

    g_warned_missing_cmd_buffer_map.store(false, std::memory_order_relaxed);
    g_warned_cmd_buffer_dispatch_fallback.store(false, std::memory_order_relaxed);
    g_warned_cmd_buffer_dispatch_drop.store(false, std::memory_order_relaxed);

    std::lock_guard<std::shared_mutex> guard(g_lock);
    for (auto& entry : g_physical_runtime) {
        entry.second.virtual_bc_feature_cached = false;
    }
}

void refresh_layer_settings(const VkInstanceCreateInfo* pCreateInfo) {
    LayerSettingsSnapshot settings;
    maybe_load_layer_settings_ini(&settings);
    apply_layer_settings_from_create_info(pCreateInfo, &settings);
    commit_layer_settings(settings);
    EXYNOS_LOGI(
        "Settings: enabled=%d xclipse_only=%d bcn_intercept=%d force_bcn_emulation=%d strict_dispatch=%d drop_on_missing_cb_map=%d block_incompatible_virtual_copies=%d microbenchmark=%d cpu_decode_primary=%d depth_override=%d disabled_bcn_mask=0x%x cpu_fallback_max_upload_mb=%u log_level=%d",
        settings.enabled ? 1 : 0,
        settings.xclipse_only ? 1 : 0,
        settings.bcn_intercept ? 1 : 0,
        settings.force_bcn_emulation ? 1 : 0,
        settings.strict_dispatch ? 1 : 0,
        settings.drop_on_missing_commandbuffer_map ? 1 : 0,
        settings.block_incompatible_virtual_copies ? 1 : 0,
        settings.microbenchmark_enabled ? 1 : 0,
        settings.cpu_decode_primary ? 1 : 0,
        settings.depth_override_mode,
        settings.disabled_bcn_mask,
        settings.cpu_fallback_max_upload_mb,
        settings.log_level);
}

VirtualizationPolicySettings snapshot_virtualization_policy_settings() {
    LayerSettingsSnapshot settings = snapshot_layer_settings();
    VirtualizationPolicySettings policy{};
    policy.enabled = settings.enabled;
    policy.xclipse_only = settings.xclipse_only;
    policy.bcn_intercept = settings.bcn_intercept;
    policy.force_bcn_emulation = settings.force_bcn_emulation;
    policy.disabled_bcn_mask = settings.disabled_bcn_mask;
    return policy;
}
