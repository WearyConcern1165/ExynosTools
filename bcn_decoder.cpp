#include <vulkan/vulkan.h>

struct TargetDeviceFuncs {
    PFN_vkCreateImage vkCreateImage;
    PFN_vkDestroyImage vkDestroyImage;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
    PFN_vkBindImageMemory vkBindImageMemory;
    PFN_vkCreateImageView vkCreateImageView;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
    PFN_vkDestroyDevice vkDestroyDevice;
};

TargetDeviceFuncs g_funcs = {};
