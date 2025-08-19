ExynosTools v1.3.0 (Stable) – Wrapper Vulkan avanzado para Xclipse (Exynos 2400+)
ExynosTools proporciona un wrapper Vulkan de grado de producción que intercepta funciones clave, anuncia extensiones seguras, incluye emulación completa BC4/BC5 por compute shader (BC6H/BC7 preparados), autodetección Xclipse mejorada, HUD en pantalla y sistema de perfiles unificado.

💡 Compatible con Winlator Bionic, DXVK 1.10.x/2.x (según compatibilidad), VKD3D-Proton y Zink.

✅ Nuevas características v1.3.0 (Stable)
- **Emulación BCn completa**: BC4/BC5 completamente funcional con shaders SPIR-V embebidos, BC6H/BC7 preparados para futuras versiones.
- **HUD en pantalla**: Activar con `EXYNOSTOOLS_HUD=1` para mostrar FPS y estadísticas en tiempo real.
- **Sistema de perfiles unificado**: Migración de `.env` a `.conf` con sintaxis consistente y mejor organización.
- **Detección GPU mejorada**: IDs de dispositivo actualizados para Xclipse 920, 940 y variantes futuras.
- **Manejo de errores robusto**: Validación completa y limpieza de recursos Vulkan.
- **Build system mejorado**: Compilación automática de shaders GLSL a SPIR-V y embedding como headers C.

🔧 Requisitos
- Compilador C, CMake >= 3.15, `tar` con soporte zstd.
- `glslc` (Shaderc) para compilación de shaders GLSL a SPIR-V.
- `xxd` para embedding de shaders como headers C.

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
- **Intercepción**: `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateInstance`, `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkGetPhysicalDeviceFeatures2`, `vkEnumerateDeviceExtensionProperties`, `vkCreateDevice`, `vkCreateSwapchainKHR`, `vkQueuePresentKHR`.
- **Anuncio/patch**: añade virtualmente `VK_EXT_descriptor_indexing`, `VK_EXT_robustness2`, `VK_KHR_shader_float16_int8`, `VK_KHR_dynamic_rendering` cuando es seguro; parcha `vkGetPhysicalDeviceFeatures2`.
- **BCn emulación**: BC4/BC5 completamente funcional (compute GLSL→SPIR-V embebido); BC6H/BC7 preparados.
- **Detección GPU**: heurística Xclipse mejorada (vendorID Samsung + nombre/deviceID conocidos), con `EXYNOSTOOLS_FORCE` y `EXYNOSTOOLS_WHITELIST`.
- **Configuración**: lee `etc/exynostools/performance_mode.conf` para rendimiento global.
- **Perfiles por app**: sistema unificado `.conf` en `etc/exynostools/profiles/` o `EXYNOSTOOLS_APP_PROFILE`.
- **HUD**: activar con `EXYNOSTOOLS_HUD=1` para overlay de FPS.
- **Logging FPS**: activar con `EXYNOSTOOLS_LOG_FPS=1` para logs de rendimiento.

🧩 Estado de Xclipse
- Falta soporte nativo BC4+ en muchos dispositivos Xclipse; la emulación por compute mejora compatibilidad (BC4/BC5 ya presentes, BC6H/BC7 a seguir).
- `supportsDynamicRendering` puede requerir emulación para DXVK 2+. Referencias públicas sobre issues conocidos enlazadas por la comunidad.
