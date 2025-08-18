#include "hud.h"
#include "logging.h"

struct XenoHudContext { VkDevice device; VkFormat fmt; };

XenoHudContext* xeno_hud_init(VkDevice device, VkFormat swapchainFormat) {
    XenoHudContext* ctx = (XenoHudContext*)calloc(1, sizeof(XenoHudContext));
    ctx->device = device; ctx->fmt = swapchainFormat;
    XENO_LOGI("HUD: initialized (format=%d)", (int)swapchainFormat);
    return ctx;
}

void xeno_hud_shutdown(XenoHudContext* ctx) {
    if (!ctx) return; free(ctx);
}

void xeno_hud_draw(XenoHudContext* ctx, VkQueue queue, const VkPresentInfoKHR* pPresentInfo, int fps) {
    (void)ctx; (void)queue; (void)pPresentInfo; (void)fps;
    // Minimal placeholder: log-only; real rendering would record commands
    XENO_LOGD("HUD: draw %d FPS", fps);
}

