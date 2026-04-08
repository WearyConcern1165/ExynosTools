#pragma once
/// @file compute_pipeline.h
/// @brief Generic compute pipeline creation and descriptor set management.

#include <vulkan/vulkan.h>
#include <mutex>
#include <vector>
#include <cstdint>

namespace bcn {

/// Manages a compute pipeline: shader module, descriptor set layout,
/// pipeline layout, and the pipeline itself.
class ComputePipeline {
public:
    ComputePipeline() = default;
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    /// @param spirvData   Pointer to compiled SPIR-V bytecode.
    /// @param spirvSize   Size in bytes of the SPIR-V data.
    /// @param pushConstantSize  Size of push constants in bytes (0 if unused).
    ///
    /// The created descriptor set layout has:
    ///   binding 0 = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER  (input BC data)
    ///   binding 1 = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE   (output decoded image)
    ///   binding 1 = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE   (output decoded image)
    VkResult create(VkDevice       device,
                    const uint32_t* spirvData,
                    size_t          spirvSize,
                    uint32_t        pushConstantSize = 0,
                    uint32_t        wgSizeX = 8,
                    uint32_t        wgSizeY = 8);

    void destroy();

    /// Allocate a descriptor set from the internal pool.
    VkResult allocateDescriptorSet(VkDescriptorSet* outSet);

    /// Update a descriptor set with a storage buffer (binding 0) and
    /// a storage image (binding 1).
    void updateDescriptorSet(VkDescriptorSet set,
                             VkBuffer        storageBuffer,
                             VkDeviceSize    bufferSize,
                             VkImageView     storageImageView);

    VkPipeline        pipeline()       const { return pipeline_; }
    VkPipelineLayout  pipelineLayout() const { return pipelineLayout_; }
    VkDescriptorSetLayout descriptorSetLayout() const { return dsLayout_; }

private:
    VkResult createDescriptorPool(VkDescriptorPool* outPool);

    VkDevice              device_         = VK_NULL_HANDLE;
    VkShaderModule        shaderModule_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsLayout_       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_       = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> dsPools_;
    std::mutex            poolMutex_;
};

} // namespace bcn
