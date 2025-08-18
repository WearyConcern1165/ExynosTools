#pragma once

typedef struct XenoPerfConf {
    char shader_cache_dir[512];
    int pipeline_cache_mb;
    enum { XENO_SYNC_AGGRESSIVE, XENO_SYNC_BALANCED, XENO_SYNC_SAFE } sync_mode;
    enum { XENO_VALIDATION_OFF, XENO_VALIDATION_MINIMAL } validation;
} XenoPerfConf;

void xeno_perf_conf_defaults(XenoPerfConf* cfg);
void xeno_perf_conf_load(const char* path, XenoPerfConf* cfg);

