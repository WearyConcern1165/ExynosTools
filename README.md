ExynosTools v1.2.1 – Wrapper Vulkan avanzado para Xclipse (Exynos 2400+)
ExynosTools proporciona un wrapper Vulkan para investigación que intercepta funciones clave, anuncia extensiones seguras, incluye stubs de emulación BC4–BC7 por compute, autodetección Xclipse y perfiles Winlator.

💡 Compatible con Winlator Bionic, DXVK 1.10.x/2.x (según compatibilidad), VKD3D-Proton y Zink.

✅ Cambios clave respecto a versiones anteriores
- Distribución en `tar.zst` con layout `usr/lib/libxclipse_wrapper.so` (ya no ZIP ni rutas de APK `libs/arm64-v8a`).
- Se elimina `icd.json`: no es usado por el cargador de Vulkan en Android/Winlator.
- Wrapper no vacío con `vkGetInstanceProcAddr` que reenvía al cargador real (`libvulkan.so.1`/`libvulkan.so`).
- Script de build y empaquetado reproducible.

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
- En Winlator, abre tu contenedor y selecciona el driver si aplica. Winlator recoge librerías en `usr/lib` automáticamente.

ℹ️ Notas técnicas
- Intercepción: `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateInstance`, `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures2`, `vkEnumerateDeviceExtensionProperties`, `vkCreateDevice`, `vkCreateSwapchainKHR`, `vkQueuePresentKHR`.
- Anuncio/patch: añade virtualmente `VK_EXT_descriptor_indexing`, `VK_EXT_robustness2`, `VK_KHR_shader_float16_int8` cuando es seguro; parcha `vkGetPhysicalDeviceFeatures2`.
- BCn: stubs listos para integrar decoders reales; assets SPIR-V placeholder en `assets/shaders/decode/`.
- Detección: heurística Xclipse (vendorID Samsung + nombre), con `EXYNOSTOOLS_FORCE` para forzar on/off.
- Modo rendimiento: lee `etc/exynostools/performance_mode.conf`.
- FPS: activar con `EXYNOSTOOLS_LOG_FPS=1`.
- Para reemplazar el wrapper por defecto de Winlator (Vortek), puede requerirse parchear el flujo de control (ver `vortek-patcher`).

🧩 Estado de Xclipse
- Falta soporte nativo BC4+ en muchos dispositivos Xclipse; DXVK 1.10.3 funciona ampliamente con wrappers de emulación BC4.
- `supportsDynamicRendering` puede requerir emulación para DXVK 2+. Referencias públicas sobre issues conocidos enlazadas por la comunidad.
