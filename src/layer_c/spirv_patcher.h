#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// ExynosTools V2.0 — SPIR-V Dynamic Shader Patcher
//
// Scans and mutates SPIR-V bytecode at vkCreateShaderModule time to:
//   1) Replace OpKill with OpDemoteToHelperInvocation (RDNA crash fix)
//   2) Clamp NaN/Inf in constant pools (GPU hang prevention)
//   3) Remove unsupported capabilities/extensions gracefully
//   4) Patch known problematic shader hashes (game-specific fixes)
// ═══════════════════════════════════════════════════════════════════════════

// SPIR-V magic number and opcode constants
#define SPIRV_MAGIC          0x07230203u
#define SPIRV_OP_NOP         0u
#define SPIRV_OP_CAPABILITY  17u
#define SPIRV_OP_EXTENSION   10u
#define SPIRV_OP_KILL        252u
#define SPIRV_OP_DEMOTE      5765u  // OpDemoteToHelperInvocation (SPV_EXT_demote_to_helper_invocation)
#define SPIRV_OP_BRANCH      249u
#define SPIRV_OP_UNREACHABLE 255u

// Capability IDs we may need to strip
#define SPIRV_CAP_GEOMETRY           2u
#define SPIRV_CAP_TESSELLATION       3u
#define SPIRV_CAP_TRANSFORM_FEEDBACK 53u

// Patch result stats
typedef struct ExynosSpirvPatchStats {
    uint32_t opcodes_scanned;
    uint32_t opkill_replaced;
    uint32_t capabilities_stripped;
    uint32_t nop_injected;
    uint32_t shaders_patched;
    uint32_t shaders_passthrough;  // not modified
    uint32_t geometry_passthrough; // geometry cap declared but unused (safe strip)
    uint32_t geometry_active_stripped; // geometry cap WAS in use (visual loss warning)
    uint32_t tess_passthrough;
    uint32_t tess_active_stripped;
} ExynosSpirvPatchStats;

// Global patcher state
typedef struct ExynosSpirvPatcher {
    int      enabled;
    int      verbose;
    int      replace_opkill;       // Replace OpKill -> OpDemoteToHelperInvocation
    int      strip_unsupported_caps; // Remove Geometry/Tessellation capabilities
    ExynosSpirvPatchStats stats;
} ExynosSpirvPatcher;

/// Initialize the patcher with default settings.
void exynos_spirv_init(ExynosSpirvPatcher* p);

/// Patch a SPIR-V binary in-place.
/// Returns 1 if the shader was modified, 0 if passed through unchanged.
/// The caller must provide a WRITABLE copy of the SPIR-V code.
int exynos_spirv_patch(ExynosSpirvPatcher* p,
                       uint32_t* code,
                       uint32_t word_count);

/// Get accumulated patch statistics.
void exynos_spirv_get_stats(const ExynosSpirvPatcher* p,
                            ExynosSpirvPatchStats* out_stats);

#ifdef __cplusplus
}
#endif
