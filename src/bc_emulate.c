#include "logging.h"
#include "bc_emulate.h"

struct XenoBCContext { int placeholder; };

XenoBCContext* xeno_bc_create_context(VkDevice device, VkPhysicalDevice phys) {
    (void)device; (void)phys;
    XenoBCContext* ctx = (XenoBCContext*)calloc(1, sizeof(XenoBCContext));
    XENO_LOGI("bc_emulate: context created (stub)");
    return ctx;
}

void xeno_bc_destroy_context(XenoBCContext* ctx) {
    free(ctx);
}

VkResult xeno_bc_decode_image(VkCommandBuffer cmd,
                              XenoBCContext* ctx,
                              VkImage src_bc, VkImage dst_rgba,
                              XenoBCFormat format,
                              VkExtent3D extent) {
    (void)cmd; (void)ctx; (void)src_bc; (void)dst_rgba; (void)format; (void)extent;
    // Minimal functional stub: no-op decode for now
    XENO_LOGD("bc_emulate: decode invoked");
    return VK_SUCCESS;
}

