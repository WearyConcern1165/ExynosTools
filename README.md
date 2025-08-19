ExynosTools v1.3.1 – Wrapper Vulkan de producción para Xclipse (Exynos 2400+)
ExynosTools proporciona un wrapper Vulkan de alto rendimiento con emulación BCn funcional (BC4–BC7), emulación de dynamicRendering y HUD opcional, orientado a producción.

💡 Compatible con Winlator Bionic, DXVK 2.x, VKD3D-Proton y Zink.

✅ Novedades 1.3.1 (Estable)
- Emulación BCn funcional: BC4, BC5, BC6h y BC7 (compute SPIR-V embebidos en el binario).
- DynamicRendering: emulación de `VK_KHR_dynamic_rendering` para DXVK 2.x.
- HUD opcional: `EXYNOSTOOLS_HUD=1` para mostrar FPS en pantalla.
- Perfiles por app unificados: `.conf` en `/etc/exynostools/profiles/`, parseados como K/V.
- Detección Xclipse mejorada: lista interna de `deviceID` + `vendorID` Samsung.
- Distribución `.tar.zst` lista para Winlator.

🔧 Requisitos
- Compilador C, CMake >= 3.15, `tar` con soporte zstd.

🚀 Build y empaquetado (CMake)
1) Linux (host x86_64 con NDK r25b para Android arm64):
```
bash scripts/build_and_package.sh
```
El artefacto quedará en `artifacts/exynostools-android-arm64.tar.zst` con la estructura requerida por Winlator.

Alternativa Meson + Ninja (Android cross):
```
meson setup build-android --cross-file=android/arm64.txt -Dbuildtype=release
ninja -C build-android
```

📦 Instalación en Winlator Bionic 10.1+
- Copia el archivo `exynostools-android-arm64.tar.zst` a:
  `/storage/emulated/0/Android/data/com.winlator/files/drivers/`
- Winlator recoge librerías en `usr/lib` automáticamente.

ℹ️ Notas técnicas
- Intercepción: `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateInstance`, `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures2`, `vkEnumerateDeviceExtensionProperties`, `vkCreateDevice`, `vkCreateSwapchainKHR`, `vkQueuePresentKHR`.
- Parcheo/emulación: `descriptor_indexing`, `robustness2`, `shader_float16_int8`, `dynamic_rendering`, `custom_border_color`, `primitive_topology_list_restart`.
- BCn: shaders SPIR-V embebidos; despacho compute automático cuando el formato no es nativo.
- Perfiles: `.conf` en `/etc/exynostools/profiles/` o forzar con `EXYNOSTOOLS_APP_PROFILE=/ruta/perfil.conf`.
- HUD: `EXYNOSTOOLS_HUD=1`; FPS por log: `EXYNOSTOOLS_LOG_FPS=1`.