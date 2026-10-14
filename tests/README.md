# Standalone regression tests

These tests call the actual layer helper, decoder and settings parser. They
do not require X360 Mobile, game files, a Vulkan device or a running GPU queue.

- `resource_identity_test.cpp`: two distinct handles with the same leading
  dispatch/storage word must not collide in per-resource maps.
- `bcn_cpu_decoder_test.cpp`: BC4/BC5 UNORM endpoint selectors in a nonuniform
  4x4 block; check all 16 texels and both channels against explicit expected bytes.
- `layer_settings_test.cpp`: 13 real settings, layer-name filtering, pNext
  traversal, missing values, numeric limits and unchanged defaults without settings.

Supply the Android NDK and a recent Vulkan-Headers checkout (including
`VK_EXT_layer_settings`). Python 3.9+:

```text
python tests/run_android.py --ndk <NDK> --headers <Vulkan-Headers> --output <new-directory>
```

Add `--adb <adb-path> --serial <authorized-device>` to run the binaries.
The runner removes only its own exact temporary executable. Build and result
logs stay in the new output directory. NDK 29/API 30/ARM64 was used for the
original before/after fixtures. This runner is a portable packaging of those
fixtures; the device results below were acquired with the original runners.

## Numerical Vulkan checks in X360 Mobile

| Device | Driver | Original layer | Corrected layer |
| --- | --- | --- | --- |
| A55, Exynos 1480 / Xclipse 530, Android 15 / One UI 7, 4 KiB | Samsung 24.3.9 | BC4 first R texel: 1 instead of 0 | RGBA8, BC1–BC5 and D32 pass, three runs |
| Z Flip7, Exynos 2500 / Xclipse 950, Android 16 / One UI 8.5, 4 KiB | Samsung 24.0.555 | Same BC4 mismatch | Same seven formats pass, six runs |

The device probe checks 16 texels per 4x4 image against known values, plus
buffer readback, with synchronization validation: zero errors/warnings on
the corrected layer. System/Helix support BC1–BC3 natively; BC4/BC5 use the
layer's CPU fallback on these devices. Both values of `cpu_decode_primary`
were exercised; false still used CPU fallback, so this does not verify
the compute decoder. No FPS or game compatibility claim is made.

The separately tested legacy ExynosTools binary failed with missing command
buffer mapping. That binary's exact source was not established; it is not
used as proof that this PR fixes that separate release.

SNORM, sRGB, mip/array/3D uploads, attachment/WSI, sustained workloads and
16 KiB kernels remain outside these results. The tests in this directory
are BSD-3-Clause; see `LICENSE`.
