cmake_minimum_required(VERSION 3.18)
project(xclipse-bcn-decoder
    VERSION     0.1.0
    LANGUAGES   C CXX
    DESCRIPTION "BCn texture fallback decoder via compute shaders for Samsung Xclipse GPUs"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ANDROID_STL c++_static)
# Compile GLSL shader to SPIR-V and convert it to C++ header
macro(add_shader_target SHADER_NAME SHADER_VAR)
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv"
        COMMAND glslc -fshader-stage=compute --target-env=vulkan1.1 -O "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}.comp" -o "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}.comp"
        VERBATIM
    )
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_spv.h"
        COMMAND ${CMAKE_COMMAND} -D "INPUT_FILE=${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv"
                                 -D "VAR_NAME=${SHADER_VAR}"
                                 -D "OUTPUT_FILE=${SHADER_OUTPUT_DIR}/${SHADER_NAME}_spv.h"
                                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2h.cmake"
        DEPENDS "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2h.cmake"
    )
    list(APPEND SHADER_HEADERS "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_spv.h")
endmacro()

set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BCN_BUILD_TESTS "Build test executables" ON)

# ---------------------------------------------------------------------------
# Vulkan dependency
# ---------------------------------------------------------------------------
# On Android NDK the Vulkan headers are provided by the NDK itself.
# On desktop (for cross-compilation or validation) we use the Vulkan SDK.
if(ANDROID)
    # NDK ≥ r25 provides Vulkan 1.1 headers and libvulkan.so.
    find_library(VULKAN_LIB vulkan)
    if(NOT VULKAN_LIB)
        message(FATAL_ERROR "libvulkan not found.  Use NDK r25+ with Vulkan support.")
    endif()
    set(VULKAN_LIBRARIES ${VULKAN_LIB})
    set(VULKAN_INCLUDE_DIRS "")  # NDK headers are already on the include path
else()
    find_package(Vulkan REQUIRED)
    set(VULKAN_LIBRARIES Vulkan::Vulkan)
    set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
endif()

# Custom target to build shaders
# Generates SPIR-V binary from GLSL
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

add_shader_target(bc6 BC6_SPV)
add_shader_target(bc6_iv BC6_IV_SPV)
add_shader_target(bc7 BC7_SPV)
add_shader_target(bc7_iv BC7_IV_SPV)
add_shader_target(rgtc RGTC_SPV)
add_shader_target(rgtc_iv RGTC_IV_SPV)
add_shader_target(s3tc S3TC_SPV)
add_shader_target(s3tc_iv S3TC_IV_SPV)

add_custom_target(bcn_shaders ALL
    DEPENDS ${SHADER_HEADERS}
)

# ---------------------------------------------------------------------------
# Library sources
# ---------------------------------------------------------------------------
set(BCN_SOURCES
    src/core/decoder_context.cpp
    src/core/format_support.cpp
    src/core/vulkan_bridge.cpp
    src/compute/compute_pipeline.cpp
    src/compute/bc5_decoder.cpp
    src/compute/bc7_decoder.cpp
    src/native/native_upload.cpp
    src/memory/texture_cache.cpp
    src/memory/staging_buffer_pool.cpp
    src/vulkan/vk_utils.cpp
    src/vulkan/vk_buffer.cpp
    src/vulkan/vk_image.cpp
)

add_library(bcn_decoder SHARED ${BCN_SOURCES})

target_include_directories(bcn_decoder
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SHADER_OUTPUT_DIR}
        ${VULKAN_INCLUDE_DIRS}
)

target_link_libraries(bcn_decoder
    PRIVATE
        ${VULKAN_LIBRARIES}
)

if(ANDROID)
    target_link_libraries(bcn_decoder PRIVATE log android)
endif()

add_dependencies(bcn_decoder bcn_shaders)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BCN_BUILD_TESTS)
    add_executable(test_format_support tests/test_format_support.cpp)
    target_include_directories(test_format_support PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${VULKAN_INCLUDE_DIRS}
    )
    target_link_libraries(test_format_support PRIVATE bcn_decoder ${VULKAN_LIBRARIES})
endif()

if(ANDROID)
    add_library(vulkan_xclipse SHARED
        # ═══════════════════════════════════════════════════════════
        # ICD Wrapper (Vulkan entry points + hooks)
        # ═══════════════════════════════════════════════════════════
        vulkan_wrapper.cpp
        bcn_decoder.cpp

        # ═══════════════════════════════════════════════════════════
        # FULL C++ BCN Decoder Engine (V1.1 Professional core)
        # These files are what made V1.1 weigh ~2MB and WORK.
        # Without them, BCn decode shaders are NOT embedded.
        # ═══════════════════════════════════════════════════════════
        src/core/decoder_context.cpp
        src/core/format_support.cpp
        src/compute/compute_pipeline.cpp
        src/compute/bc5_decoder.cpp
        src/compute/bc7_decoder.cpp
        src/native/native_upload.cpp
        src/memory/texture_cache.cpp
        src/memory/staging_buffer_pool.cpp
        src/vulkan/vk_utils.cpp
        src/vulkan/vk_buffer.cpp
        src/vulkan/vk_image.cpp

        # ═══════════════════════════════════════════════════════════
        # C Layer modules (V1.6+ features)
        # ═══════════════════════════════════════════════════════════
        src/layer_c/exynostools_hashmap.c
        src/layer_c/exynostools_bcn_dispatch.c
        src/layer_c/staging_pool.c
        src/layer_c/mipmap.c
        src/layer_c/exynostools_config.c
        src/layer_c/pipeline_cache.c
        src/layer_c/watchdog.c
        src/layer_c/micro_vma.c
        src/layer_c/ext_emulation.c
        src/layer_c/spirv_patcher.c
        src/layer_c/tfb_emulator.c
        src/layer_c/query_interceptor.c
    )
    target_include_directories(vulkan_xclipse PRIVATE
        ${VULKAN_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/layer_c
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SHADER_OUTPUT_DIR}
    )
    target_link_libraries(vulkan_xclipse PRIVATE
        ${VULKAN_LIBRARIES}
        log
        dl
    )
    add_dependencies(vulkan_xclipse bcn_shaders)
    target_link_options(vulkan_xclipse PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    )
    set_target_properties(vulkan_xclipse PROPERTIES
        OUTPUT_NAME vulkan_xclipse
    )

    add_library(exynostools_layer SHARED
        src/core/vulkan_bridge.cpp
    )
    target_include_directories(exynostools_layer PRIVATE
        ${VULKAN_INCLUDE_DIRS}
    )
    target_link_libraries(exynostools_layer PRIVATE
        log
    )
    set_target_properties(exynostools_layer PROPERTIES
        OUTPUT_NAME VkLayer_EXYNOSTOOLS_bcn_fix
    )

    add_library(exynostools_layer_c SHARED
        src/layer_c/exynostools_layer.c
        src/layer_c/exynostools_hashmap.c
        src/layer_c/exynostools_bcn_dispatch.c
        src/layer_c/staging_pool.c
        src/layer_c/mipmap.c
    )
    target_include_directories(exynostools_layer_c PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/layer_c
        ${VULKAN_INCLUDE_DIRS}
    )
    target_link_libraries(exynostools_layer_c PRIVATE
        ${VULKAN_LIBRARIES}
        log
    )
    set_target_properties(exynostools_layer_c PROPERTIES
        OUTPUT_NAME VkLayer_exynostools_bcn_decode
    )
endif()
