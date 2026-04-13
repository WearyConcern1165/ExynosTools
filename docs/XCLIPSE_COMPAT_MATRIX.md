# Matriz Tecnica Xclipse (Exynos) y Reglas de Decision del Wrapper

Fecha de corte: 10 de abril de 2026.

Este documento define una base tecnica para tomar decisiones en tiempo de ejecucion dentro de ExynosTools, evitando supuestos fijos por modelo y priorizando deteccion real por driver.

## 1. Fuentes base

- Samsung Semiconductor (paginas oficiales de Exynos):
  - Exynos 2200 (Xclipse 920): https://semiconductor.samsung.com/news-events/news/samsung-announces-game-changing-exynos-2200/
  - Exynos 1480 (Xclipse 530): https://semiconductor.samsung.com/processor/mobile-processor/exynos-1480/
  - Exynos 2400 (Xclipse 940): https://semiconductor.samsung.com/processor/mobile-processor/exynos-2400/
  - Exynos 2500 (Xclipse 950): https://semiconductor.samsung.com/processor/mobile-processor/exynos-2500/
  - Exynos 2600 (Xclipse 960): https://semiconductor.samsung.com/processor/mobile-processor/exynos-2600/
- Vulkan GPUInfo (observacion real de capacidades Vulkan por dispositivo/driver):
  - Xclipse 530 (Android): https://vulkan.gpuinfo.org/displayreport.php?id=35046
  - Xclipse 940 (Android): https://vulkan.gpuinfo.org/displayreport.php?id=39529
  - Xclipse 920 (reporte Windows): https://vulkan.gpuinfo.org/displayreport.php?id=40913

## 2. Matriz por generacion

| Generacion Xclipse | SoC (ejemplo) | Base arquitectonica | Vulkan observado (muestras publicas) | Implicacion para ExynosTools |
|---|---|---|---|---|
| Xclipse 530 | Exynos 1480 | RDNA movil (Samsung + AMD) | Android con Vulkan 1.3.x, subgrupos 32..64, sin trayectoria BC nativa garantizada en todos los reportes | Mantener virtualizacion BCn y decode por compute como ruta principal |
| Xclipse 920 | Exynos 2200 | RDNA 2 | Reportes mixtos segun plataforma/driver (ej. Windows con BC expuesto) | No hardcodear BC por nombre de GPU; siempre sondeo por formato |
| Xclipse 940 | Exynos 2400 | RDNA 3 | Android con Vulkan 1.3.279 en reportes, Geometry/Tessellation y TFB disponibles en muestras | Mantener Geometry/Tessellation habilitado; no usar patchers que censuren capacidades |
| Xclipse 950 | Exynos 2500 | 4ta gen Xclipse (mas WGP/RB) | Hardware mas fuerte en raster/RT segun Samsung, pero comportamiento Vulkan depende de firmware | Reusar reglas runtime; no asumir compatibilidad BC fija por marketing |
| Xclipse 960 | Exynos 2600 | 5ta gen Xclipse (2nm GAA) | Mejoras de compute/RT anunciadas; perfil Vulkan final depende de version de driver Android | Misma estrategia: deteccion dinamica + telemetria por app |

## 3. Principio rector

No decidir por nombre de GPU.

Decidir por resultados reales de:
- `vkGetPhysicalDeviceFeatures2`
- `vkGetPhysicalDeviceFormatProperties2`
- `vkEnumerateDeviceExtensionProperties`
- limites de descriptor/pipeline/layout y colas disponibles

## 4. Reglas de decision para BCn

### Regla R1: Soporte BC por formato (no por bandera global)

Para cada formato BC objetivo (`BC1..BC7`, `BC4/BC5`, `BC6H`):
- Consultar `VkFormatProperties2`.
- Requerir al menos los bits necesarios para el uso real del juego (sampled/transfer/storage segun ruta).
- Si falta cualquier requisito critico: marcar formato como `virtualizado`.

### Regla R2: Politica de imagen virtualizada

Si el formato solicitado es BC virtualizado:
- `vkCreateImage`: sustituir a formato interno compatible (ej. `R8G8B8A8_*` o `R16G16B16A16_SFLOAT` para BC6H).
- Registrar mapeo:
  - imagen logica (solicitada por app)
  - imagen real (backend)
  - formato original solicitado
  - formato real

### Regla R3: Intercepcion de copias

Si destino es imagen virtualizada:
- Interceptar:
  - `vkCmdCopyBufferToImage`
  - `vkCmdCopyBufferToImage2`
  - `vkCmdCopyBufferToImage2KHR`
- Ruta:
  - no copiar BC comprimido directo a la imagen real
  - ejecutar decode compute
  - aplicar barreras/layouts correctas

Nota: `vkCmdCopyImage` puede ser necesario en ciertos motores/rutas de streaming. No es obligatorio para todos los juegos, pero es expansion recomendada para compatibilidad amplia.

### Regla R4: SRGB correcto

Si formato origen es BC SRGB:
- usar formato real SRGB cuando aplique (`R8G8B8A8_SRGB`)
- evitar doble conversion SRGB en shader + sampler

## 5. Reglas de concurrencia y memoria

### Regla C1: Pools de descriptor dinamicos

- No usar un unico pool fijo para toda la sesion.
- Escalar por bloques cuando `VK_ERROR_OUT_OF_POOL_MEMORY`.
- Liberacion segura por command buffer/pool.

### Regla C2: Staging seguro ante timeout

- Si `vkWaitForFences(..., timeout)` retorna `VK_TIMEOUT`, no reciclar recurso ocupado.
- Crear temporal de overflow.
- Liberar al completar fence/timeline.

### Regla C3: Hot path sin lock global largo

- Evitar mutex global en rutas de alta frecuencia (`CreateImage`, `CmdCopy*`).
- Preferir `shared_mutex` + secciones criticas cortas.

## 6. Reglas de shader y subgrupos (Wave32/Wave64)

- Xclipse moderno suele reportar subgrupos 32..64.
- Estrategia recomendada:
  - default: pipelines optimizados para Wave32
  - variante opcional Wave64 para casos concretos
  - seleccion por metricas reales (tiempo GPU y estabilidad), no por intuicion

## 7. Plan minimo de validacion por release

Por cada build candidata:
- Validacion API:
  - capas de validacion Vulkan sin errores criticos
- Casos funcionales:
  - juego con BCn pesado
  - juego con streaming agresivo
  - test 3D sintetico
- Metricas:
  - decode success/fallback/fail
  - eventos `VK_TIMEOUT`
  - agotamiento de descriptor pools

## 8. Conclusiones operativas

- La familia Xclipse es viable para una capa BCn robusta.
- La compatibilidad depende fuertemente del driver/firmware.
- El wrapper debe ser `driver-aware`, no `model-name-aware`.
- ExynosTools debe seguir una politica de deteccion runtime + virtualizacion selectiva + telemetria.

