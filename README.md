ExynosTools v1.2.1 – Wrapper Vulkan avanzado para Xclipse (Exynos 2400+)
ExynosTools proporciona un wrapper Vulkan para investigación que intercepta funciones clave, anuncia extensiones seguras, incluye emulación inicial BC4/BC5 por compute (BC6H/BC7 planificados), autodetección Xclipse y perfiles Winlator.

💡 Compatible con Winlator Bionic, DXVK 1.10.x/2.x (según compatibilidad), VKD3D-Proton y Zink.

✅ Cambios clave respecto a versiones anteriores
- Distribución en `tar.zst` con layout `usr/lib/libxeno_wrapper.so` (ya no ZIP ni rutas de APK `libs/arm64-v8a`).
- Se elimina `icd.json`: no es usado por el cargador de Vulkan en Android/Winlator.
- Wrapper con `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` que reenvía al cargador real y aplica parches de features/extensiones cuando procede.
- Emulación BCn: BC4/BC5 con shaders de cómputo iniciales (assets en `assets/shaders/src/`), BC6H/BC7 en preparación.
- Perfiles por aplicación y configuración de rendimiento cargados en `vkCreateInstance`.

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
- Anuncio/patch: añade virtualmente `VK_EXT_descriptor_indexing`, `VK_EXT_robustness2`, `VK_KHR_shader_float16_int8`, `VK_KHR_dynamic_rendering` cuando es seguro; parcha `vkGetPhysicalDeviceFeatures2`.
- BCn: BC4/BC5 iniciales (compute GLSL→SPIR-V); BC6H/BC7 en progreso.
- Detección: heurística Xclipse (vendorID Samsung + nombre/deviceID), con `EXYNOSTOOLS_FORCE` y `EXYNOSTOOLS_WHITELIST`.
- Modo rendimiento: lee `etc/exynostools/performance_mode.conf`.
- Perfiles por app: auto-aplica `.env` por proceso o `EXYNOSTOOLS_APP_PROFILE`.
- FPS: activar con `EXYNOSTOOLS_LOG_FPS=1`.

🧩 Estado de Xclipse
- Falta soporte nativo BC4+ en muchos dispositivos Xclipse; la emulación por compute mejora compatibilidad (BC4/BC5 ya presentes, BC6H/BC7 a seguir).
- `supportsDynamicRendering` puede requerir emulación para DXVK 2+. Referencias públicas sobre issues conocidos enlazadas por la comunidad.
