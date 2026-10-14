#!/usr/bin/env python3
"""Build standalone regression tests; optionally run on an authorized device.

No GPU, emulator, ROM, driver installation or global device changes required.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--headers', type=Path, required=True, help='Vulkan-Headers repository root')
    parser.add_argument('--output', type=Path, required=True, help='New output directory')
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', help='Omit to build only; requires a free authorized Android device')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    layer = root / 'src/layer'
    host = 'windows-x86_64' if sys.platform == 'win32' else 'linux-x86_64'
    compiler = args.ndk / 'toolchains/llvm/prebuilt' / host / 'bin' / (
        'clang++.exe' if sys.platform == 'win32' else 'clang++')
    if not compiler.is_file() or not (args.headers / 'include/vulkan/vulkan.h').is_file():
        parser.error('Supply the Android NDK and Vulkan-Headers before running')
    args.output.mkdir(parents=True, exist_ok=False)
    tests = {
        'resource_identity': [],
        'bcn_cpu_decoder': [layer / 'layer_bcn_cpu_decoder.cpp'],
        'layer_settings': [layer / 'layer_settings_runtime.cpp', layer / 'layer_settings_utils.cpp'],
    }
    for name, sources in tests.items():
        binary = args.output.resolve() / name
        command = [str(compiler), '--target=aarch64-linux-android30', '-std=c++17', '-O2',
            '-static-libstdc++', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            '-Wl,-z,max-page-size=16384', '-I', str(layer), '-I', str(args.headers / 'include'),
            '-I', str(root / 'external/VulkanMemoryAllocator/include'),
            '-I', str(root / 'third_party/bcdec'), str(root / 'tests' / (name + '_test.cpp')),
            *map(str, sources), '-llog', '-o', str(binary)]
        with (args.output / (name + '-build.txt')).open('wb') as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        if not args.serial:
            continue
        adb = [args.adb, '-s', args.serial]
        remote = '/data/local/tmp/exynostools-test-' + uuid.uuid4().hex
        try:
            subprocess.run(adb + ['push', str(binary), remote], check=True, capture_output=True)
            subprocess.run(adb + ['shell', 'chmod', '700', remote], check=True, capture_output=True)
            result = subprocess.run(adb + ['shell', remote], capture_output=True, timeout=30)
            (args.output / (name + '-result.txt')).write_bytes(result.stdout + result.stderr)
            print(result.stdout.decode('utf-8', errors='replace').strip())
            if result.returncode:
                raise SystemExit(name + ' failed; see the preserved result')
        finally:
            subprocess.run(adb + ['shell', 'rm', '-f', remote], capture_output=True)


if __name__ == '__main__':
    main()
