/// @file test_format_support.cpp
/// @brief Smoke test: initialises the decoder context and prints the BCn
///        format support table for the current device.
///
/// This is a minimal test meant to validate:
///  1. The library links and starts correctly.
///  2. Vulkan instance/device creation works.
///  3. Format support detection produces sensible results.
///
/// To run on device:
///   adb push test_format_support /data/local/tmp/
///   adb shell /data/local/tmp/test_format_support

#include "bcn_decoder/bcn_decoder.h"
#include "bcn_decoder/bcn_types.h"

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

/// Minimal Vulkan bootstrap just for this test.
/// In production, the app already has a VkDevice.
struct TestVulkan {
    VkInstance       instance    = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice  = VK_NULL_HANDLE;
    VkDevice         device      = VK_NULL_HANDLE;
    VkQueue          queue       = VK_NULL_HANDLE;
    uint32_t         queueFamily = 0;

    bool init() {
        // Instance
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "bcn_format_test";
        appInfo.applicationVersion = 1;
        appInfo.apiVersion         = VK_API_VERSION_1_1;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;

        if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) {
            std::fprintf(stderr, "Failed to create VkInstance\n");
            return false;
        }

        // Physical device (pick first)
        uint32_t gpuCount = 0;
        vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
        if (gpuCount == 0) {
            std::fprintf(stderr, "No Vulkan physical devices found\n");
            return false;
        }
        std::vector<VkPhysicalDevice> gpus(gpuCount);
        vkEnumeratePhysicalDevices(instance, &gpuCount, gpus.data());
        physDevice = gpus[0];

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physDevice, &props);
        std::printf("GPU: %s (Vulkan %u.%u.%u)\n",
                    props.deviceName,
                    VK_VERSION_MAJOR(props.apiVersion),
                    VK_VERSION_MINOR(props.apiVersion),
                    VK_VERSION_PATCH(props.apiVersion));

        // Queue family with COMPUTE
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDevice, &qfCount, qfProps.data());

        for (uint32_t i = 0; i < qfCount; ++i) {
            if (qfProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                queueFamily = i;
                break;
            }
        }

        // Device
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qInfo{};
        qInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = queueFamily;
        qInfo.queueCount       = 1;
        qInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo devInfo{};
        devInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos    = &qInfo;

        if (vkCreateDevice(physDevice, &devInfo, nullptr, &device) != VK_SUCCESS) {
            std::fprintf(stderr, "Failed to create VkDevice\n");
            return false;
        }

        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        return true;
    }

    void shutdown() {
        if (device)   vkDestroyDevice(device, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

int main() {
    TestVulkan vk;
    if (!vk.init()) return 1;

    // Create decoder context
    bcn::DecoderCreateInfo createInfo{};
    createInfo.instance           = vk.instance;
    createInfo.physicalDevice     = vk.physDevice;
    createInfo.device             = vk.device;
    createInfo.computeQueue       = vk.queue;
    createInfo.computeQueueFamily = vk.queueFamily;
    createInfo.memoryCacheBudgetBytes = 0; // No cache for this test
    createInfo.enableValidation   = false;

    bcn::BcnDecoderContext* ctx = nullptr;
    bcn::BcnResult res = bcn::BcnDecoderContext::create(createInfo, &ctx);
    if (res != bcn::BcnResult::Success) {
        std::fprintf(stderr, "Decoder create failed: %s\n",
                     bcn::resultToString(res));
        vk.shutdown();
        return 1;
    }

    // Print format support table
    const char* fmtNames[] = {
        "BC1_RGB_UNORM", "BC2_UNORM", "BC3_UNORM",
        "BC4_UNORM",     "BC5_UNORM", "BC7_UNORM"
    };
    const char* pathNames[] = { "NotSupported", "Native", "ComputeFallback" };

    std::printf("\n%-16s  %-16s\n", "Format", "Support Path");
    std::printf("%-16s  %-16s\n", "------", "------------");
    for (uint32_t i = 0; i < static_cast<uint32_t>(bcn::BcnFormat::Count); ++i) {
        auto path = ctx->queryFormatSupport(static_cast<bcn::BcnFormat>(i));
        std::printf("%-16s  %-16s\n",
                    fmtNames[i],
                    pathNames[static_cast<int>(path)]);
    }

    ctx->destroy(); // Also deletes ctx
    vk.shutdown();

    std::printf("\nTest passed.\n");
    return 0;
}
