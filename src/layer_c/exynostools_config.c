#include "exynostools_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <android/log.h>

#define CFG_TAG "ExynosToolsConfig"
#define CFG_LOGI(...) __android_log_print(ANDROID_LOG_INFO, CFG_TAG, __VA_ARGS__)
#define CFG_LOGW(...) __android_log_print(ANDROID_LOG_WARN, CFG_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Defaults
// ═══════════════════════════════════════════════════════════════════════════

void exynos_config_defaults(ExynosToolsConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->workgroup_size       = 0;    // 0 = auto-detect from GPU profile
    cfg->force_wave32         = 0;
    cfg->enable_bcn_decode    = 1;    // Full C++ decoder engine is now embedded in the .so
    cfg->enable_mipmap        = 1;
    cfg->staging_pool_count   = 4;
    cfg->staging_buffer_mb    = 16;
    cfg->enable_pipeline_cache = 1;
    strncpy(cfg->cache_path, "/data/local/tmp/exynostools_pipeline.bin", sizeof(cfg->cache_path) - 1);
    cfg->enable_watchdog      = 1;
    cfg->watchdog_timeout_ms  = 5000;
    cfg->enable_vma           = 0;    // Disabled by default (experimental)
    cfg->vma_block_size_mb    = 128;
    cfg->verbose_logging      = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// INI Parser (lightweight, pure C, no dependencies)
// ═══════════════════════════════════════════════════════════════════════════

static char* trim_whitespace(char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
    return str;
}

static void parse_ini_line(ExynosToolsConfig* cfg, const char* key, const char* value) {
    // --- GPU Tuning ---
    if (strcmp(key, "workgroup_size") == 0)        { cfg->workgroup_size = (uint32_t)atoi(value); return; }
    if (strcmp(key, "force_wave32") == 0)           { cfg->force_wave32 = atoi(value); return; }

    // --- BCn Decoder ---
    if (strcmp(key, "enable_bcn_decode") == 0)      { cfg->enable_bcn_decode = atoi(value); return; }
    if (strcmp(key, "enable_mipmap") == 0)          { cfg->enable_mipmap = atoi(value); return; }
    if (strcmp(key, "staging_pool_count") == 0)     { cfg->staging_pool_count = atoi(value); return; }
    if (strcmp(key, "staging_buffer_mb") == 0)      { cfg->staging_buffer_mb = (uint32_t)atoi(value); return; }

    // --- Pipeline Caching ---
    if (strcmp(key, "enable_pipeline_cache") == 0)  { cfg->enable_pipeline_cache = atoi(value); return; }
    if (strcmp(key, "cache_path") == 0)             { strncpy(cfg->cache_path, value, sizeof(cfg->cache_path) - 1); return; }

    // --- Watchdog ---
    if (strcmp(key, "enable_watchdog") == 0)        { cfg->enable_watchdog = atoi(value); return; }
    if (strcmp(key, "watchdog_timeout_ms") == 0)    { cfg->watchdog_timeout_ms = (uint32_t)atoi(value); return; }

    // --- VMA ---
    if (strcmp(key, "enable_vma") == 0)             { cfg->enable_vma = atoi(value); return; }
    if (strcmp(key, "vma_block_size_mb") == 0)      { cfg->vma_block_size_mb = (uint32_t)atoi(value); return; }

    // --- Debug ---
    if (strcmp(key, "verbose_logging") == 0)        { cfg->verbose_logging = atoi(value); return; }

    CFG_LOGW("Unknown config key: %s", key);
}

int exynos_config_load(ExynosToolsConfig* cfg, const char* ini_path) {
    if (!cfg || !ini_path) return -1;

    FILE* f = fopen(ini_path, "r");
    if (!f) return -1;

    CFG_LOGI("Loading config from: %s", ini_path);

    char line[512];
    int line_num = 0;
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        char* trimmed = trim_whitespace(line);

        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        // Skip section headers [Section]
        if (trimmed[0] == '[')
            continue;

        // Find '='
        char* eq = strchr(trimmed, '=');
        if (!eq) {
            CFG_LOGW("Malformed line %d: %s", line_num, trimmed);
            continue;
        }

        *eq = '\0';
        char* key   = trim_whitespace(trimmed);
        char* value = trim_whitespace(eq + 1);

        parse_ini_line(cfg, key, value);
    }

    fclose(f);
    CFG_LOGI("Config loaded successfully (%d lines parsed)", line_num);
    return 0;
}

int exynos_config_auto_load(ExynosToolsConfig* cfg) {
    if (!cfg) return -1;

    // Initialize defaults first
    exynos_config_defaults(cfg);

    // Try multiple known paths in order of priority
    static const char* search_paths[] = {
        "/sdcard/ExynosTools/exynostools_config.ini",
        "/sdcard/exynostools_config.ini",
        "/data/local/tmp/exynostools_config.ini",
        "/storage/emulated/0/ExynosTools/exynostools_config.ini",
        NULL
    };

    for (int i = 0; search_paths[i] != NULL; i++) {
        if (exynos_config_load(cfg, search_paths[i]) == 0) {
            return 0;
        }
    }

    CFG_LOGI("No config file found, using defaults");
    return -1;
}
