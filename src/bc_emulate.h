#pragma once

#include <vulkan/vulkan.h>
#include <stdlib.h>

typedef enum XenoBCFormat {
    XENO_BC4,
    XENO_BC5,
    XENO_BC6H,
    XENO_BC7,
    XENO_BC_FORMAT_COUNT
} XenoBCFormat;

typedef struct XenoBCContext {
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkPipelineCache pipelineCache;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipelines[XENO_BC_FORMAT_COUNT];
    VkDescriptorPool descriptorPool;
    VkCommandPool commandPool;
    int initialized;
} XenoBCContext;

XenoBCContext* xeno_bc_create_context(VkDevice device, VkPhysicalDevice phys);
void xeno_bc_destroy_context(XenoBCContext* ctx);

VkResult xeno_bc_decode_image(VkCommandBuffer cmd,
                              XenoBCContext* ctx,
                              VkBuffer src_bc, VkImage dst_rgba,
                              XenoBCFormat format,
                              VkExtent3D extent);

