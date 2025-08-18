#pragma once

#include <stdio.h>
#include <stdlib.h>

void xeno_logging_init(void);
FILE* xeno_log_stream(void);

static inline int xeno_log_enabled_debug(void) {
    const char* e = getenv("EXYNOSTOOLS_DEBUG");
    return e && *e == '1';
}

#define XENO_LOGI(fmt, ...) fprintf(xeno_log_stream(), "[ExynosTools][I] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGW(fmt, ...) fprintf(xeno_log_stream(), "[ExynosTools][W] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGE(fmt, ...) fprintf(xeno_log_stream(), "[ExynosTools][E] " fmt "\n", ##__VA_ARGS__)
#define XENO_LOGD(fmt, ...) do { if (xeno_log_enabled_debug()) fprintf(xeno_log_stream(), "[ExynosTools][D] " fmt "\n", ##__VA_ARGS__); } while(0)

