#pragma once

#include <vulkan/vulkan.h>

typedef struct XenoHudContext XenoHudContext;

XenoHudContext* xeno_hud_init(VkDevice device, VkFormat swapchainFormat);
void xeno_hud_shutdown(XenoHudContext* ctx);

// For now, a minimal stub that can be expanded to real rendering
void xeno_hud_draw(XenoHudContext* ctx, VkQueue queue, const VkPresentInfoKHR* pPresentInfo, int fps);

