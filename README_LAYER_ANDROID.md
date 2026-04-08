# ExynosTools Vulkan Layer (Android)

## Files

- `libvulkan_xclipse.so`
- `VkLayer_exynostools.json`

## Enable Per-App Debug Layer

```bash
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app com.your.app
adb shell settings put global gpu_debug_layers VK_LAYER_EXYNOSTOOLS_bcn_decode
adb shell settings put global gpu_debug_layer_app com.your.app
```

## Disable

```bash
adb shell settings delete global enable_gpu_debug_layers
adb shell settings delete global gpu_debug_app
adb shell settings delete global gpu_debug_layers
adb shell settings delete global gpu_debug_layer_app
```

## Behavior

1. Negotiates through `vkNegotiateLoaderLayerInterfaceVersion`.
2. Intercepts BCn-related image creation and buffer-to-image upload calls.
3. Uses the embedded C++ decoder path on Samsung Xclipse GPUs.
4. Falls back to passthrough behavior on unsupported devices.
