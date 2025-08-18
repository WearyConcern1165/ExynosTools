ExynosTools v1.2.0 – Wrapper Vulkan para Xclipse 940 (Exynos 2400)
ExynosTools incluye un wrapper Vulkan mínimo y abierto para dispositivos con GPU Xclipse 940 (Samsung Exynos 2400). Esta versión corrige empaquetado, rutas y formato de distribución para Winlator Bionic.

💡 Compatible con Winlator Bionic, DXVK 1.10.x/2.x (según compatibilidad), VKD3D-Proton y Zink.

✅ Cambios clave respecto a versiones anteriores
- Distribución en `tar.zst` con layout `usr/lib/libxclipse_wrapper.so` (ya no ZIP ni rutas de APK `libs/arm64-v8a`).
- Se elimina `icd.json`: no es usado por el cargador de Vulkan en Android/Winlator.
- Wrapper no vacío con `vkGetInstanceProcAddr` que reenvía al cargador real (`libvulkan.so.1`/`libvulkan.so`).
- Script de build y empaquetado reproducible.

🔧 Requisitos
- Compilador C, CMake >= 3.15, `tar` con soporte zstd.

🚀 Build y empaquetado
1) Linux (host x86_64 con toolchain cross o nativo en dispositivo):
```
bash scripts/build_and_package.sh
```
El artefacto quedará en `artifacts/xclipse_tools_stable_v1.2.0.tar.zst` con la estructura `usr/lib/libxclipse_wrapper.so`.

📦 Instalación en Winlator Bionic 10.1+
- Copia el archivo `xclipse_tools_stable_v1.2.0.tar.zst` a:
  `/storage/emulated/0/Android/data/com.winlator/files/drivers/`
- En Winlator, abre tu contenedor y selecciona el driver si aplica. Winlator recoge librerías en `usr/lib` automáticamente.

ℹ️ Notas técnicas
- Este wrapper hoy solo reenvía funciones al cargador real (hook mínimo). Puntos de extensión: emulación BC4+ y dynamic rendering.
- Para reemplazar el wrapper por defecto de Winlator (Vortek), puede requerirse parchear el flujo de control (ver `vortek-patcher`).

🧩 Estado de Xclipse
- Falta soporte nativo BC4+ en muchos dispositivos Xclipse; DXVK 1.10.3 funciona ampliamente con wrappers de emulación BC4.
- `supportsDynamicRendering` puede requerir emulación para DXVK 2+. Referencias públicas sobre issues conocidos enlazadas por la comunidad.
