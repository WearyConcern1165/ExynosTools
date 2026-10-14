// Copyright 2026 X360 Mobile. SPDX-License-Identifier: BSD-3-Clause
// Link the actual parser, not a copied implementation or a mock.
#include "layer_settings_runtime.h"
#include <cstdio>

int main() {
  VkBool32 no = VK_FALSE, yes = VK_TRUE;
  int32_t depth = 3, upload = 256, level = 1;
  uint32_t mask = 123;
  constexpr const char* layer = "VK_LAYER_VORTEK_XCLIPSE";
  VkLayerSettingEXT entries[] = {
      {layer, "enabled", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "xclipse_only", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "bcn_intercept", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "force_bcn_emulation", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "cpu_decode_primary", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "strict_dispatch", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &yes},
      {layer, "drop_on_missing_commandbuffer_map", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &yes},
      {layer, "block_incompatible_virtual_copies", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &no},
      {layer, "microbenchmark_enabled", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &yes},
      {layer, "depth_override_mode", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &depth},
      {layer, "cpu_fallback_max_upload_mb", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &upload},
      {layer, "log_level", VK_LAYER_SETTING_TYPE_INT32_EXT, 1, &level},
      {layer, "disabled_bcn_mask", VK_LAYER_SETTING_TYPE_UINT32_EXT, 1, &mask},
  };
  VkLayerSettingsCreateInfoEXT settings{VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT};
  settings.settingCount = sizeof(entries) / sizeof(entries[0]);
  settings.pSettings = entries;
  // A preceding unrelated node must not hide the settings node.
  VkBaseInStructure preceding{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      reinterpret_cast<const VkBaseInStructure*>(&settings)};
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pNext = &preceding;
  LayerSettingsSnapshot actual;
  apply_layer_settings_from_create_info(&info, &actual);
  bool configured = !actual.enabled && !actual.xclipse_only && !actual.bcn_intercept &&
      !actual.force_bcn_emulation && !actual.cpu_decode_primary && actual.strict_dispatch &&
      actual.drop_on_missing_commandbuffer_map && !actual.block_incompatible_virtual_copies &&
      actual.microbenchmark_enabled && actual.depth_override_mode == 3 &&
      actual.disabled_bcn_mask == 123 && actual.cpu_fallback_max_upload_mb == 256 && actual.log_level == 1;
  if (!configured) {
    std::printf("FAIL: requested overrides not applied: enabled=%d force_bcn=%d cpu_decode=%d upload=%u mask=%u\n",
        actual.enabled, actual.force_bcn_emulation, actual.cpu_decode_primary,
        actual.cpu_fallback_max_upload_mb, actual.disabled_bcn_mask);
    return 1;
  }
  // Exact layer scoping: another layer's settings must not affect us.
  entries[0].pLayerName = "VK_LAYER_OTHER";
  actual = LayerSettingsSnapshot{};
  apply_layer_settings_from_create_info(&info, &actual);
  if (!actual.enabled) return 2;
  // Empty values and null names must be ignored, retaining the caller's values.
  entries[0].pLayerName = layer; entries[0].valueCount = 0;
  entries[1].pSettingName = nullptr;
  actual = LayerSettingsSnapshot{};
  apply_layer_settings_from_create_info(&info, &actual);
  if (!actual.enabled || !actual.xclipse_only) return 3;
  // Published numeric bounds, including negative INT32 input.
  upload = -1; depth = 99; level = -1;
  apply_layer_settings_from_create_info(&info, &actual);
  if (actual.cpu_fallback_max_upload_mb != 16 || actual.depth_override_mode != 3 || actual.log_level != 0) return 4;
  upload = 900; level = 99;
  apply_layer_settings_from_create_info(&info, &actual);
  if (actual.cpu_fallback_max_upload_mb != 512 || actual.log_level != 2) return 5;
  info.pNext = nullptr;
  actual = LayerSettingsSnapshot{};
  apply_layer_settings_from_create_info(&info, &actual);
  if (!actual.enabled || actual.disabled_bcn_mask != 0) return 6;
  std::puts("PASS: real parser, 13 settings, layer scoping, pNext traversal, missing values and numeric bounds.");
  return 0;
}
