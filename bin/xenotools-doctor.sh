#!/usr/bin/env bash
set -euo pipefail

echo "== ExynosTools Doctor =="
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH:-}"

echo "-- Looking for Vulkan loader --"
for n in libvulkan.so.1 libvulkan.so; do
  if ldconfig -p | grep -q "$n"; then echo "found $n in ldconfig"; fi
  if ldd /bin/true 2>/dev/null | grep -q "$n"; then echo "ldd sees $n"; fi
done

echo "-- Zink / Lavapipe check --"
if ls /usr/share/vulkan/icd.d/*zink*.json >/dev/null 2>&1; then echo "Zink ICD present"; else echo "Zink ICD not found"; fi
if ls /usr/share/vulkan/icd.d/*lvp*.json >/dev/null 2>&1; then echo "Lavapipe ICD present"; else echo "Lavapipe ICD not found"; fi

echo "-- Profiles --"
ls -l profiles/winlator || true

echo "-- Config --"
cat etc/exynostools/performance_mode.conf || true

echo "-- Shaders --"
ls -l assets/shaders/decode || true

echo "== Done =="

