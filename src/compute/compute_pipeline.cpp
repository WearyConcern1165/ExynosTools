#include "compute_pipeline.h"
#include "bcn_decoder/bcn_config.h"

#include <cassert>
#include <cstring>

namespace bcn {

// ---------------------------------------------------------------------------
VkResult ComputePipeline::createDescriptorPool(VkDescriptorPool* outPool) {
    if (!outPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = config::kDescriptorPoolMaxSets;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = config::kDescriptorPoolMaxSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets       = config::kDescriptorPoolMaxSets;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;

    return vkCreateDescriptorPool(device_, &poolInfo, nullptr, outPool);
}

// ---------------------------------------------------------------------------
ComputePipeline::~ComputePipeline() {
    destroy();
}

// ---------------------------------------------------------------------------
VkResult ComputePipeline::create(VkDevice         device,
                                 const uint32_t*  spirvData,
                                 size_t           spirvSize,
                                 uint32_t         pushConstantSize,
                                 uint32_t         wgSizeX,
                                 uint32_t         wgSizeY)
{
    assert(device != VK_NULL_HANDLE);
    device_ = device;

    // -- Shader module -------------------------------------------------------
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = spirvSize;
    smInfo.pCode    = spirvData;

    VkResult res = vkCreateShaderModule(device_, &smInfo, nullptr, &shaderModule_);
    if (res != VK_SUCCESS) return res;

    // -- Descriptor set layout -----------------------------------------------
    // Binding 0: storage image   (output decoded image)
    // Binding 1: storage buffer  (input BC data)
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dsLayoutInfo{};
    dsLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsLayoutInfo.bindingCount = 2;
    dsLayoutInfo.pBindings    = bindings;

    res = vkCreateDescriptorSetLayout(device_, &dsLayoutInfo, nullptr, &dsLayout_);
    if (res != VK_SUCCESS) return res;

    // -- Pipeline layout -----------------------------------------------------
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset     = 0;
    pcRange.size       = pushConstantSize;

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount         = 1;
    plInfo.pSetLayouts            = &dsLayout_;
    plInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1u : 0u;
    plInfo.pPushConstantRanges    = pushConstantSize > 0 ? &pcRange : nullptr;

    res = vkCreatePipelineLayout(device_, &plInfo, nullptr, &pipelineLayout_);
    if (res != VK_SUCCESS) return res;

    // -- Compute pipeline ----------------------------------------------------
    uint32_t specData[2] = { wgSizeX, wgSizeY };
    VkSpecializationMapEntry specEntries[2]{};
    specEntries[0].constantID = 0;
    specEntries[0].offset     = 0;
    specEntries[0].size       = sizeof(uint32_t);

    specEntries[1].constantID = 1;
    specEntries[1].offset     = sizeof(uint32_t);
    specEntries[1].size       = sizeof(uint32_t);

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 2;
    specInfo.pMapEntries   = specEntries;
    specInfo.dataSize      = sizeof(specData);
    specInfo.pData         = specData;

    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.layout = pipelineLayout_;
    cpInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = shaderModule_;
    cpInfo.stage.pName  = "main";
    cpInfo.stage.pSpecializationInfo = &specInfo;

    res = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &cpInfo,
                                   nullptr, &pipeline_);
    if (res != VK_SUCCESS) return res;

    // -- Descriptor pool -----------------------------------------------------
    VkDescriptorPool initialPool = VK_NULL_HANDLE;
    res = createDescriptorPool(&initialPool);
    if (res == VK_SUCCESS) {
        dsPools_.push_back(initialPool);
    }
    return res;
}

// ---------------------------------------------------------------------------
void ComputePipeline::destroy() {
    if (device_ == VK_NULL_HANDLE) return;

    for (VkDescriptorPool pool : dsPools_) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, pool, nullptr);
        }
    }
    dsPools_.clear();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (dsLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, dsLayout_, nullptr);
        dsLayout_ = VK_NULL_HANDLE;
    }
    if (shaderModule_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, shaderModule_, nullptr);
        shaderModule_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
VkResult ComputePipeline::allocateDescriptorSet(VkDescriptorSet* outSet) {
    if (!outSet) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::lock_guard<std::mutex> lock(poolMutex_);

    if (dsPools_.empty()) {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkResult res = createDescriptorPool(&pool);
        if (res != VK_SUCCESS) {
            return res;
        }
        dsPools_.push_back(pool);
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsLayout_;

    allocInfo.descriptorPool = dsPools_.back();
    VkResult res = vkAllocateDescriptorSets(device_, &allocInfo, outSet);
    if (res == VK_SUCCESS) {
        return res;
    }

    if (res != VK_ERROR_OUT_OF_POOL_MEMORY && res != VK_ERROR_FRAGMENTED_POOL) {
        return res;
    }

    VkDescriptorPool newPool = VK_NULL_HANDLE;
    res = createDescriptorPool(&newPool);
    if (res != VK_SUCCESS) {
        return res;
    }
    dsPools_.push_back(newPool);

    allocInfo.descriptorPool = newPool;
    return vkAllocateDescriptorSets(device_, &allocInfo, outSet);
}

// ---------------------------------------------------------------------------
void ComputePipeline::updateDescriptorSet(VkDescriptorSet set,
                                          VkBuffer        storageBuffer,
                                          VkDeviceSize    bufferSize,
                                          VkImageView     storageImageView)
{
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = storageBuffer;
    bufInfo.offset = 0;
    bufInfo.range  = bufferSize;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = storageImageView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = set;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo      = &imgInfo;

    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = set;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo     = &bufInfo;

    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
}

} // namespace bcn
