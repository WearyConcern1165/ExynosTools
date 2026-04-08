#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V1.6.0 — Dynamic Configuration System
// Reads exynostools_config.ini at driver init time.
// ═══════════════════════════════════════════════════════════════════════════

typedef struct ExynosToolsConfig {
    // --- GPU Tuning ---
    uint32_t workgroup_size;       // 0 = auto-detect, 32/64/128 = forced
    int      force_wave32;         // 1 = force Wave32 on all GPUs

    // --- BCn Decoder ---
    int      enable_bcn_decode;    // 1 = enabled (default), 0 = passthrough
    int      enable_mipmap;        // 1 = multi-mip decode, 0 = base level only
    int      staging_pool_count;   // Number of staging buffers (default 4)
    uint32_t staging_buffer_mb;    // Size of each staging buffer in MB (default 16)

    // --- Pipeline Caching ---
    int      enable_pipeline_cache; // 1 = save/load pipeline cache to disk
    char     cache_path[256];       // Path for pipeline cache file

    // --- Watchdog (Anti-Crash) ---
    int      enable_watchdog;       // 1 = intercept VK_ERROR_DEVICE_LOST
    uint32_t watchdog_timeout_ms;   // Fence timeout in ms (default 5000)

    // --- Memory Manager (Micro-VMA) ---
    int      enable_vma;            // 1 = use internal memory allocator
    uint32_t vma_block_size_mb;     // Block size in MB (default 128)

    // --- Debug ---
    int      verbose_logging;       // 1 = extra logcat output
} ExynosToolsConfig;

/// Initialize config with sane defaults.
void exynos_config_defaults(ExynosToolsConfig* cfg);

/// Load config from an INI file. Returns 0 on success, -1 if file not found.
int exynos_config_load(ExynosToolsConfig* cfg, const char* ini_path);

/// Try multiple known paths to find the config file.
/// Searches: /sdcard/ExynosTools/, /data/local/tmp/, emulator Z:\ paths.
int exynos_config_auto_load(ExynosToolsConfig* cfg);

#ifdef __cplusplus
}
#endif
