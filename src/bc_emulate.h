#pragma once

#include <vulkan/vulkan.h>

typedef enum XenoBCFormat {
    XENO_BC4,
    XENO_BC5,
    XENO_BC6H,
    XENO_BC7
} XenoBCFormat;

typedef struct XenoBCContext XenoBCContext;

XenoBCContext* xeno_bc_create_context(VkDevice device, VkPhysicalDevice phys);
void xeno_bc_destroy_context(XenoBCContext* ctx);

VkResult xeno_bc_decode_image(VkCommandBuffer cmd,
                              XenoBCContext* ctx,
                              VkImage src_bc, VkImage dst_rgba,
                              XenoBCFormat format,
                              VkExtent3D extent);

