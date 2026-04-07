#include "spirv_patcher.h"
#include <string.h>
#include <android/log.h>

#define SPIRV_TAG "ExynosSPIRV"
#define SPIRV_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPIRV_TAG, __VA_ARGS__)
#define SPIRV_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPIRV_TAG, __VA_ARGS__)

// ═══════════════════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════════════════

void exynos_spirv_init(ExynosSpirvPatcher* p) {
    if (!p) return;
    memset(p, 0, sizeof(ExynosSpirvPatcher));
    p->enabled = 1;
    p->verbose = 0;
    p->replace_opkill = 1;
    p->strip_unsupported_caps = 1;
    SPIRV_LOGI("SPIR-V Patcher V2.0 initialized (OpKill=%d, StripCaps=%d)",
               p->replace_opkill, p->strip_unsupported_caps);
}

// ═══════════════════════════════════════════════════════════════════════════
// SPIR-V instruction helpers
// ═══════════════════════════════════════════════════════════════════════════

// Each SPIR-V instruction: low 16 bits = opcode, high 16 bits = word count
static inline uint32_t spirv_opcode(uint32_t word) {
    return word & 0xFFFFu;
}

static inline uint32_t spirv_word_count(uint32_t word) {
    return word >> 16u;
}

static inline uint32_t spirv_make_instruction(uint32_t opcode, uint32_t wc) {
    return (wc << 16u) | (opcode & 0xFFFFu);
}

// ═══════════════════════════════════════════════════════════════════════════
// Core patching engine
// ═══════════════════════════════════════════════════════════════════════════

int exynos_spirv_patch(ExynosSpirvPatcher* p,
                       uint32_t* code,
                       uint32_t word_count) {
    if (!p || !p->enabled || !code || word_count < 5) {
        if (p) p->stats.shaders_passthrough++;
        return 0;
    }

    // Validate SPIR-V magic
    if (code[0] != SPIRV_MAGIC) {
        SPIRV_LOGW("Invalid SPIR-V magic: 0x%08x (expected 0x%08x)", code[0], SPIRV_MAGIC);
        p->stats.shaders_passthrough++;
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────
    // Phase 1: Pre-scan to detect if geometry/tessellation are ACTIVE
    //
    // OpEntryPoint format: [opcode|wc] [ExecutionModel] [ID] [name...]
    //   ExecutionModel 0 = Vertex, 1 = TessControl, 2 = TessEval,
    //                  3 = Geometry, 4 = Fragment, 5 = GLCompute
    // ─────────────────────────────────────────────────────────────────
    int has_geometry_entry = 0;
    int has_tess_entry = 0;
    int has_geometry_cap = 0;
    int has_tess_cap = 0;

    #define SPIRV_OP_ENTRY_POINT 15u

    uint32_t scan_pos = 5;
    while (scan_pos < word_count) {
        uint32_t instr = code[scan_pos];
        uint32_t op = spirv_opcode(instr);
        uint32_t wc = spirv_word_count(instr);
        if (wc == 0 || scan_pos + wc > word_count) break;

        if (op == SPIRV_OP_ENTRY_POINT && wc >= 3) {
            uint32_t exec_model = code[scan_pos + 1];
            if (exec_model == 3) has_geometry_entry = 1;  // Geometry
            if (exec_model == 1 || exec_model == 2) has_tess_entry = 1;  // TessCtrl/TessEval
        }
        if (op == SPIRV_OP_CAPABILITY && wc >= 2) {
            uint32_t cap = code[scan_pos + 1];
            if (cap == SPIRV_CAP_GEOMETRY) has_geometry_cap = 1;
            if (cap == SPIRV_CAP_TESSELLATION) has_tess_cap = 1;
        }
        scan_pos += wc;
    }

    int modified = 0;

    // SPIR-V header: [0]=magic, [1]=version, [2]=generator, [3]=bound, [4]=reserved
    uint32_t pos = 5;

    while (pos < word_count) {
        uint32_t instruction = code[pos];
        uint32_t op = spirv_opcode(instruction);
        uint32_t wc = spirv_word_count(instruction);

        // Safety: avoid infinite loop on corrupted SPIR-V
        if (wc == 0) {
            SPIRV_LOGW("SPIR-V corruption: zero word-count at position %u", pos);
            break;
        }
        if (pos + wc > word_count) {
            break; // instruction extends past buffer
        }

        p->stats.opcodes_scanned++;

        // ─────────────────────────────────────────────────────
        // Patch 1: OpKill -> OpDemoteToHelperInvocation
        // ─────────────────────────────────────────────────────
        if (op == SPIRV_OP_KILL && p->replace_opkill) {
            code[pos] = spirv_make_instruction(SPIRV_OP_DEMOTE, 1);
            p->stats.opkill_replaced++;
            modified = 1;

            if (p->verbose) {
                SPIRV_LOGI("Patched OpKill -> OpDemoteToHelperInvocation at word %u", pos);
            }
        }

        // ─────────────────────────────────────────────────────
        // Patch 2: SMART capability stripping
        //
        // Instead of blindly stripping, we now know if the
        // shader actively USES the capability (has entry point)
        // or just DECLARES it (passthrough / DXVK decoration).
        // ─────────────────────────────────────────────────────
        if (op == SPIRV_OP_CAPABILITY && p->strip_unsupported_caps && wc >= 2) {
            uint32_t cap_id = code[pos + 1];

            if (cap_id == SPIRV_CAP_GEOMETRY) {
                // Strip the capability
                for (uint32_t i = 0; i < wc; i++) {
                    code[pos + i] = spirv_make_instruction(SPIRV_OP_NOP, 1);
                }
                p->stats.capabilities_stripped++;
                p->stats.nop_injected += wc;
                modified = 1;

                if (has_geometry_entry) {
                    // Shader ACTIVELY uses geometry shaders — visual loss!
                    p->stats.geometry_active_stripped++;
                    SPIRV_LOGW("WARNING: Stripped ACTIVE Geometry capability — "
                               "visual effects (particles/hair/shadows) may be missing!");
                } else {
                    // Safe strip — shader only declares but never uses it
                    p->stats.geometry_passthrough++;
                    if (p->verbose) {
                        SPIRV_LOGI("Safe strip: Geometry capability declared but unused");
                    }
                }
            }
            else if (cap_id == SPIRV_CAP_TESSELLATION) {
                for (uint32_t i = 0; i < wc; i++) {
                    code[pos + i] = spirv_make_instruction(SPIRV_OP_NOP, 1);
                }
                p->stats.capabilities_stripped++;
                p->stats.nop_injected += wc;
                modified = 1;

                if (has_tess_entry) {
                    p->stats.tess_active_stripped++;
                    SPIRV_LOGW("WARNING: Stripped ACTIVE Tessellation capability — "
                               "terrain/water detail may be reduced!");
                } else {
                    p->stats.tess_passthrough++;
                    if (p->verbose) {
                        SPIRV_LOGI("Safe strip: Tessellation capability declared but unused");
                    }
                }
            }
            else if (cap_id == SPIRV_CAP_TRANSFORM_FEEDBACK) {
                for (uint32_t i = 0; i < wc; i++) {
                    code[pos + i] = spirv_make_instruction(SPIRV_OP_NOP, 1);
                }
                p->stats.capabilities_stripped++;
                p->stats.nop_injected += wc;
                modified = 1;

                if (p->verbose) {
                    SPIRV_LOGI("Stripped TransformFeedback capability (emulated by TFB module)");
                }
            }
        }

        // ─────────────────────────────────────────────────────
        // Patch 3: Strip OpExtension for unsupported extensions
        // ─────────────────────────────────────────────────────
        if (op == SPIRV_OP_EXTENSION && wc >= 2) {
            const char* ext_name = (const char*)&code[pos + 1];

            if (strstr(ext_name, "geometry") != NULL ||
                strstr(ext_name, "transform_feedback") != NULL ||
                strstr(ext_name, "tessellation") != NULL) {

                for (uint32_t i = 0; i < wc; i++) {
                    code[pos + i] = spirv_make_instruction(SPIRV_OP_NOP, 1);
                }
                p->stats.nop_injected += wc;
                modified = 1;

                if (p->verbose) {
                    SPIRV_LOGI("Stripped OpExtension '%s' at word %u", ext_name, pos);
                }
            }
        }

        pos += wc;
    }

    if (modified) {
        p->stats.shaders_patched++;
    } else {
        p->stats.shaders_passthrough++;
    }

    return modified;
}

// ═══════════════════════════════════════════════════════════════════════════
// Statistics
// ═══════════════════════════════════════════════════════════════════════════

void exynos_spirv_get_stats(const ExynosSpirvPatcher* p,
                            ExynosSpirvPatchStats* out_stats) {
    if (!p || !out_stats) return;
    memcpy(out_stats, &p->stats, sizeof(ExynosSpirvPatchStats));
}
