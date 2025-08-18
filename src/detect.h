#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

typedef struct XenoDetectConfig {
    int force_enable;
    int force_disable;
} XenoDetectConfig;

int xeno_is_xclipse_gpu(VkPhysicalDevice phys, const XenoDetectConfig* cfg);
void xeno_detect_parse_env(XenoDetectConfig* cfg);

