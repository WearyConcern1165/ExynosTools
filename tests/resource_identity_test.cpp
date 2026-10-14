// Copyright 2026 X360 Mobile. SPDX-License-Identifier: BSD-3-Clause
#include "layer_dispatch_key.h"
#include <vulkan/vulkan.h>
#include <cstdio>

int main() {
    struct Object { void* dispatch; int payload; } a{}, b{};
    int shared = 0;
    a.dispatch = b.dispatch = &shared;
    if (dispatch_key(reinterpret_cast<VkCommandBuffer>(&a)) ==
            dispatch_key(reinterpret_cast<VkCommandBuffer>(&b)) ||
        dispatch_key(reinterpret_cast<VkBuffer>(&a)) ==
            dispatch_key(reinterpret_cast<VkBuffer>(&b))) {
        std::puts("FAIL: distinct Vulkan resource handles collide");
        return 1;
    }
    std::puts("PASS: distinct handles sharing a dispatch/storage prefix stay distinct");
    return 0;
}
