#include "logging.h"
#include "perf_conf.h"

#include <stdio.h>
#include <string.h>

void xeno_perf_conf_defaults(XenoPerfConf* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->shader_cache_dir, "/storage/emulated/0/Android/data/com.winlator/files/cache/exynostools", sizeof(cfg->shader_cache_dir)-1);
    cfg->pipeline_cache_mb = 64;
    cfg->sync_mode = XENO_SYNC_AGGRESSIVE;
    cfg->validation = XENO_VALIDATION_MINIMAL;
}

static void trim(char* s) {
    // simple trim of newline and spaces at ends
    size_t len = strlen(s);
    while (len && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' ' || s[len-1] == '\t')) s[--len] = 0;
}

void xeno_perf_conf_load(const char* path, XenoPerfConf* cfg) {
    xeno_perf_conf_defaults(cfg);
    FILE* f = fopen(path, "r");
    if (!f) {
        XENO_LOGW("perf_conf: cannot open %s, using defaults", path);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        char key[256], val[768];
        if (sscanf(line, "%255[^=]=%767[^\n]", key, val) == 2) {
            if (strcmp(key, "shader_cache_dir") == 0) {
                strncpy(cfg->shader_cache_dir, val, sizeof(cfg->shader_cache_dir)-1);
            } else if (strcmp(key, "pipeline_cache_mb") == 0) {
                cfg->pipeline_cache_mb = atoi(val);
            } else if (strcmp(key, "sync_mode") == 0) {
                if (strcmp(val, "aggressive") == 0) cfg->sync_mode = XENO_SYNC_AGGRESSIVE;
                else if (strcmp(val, "balanced") == 0) cfg->sync_mode = XENO_SYNC_BALANCED;
                else cfg->sync_mode = XENO_SYNC_SAFE;
            } else if (strcmp(key, "validation") == 0) {
                if (strcmp(val, "off") == 0) cfg->validation = XENO_VALIDATION_OFF;
                else cfg->validation = XENO_VALIDATION_MINIMAL;
            }
        }
    }
    fclose(f);
    XENO_LOGI("perf_conf: loaded from %s (cache_dir=%s, pcache=%dMB)", path, cfg->shader_cache_dir, cfg->pipeline_cache_mb);
}

