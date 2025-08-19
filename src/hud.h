#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

typedef struct XenoHUDContext {
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkSampler fontSampler;
    VkImage fontImage;
    VkDeviceMemory fontImageMemory;
    VkImageView fontImageView;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t swapchainImageCount;
    VkFramebuffer* framebuffers;
    int initialized;
    float frameTime;
    uint32_t frameCount;
    double lastTime;
} XenoHUDContext;

typedef struct HUDVertex {
    float pos[2];
    float uv[2];
    uint32_t color;
} HUDVertex;

XenoHUDContext* xeno_hud_create_context(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkFormat swapchainFormat, VkExtent2D swapchainExtent,
                                       uint32_t imageCount, VkImageView* swapchainImageViews);

void xeno_hud_destroy_context(XenoHUDContext* ctx);

VkResult xeno_hud_begin_frame(XenoHUDContext* ctx);
VkResult xeno_hud_draw(XenoHUDContext* ctx, VkCommandBuffer cmd, uint32_t imageIndex);
void xeno_hud_end_frame(XenoHUDContext* ctx);

void xeno_hud_update_fps(XenoHUDContext* ctx, double currentTime);
