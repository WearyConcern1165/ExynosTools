#include "logging.h"
#include "bc_emulate.h"
#include "bc4_shader.h"
#include "bc5_shader.h"
#include <string.h>

static VkResult create_shader_module(VkDevice device, const unsigned char* code, 
                                   unsigned int code_len, VkShaderModule* module) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_len,
        .pCode = (const uint32_t*)code
    };
    return vkCreateShaderModule(device, &createInfo, NULL, module);
}

static VkResult create_descriptor_set_layout(VkDevice device, VkDescriptorSetLayout* layout) {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings
    };
    
    return vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, layout);
}

static VkResult create_pipeline_layout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                     VkPipelineLayout* pipelineLayout) {
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 4  // extent.xy, blocksPerRow, pad
    };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    
    return vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, pipelineLayout);
}

static VkResult create_compute_pipeline(VkDevice device, VkShaderModule shaderModule,
                                      VkPipelineLayout pipelineLayout, VkPipelineCache pipelineCache,
                                      VkPipeline* pipeline) {
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        .layout = pipelineLayout
    };
    
    return vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, NULL, pipeline);
}

XenoBCContext* xeno_bc_create_context(VkDevice device, VkPhysicalDevice phys) {
    XenoBCContext* ctx = (XenoBCContext*)calloc(1, sizeof(XenoBCContext));
    if (!ctx) {
        XENO_LOGE("bc_emulate: failed to allocate context");
        return NULL;
    }
    
    ctx->device = device;
    ctx->physicalDevice = phys;
    
    VkResult result;
    
    // Create pipeline cache
    VkPipelineCacheCreateInfo cacheInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    result = vkCreatePipelineCache(device, &cacheInfo, NULL, &ctx->pipelineCache);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create pipeline cache: %d", result);
        goto cleanup;
    }
    
    // Create descriptor set layout
    result = create_descriptor_set_layout(device, &ctx->descriptorSetLayout);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create descriptor set layout: %d", result);
        goto cleanup;
    }
    
    // Create pipeline layout
    result = create_pipeline_layout(device, ctx->descriptorSetLayout, &ctx->pipelineLayout);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create pipeline layout: %d", result);
        goto cleanup;
    }
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16 }
    };
    
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 16,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };
    
    result = vkCreateDescriptorPool(device, &poolInfo, NULL, &ctx->descriptorPool);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create descriptor pool: %d", result);
        goto cleanup;
    }
    
    // Initialize pipelines array
    for (int i = 0; i < XENO_BC_FORMAT_COUNT; i++) {
        ctx->pipelines[i] = VK_NULL_HANDLE;
    }
    
    // Create BC4 pipeline
    VkShaderModule bc4Module;
    result = create_shader_module(device, bc4_shader_spv, bc4_shader_spv_len, &bc4Module);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create BC4 shader module: %d", result);
        goto cleanup;
    }
    
    result = create_compute_pipeline(device, bc4Module, ctx->pipelineLayout, 
                                   ctx->pipelineCache, &ctx->pipelines[XENO_BC4]);
    vkDestroyShaderModule(device, bc4Module, NULL);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create BC4 pipeline: %d", result);
        goto cleanup;
    }
    
    // Create BC5 pipeline
    VkShaderModule bc5Module;
    result = create_shader_module(device, bc5_shader_spv, bc5_shader_spv_len, &bc5Module);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create BC5 shader module: %d", result);
        goto cleanup;
    }
    
    result = create_compute_pipeline(device, bc5Module, ctx->pipelineLayout, 
                                   ctx->pipelineCache, &ctx->pipelines[XENO_BC5]);
    vkDestroyShaderModule(device, bc5Module, NULL);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create BC5 pipeline: %d", result);
        goto cleanup;
    }
    
    ctx->initialized = 1;
    XENO_LOGI("bc_emulate: context created successfully with BC4/BC5 support");
    return ctx;
    
cleanup:
    xeno_bc_destroy_context(ctx);
    return NULL;
}

void xeno_bc_destroy_context(XenoBCContext* ctx) {
    if (!ctx) return;
    
    if (ctx->device) {
        for (int i = 0; i < XENO_BC_FORMAT_COUNT; i++) {
            if (ctx->pipelines[i] != VK_NULL_HANDLE) {
                vkDestroyPipeline(ctx->device, ctx->pipelines[i], NULL);
            }
        }
        
        if (ctx->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(ctx->device, ctx->descriptorPool, NULL);
        }
        
        if (ctx->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(ctx->device, ctx->pipelineLayout, NULL);
        }
        
        if (ctx->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(ctx->device, ctx->descriptorSetLayout, NULL);
        }
        
        if (ctx->pipelineCache != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(ctx->device, ctx->pipelineCache, NULL);
        }
    }
    
    free(ctx);
}

VkResult xeno_bc_decode_image(VkCommandBuffer cmd,
                              XenoBCContext* ctx,
                              VkBuffer src_bc, VkImage dst_rgba,
                              XenoBCFormat format,
                              VkExtent3D extent) {
    if (!ctx || !ctx->initialized) {
        XENO_LOGE("bc_emulate: context not initialized");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    if (format >= XENO_BC_FORMAT_COUNT || ctx->pipelines[format] == VK_NULL_HANDLE) {
        XENO_LOGE("bc_emulate: unsupported format %d", format);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    
    // Pre-dispatch barrier: transition image to general layout
    VkImageMemoryBarrier preBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst_rgba,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &preBarrier);
    
    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipelines[format]);
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &ctx->descriptorSetLayout
    };
    
    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(ctx->device, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to allocate descriptor set: %d", result);
        return result;
    }
    
    // Create image view for destination
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = dst_rgba,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    VkImageView imageView;
    result = vkCreateImageView(ctx->device, &viewInfo, NULL, &imageView);
    if (result != VK_SUCCESS) {
        XENO_LOGE("bc_emulate: failed to create image view: %d", result);
        return result;
    }
    
    // Update descriptor set
    VkDescriptorBufferInfo bufferInfo = {
        .buffer = src_bc,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };
    
    VkDescriptorImageInfo imageInfo = {
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };
    
    VkWriteDescriptorSet descriptorWrites[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &bufferInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .pImageInfo = &imageInfo
        }
    };
    
    vkUpdateDescriptorSets(ctx->device, 2, descriptorWrites, 0, NULL);
    
    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipelineLayout, 
                           0, 1, &descriptorSet, 0, NULL);
    
    // Push constants
    uint32_t pushConstants[4] = {
        extent.width,
        extent.height,
        (extent.width + 3) / 4,  // blocksPerRow
        0  // padding
    };
    
    vkCmdPushConstants(cmd, ctx->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 
                      0, sizeof(pushConstants), pushConstants);
    
    // Dispatch compute shader
    uint32_t groupsX = (extent.width + 3) / 4;
    uint32_t groupsY = (extent.height + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
    
    // Post-dispatch barrier: transition to shader read-only
    VkImageMemoryBarrier postBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dst_rgba,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &postBarrier);
    
    // Clean up image view (descriptor set will be freed when pool is reset)
    vkDestroyImageView(ctx->device, imageView, NULL);
    
    XENO_LOGD("bc_emulate: decoded %dx%d texture using format %d", extent.width, extent.height, format);
    return VK_SUCCESS;
}

