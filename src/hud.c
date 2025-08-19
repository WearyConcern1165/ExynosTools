#include "hud.h"
#include "logging.h"
#include <string.h>
#include <math.h>
#include <time.h>

// Simple 8x8 bitmap font data (embedded)
static const unsigned char font_bitmap[128][8] = {
    // Space (32)
    [32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // Numbers 0-9
    [48] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // 0
    [49] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 1
    [50] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00}, // 2
    [51] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}, // 3
    [52] = {0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C, 0x00}, // 4
    [53] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}, // 5
    [54] = {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}, // 6
    [55] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, // 7
    [56] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}, // 8
    [57] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}, // 9
    // Letters A-Z
    [65] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00}, // A
    [66] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // B
    [67] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // C
    [68] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // D
    [69] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}, // E
    [70] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}, // F
    [71] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00}, // G
    [72] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // H
    [73] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // I
    [74] = {0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // J
    [75] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // K
    [76] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // L
    [77] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // M
    [78] = {0x66, 0x76, 0x7E, 0x6E, 0x66, 0x66, 0x66, 0x00}, // N
    [79] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // O
    [80] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // P
    [81] = {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00}, // Q
    [82] = {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00}, // R
    [83] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // S
    [84] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
    [85] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U
    [86] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // V
    [87] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    [88] = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // X
    [89] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00}, // Y
    [90] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}, // Z
    // Colon and period
    [58] = {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00}, // :
    [46] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, // .
};

static VkResult create_font_texture(XenoHUDContext* ctx) {
    const uint32_t fontWidth = 128 * 8;  // 128 chars * 8 pixels wide
    const uint32_t fontHeight = 8;       // 8 pixels tall
    const uint32_t imageSize = fontWidth * fontHeight;
    
    // Create font texture data
    unsigned char* fontData = (unsigned char*)malloc(imageSize);
    if (!fontData) {
        XENO_LOGE("hud: failed to allocate font data");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    
    // Generate font texture from bitmap
    for (int c = 0; c < 128; c++) {
        for (int y = 0; y < 8; y++) {
            unsigned char row = font_bitmap[c][y];
            for (int x = 0; x < 8; x++) {
                int pixelIndex = (y * fontWidth) + (c * 8) + x;
                fontData[pixelIndex] = (row & (0x80 >> x)) ? 255 : 0;
            }
        }
    }
    
    VkResult result;
    
    // Create image
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = {fontWidth, fontHeight, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    
    result = vkCreateImage(ctx->device, &imageInfo, NULL, &ctx->fontImage);
    if (result != VK_SUCCESS) {
        free(fontData);
        return result;
    }
    
    // Allocate memory for image
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(ctx->device, ctx->fontImage, &memReqs);
    
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProps);
    
    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) && 
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryTypeIndex = i;
            break;
        }
    }
    
    if (memoryTypeIndex == UINT32_MAX) {
        free(fontData);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = memoryTypeIndex
    };
    
    result = vkAllocateMemory(ctx->device, &allocInfo, NULL, &ctx->fontImageMemory);
    if (result != VK_SUCCESS) {
        free(fontData);
        return result;
    }
    
    vkBindImageMemory(ctx->device, ctx->fontImage, ctx->fontImageMemory, 0);
    
    // Create image view
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = ctx->fontImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    result = vkCreateImageView(ctx->device, &viewInfo, NULL, &ctx->fontImageView);
    free(fontData);
    
    return result;
}

static VkResult create_render_pass(XenoHUDContext* ctx, VkFormat swapchainFormat) {
    VkAttachmentDescription colorAttachment = {
        .format = swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    
    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef
    };
    
    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass
    };
    
    return vkCreateRenderPass(ctx->device, &renderPassInfo, NULL, &ctx->renderPass);
}

static VkResult create_graphics_pipeline(XenoHUDContext* ctx, VkExtent2D swapchainExtent) {
    // Simple vertex shader (embedded as string)
    const char* vertexShaderCode = 
        "#version 450\n"
        "layout(location = 0) in vec2 inPosition;\n"
        "layout(location = 1) in vec2 inTexCoord;\n"
        "layout(location = 2) in uint inColor;\n"
        "layout(location = 0) out vec2 fragTexCoord;\n"
        "layout(location = 1) out vec4 fragColor;\n"
        "layout(push_constant) uniform PushConstants {\n"
        "    vec2 scale;\n"
        "    vec2 translate;\n"
        "} pc;\n"
        "void main() {\n"
        "    gl_Position = vec4(inPosition * pc.scale + pc.translate, 0.0, 1.0);\n"
        "    fragTexCoord = inTexCoord;\n"
        "    fragColor = unpackUnorm4x8(inColor);\n"
        "}\n";
    
    // Simple fragment shader
    const char* fragmentShaderCode = 
        "#version 450\n"
        "layout(location = 0) in vec2 fragTexCoord;\n"
        "layout(location = 1) in vec4 fragColor;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "layout(binding = 0) uniform sampler2D texSampler;\n"
        "void main() {\n"
        "    float alpha = texture(texSampler, fragTexCoord).r;\n"
        "    outColor = vec4(fragColor.rgb, fragColor.a * alpha);\n"
        "}\n";
    
    // For simplicity, we'll skip the shader compilation here and assume
    // the shaders are compiled. In a real implementation, you'd compile
    // these GLSL strings to SPIR-V.
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding samplerLayoutBinding = {
        .binding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &samplerLayoutBinding
    };
    
    VkResult result = vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &ctx->descriptorSetLayout);
    if (result != VK_SUCCESS) return result;
    
    // Create pipeline layout
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float) * 4  // scale.xy, translate.xy
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &ctx->descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    
    result = vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &ctx->pipelineLayout);
    if (result != VK_SUCCESS) return result;
    
    // For now, return success. In a complete implementation, you'd create
    // the full graphics pipeline with vertex/fragment shaders.
    XENO_LOGI("hud: graphics pipeline creation simplified for demo");
    
    return VK_SUCCESS;
}

XenoHUDContext* xeno_hud_create_context(VkDevice device, VkPhysicalDevice physicalDevice,
                                       VkFormat swapchainFormat, VkExtent2D swapchainExtent,
                                       uint32_t imageCount, VkImageView* swapchainImageViews) {
    XenoHUDContext* ctx = (XenoHUDContext*)calloc(1, sizeof(XenoHUDContext));
    if (!ctx) {
        XENO_LOGE("hud: failed to allocate context");
        return NULL;
    }
    
    ctx->device = device;
    ctx->physicalDevice = physicalDevice;
    ctx->swapchainImageCount = imageCount;
    ctx->frameCount = 0;
    ctx->lastTime = 0.0;
    
    VkResult result;
    
    // Create font texture
    result = create_font_texture(ctx);
    if (result != VK_SUCCESS) {
        XENO_LOGE("hud: failed to create font texture: %d", result);
        goto cleanup;
    }
    
    // Create render pass
    result = create_render_pass(ctx, swapchainFormat);
    if (result != VK_SUCCESS) {
        XENO_LOGE("hud: failed to create render pass: %d", result);
        goto cleanup;
    }
    
    // Create graphics pipeline
    result = create_graphics_pipeline(ctx, swapchainExtent);
    if (result != VK_SUCCESS) {
        XENO_LOGE("hud: failed to create graphics pipeline: %d", result);
        goto cleanup;
    }
    
    // Create framebuffers
    ctx->framebuffers = (VkFramebuffer*)malloc(sizeof(VkFramebuffer) * imageCount);
    if (!ctx->framebuffers) {
        XENO_LOGE("hud: failed to allocate framebuffers");
        goto cleanup;
    }
    
    for (uint32_t i = 0; i < imageCount; i++) {
        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->renderPass,
            .attachmentCount = 1,
            .pAttachments = &swapchainImageViews[i],
            .width = swapchainExtent.width,
            .height = swapchainExtent.height,
            .layers = 1
        };
        
        result = vkCreateFramebuffer(ctx->device, &framebufferInfo, NULL, &ctx->framebuffers[i]);
        if (result != VK_SUCCESS) {
            XENO_LOGE("hud: failed to create framebuffer %d: %d", i, result);
            goto cleanup;
        }
    }
    
    ctx->initialized = 1;
    XENO_LOGI("hud: context created successfully");
    return ctx;
    
cleanup:
    xeno_hud_destroy_context(ctx);
    return NULL;
}

void xeno_hud_destroy_context(XenoHUDContext* ctx) {
    if (!ctx) return;
    
    if (ctx->device) {
        if (ctx->framebuffers) {
            for (uint32_t i = 0; i < ctx->swapchainImageCount; i++) {
                if (ctx->framebuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(ctx->device, ctx->framebuffers[i], NULL);
                }
            }
            free(ctx->framebuffers);
        }
        
        if (ctx->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device, ctx->pipeline, NULL);
        }
        
        if (ctx->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(ctx->device, ctx->pipelineLayout, NULL);
        }
        
        if (ctx->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptorSetLayout, NULL);
        }
        
        if (ctx->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(ctx->device, ctx->descriptorPool, NULL);
        }
        
        if (ctx->renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(ctx->device, ctx->renderPass, NULL);
        }
        
        if (ctx->fontImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(ctx->device, ctx->fontImageView, NULL);
        }
        
        if (ctx->fontImage != VK_NULL_HANDLE) {
            vkDestroyImage(ctx->device, ctx->fontImage, NULL);
        }
        
        if (ctx->fontImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(ctx->device, ctx->fontImageMemory, NULL);
        }
        
        if (ctx->fontSampler != VK_NULL_HANDLE) {
            vkDestroySampler(ctx->device, ctx->fontSampler, NULL);
        }
        
        if (ctx->vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, ctx->vertexBuffer, NULL);
        }
        
        if (ctx->vertexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(ctx->device, ctx->vertexBufferMemory, NULL);
        }
        
        if (ctx->indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx->device, ctx->indexBuffer, NULL);
        }
        
        if (ctx->indexBufferMemory != VK_NULL_HANDLE) {
            vkFreeMemory(ctx->device, ctx->indexBufferMemory, NULL);
        }
    }
    
    free(ctx);
}

VkResult xeno_hud_begin_frame(XenoHUDContext* ctx) {
    if (!ctx || !ctx->initialized) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    ctx->frameCount++;
    return VK_SUCCESS;
}

VkResult xeno_hud_draw(XenoHUDContext* ctx, VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!ctx || !ctx->initialized) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    // Check if HUD is enabled
    const char* hudEnv = getenv("EXYNOSTOOLS_HUD");
    if (!hudEnv || *hudEnv != '1') {
        return VK_SUCCESS;  // HUD disabled
    }
    
    // Simple HUD rendering - just log for now
    XENO_LOGD("hud: drawing frame %u (FPS: %.1f)", ctx->frameCount, 1.0f / ctx->frameTime);
    
    // In a complete implementation, this would:
    // 1. Begin render pass
    // 2. Bind pipeline and descriptor sets
    // 3. Generate text vertices for FPS display
    // 4. Draw the HUD elements
    // 5. End render pass
    
    return VK_SUCCESS;
}

void xeno_hud_end_frame(XenoHUDContext* ctx) {
    if (!ctx) return;
    // Frame cleanup if needed
}

void xeno_hud_update_fps(XenoHUDContext* ctx, double currentTime) {
    if (!ctx) return;
    
    if (ctx->lastTime > 0.0) {
        ctx->frameTime = (float)(currentTime - ctx->lastTime);
    }
    ctx->lastTime = currentTime;
}
