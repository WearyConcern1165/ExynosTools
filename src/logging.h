#pragma once

#include <stdio.h>
#include <stdlib.h>

static inline int xeno_log_enabled_debug(void) {
    const char* e = getenv("EXYNOSTOOLS_DEBUG");
    return e && *e == '1';
}

#define XENO_LOGI(fmt, ...) fprintf(stderr, "[ExynosTools][I] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGW(fmt, ...) fprintf(stderr, "[ExynosTools][W] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGE(fmt, ...) fprintf(stderr, "[ExynosTools][E] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGD(fmt, ...) do { if (xeno_log_enabled_debug()) fprintf(stderr, "[ExynosTools][D] " fmt "\n", ##__VA_ARGS__); } while(0)

