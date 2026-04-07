# CMake helper to compile GLSL compute shaders to SPIR-V.
#
# Usage:
#   include(cmake/spirv.cmake)
#   compile_shader(
#       SOURCE  shaders/bc5_decode.comp
#       OUTPUT  ${CMAKE_BINARY_DIR}/shaders/bc5_decode.spv
#   )
#
# Requires glslc to be on PATH (ships with the Android NDK / Vulkan SDK).

function(compile_shader)
    cmake_parse_arguments(SHADER "" "SOURCE;OUTPUT" "" ${ARGN})

    if(NOT SHADER_SOURCE)
        message(FATAL_ERROR "compile_shader: SOURCE not specified")
    endif()
    if(NOT SHADER_OUTPUT)
        message(FATAL_ERROR "compile_shader: OUTPUT not specified")
    endif()

    # Find glslc – NDK ships it, Vulkan SDK also provides it.
    find_program(GLSLC_EXECUTABLE glslc
        HINTS
            $ENV{VULKAN_SDK}/bin
            $ENV{ANDROID_NDK}/shader-tools
    )
    if(NOT GLSLC_EXECUTABLE)
        message(WARNING "glslc not found – SPIR-V shaders will NOT be compiled. "
                        "Set VULKAN_SDK or ensure glslc is on PATH.")
        return()
    endif()

    get_filename_component(SHADER_OUTPUT_DIR "${SHADER_OUTPUT}" DIRECTORY)

    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND ${GLSLC_EXECUTABLE}
                -fshader-stage=compute
                --target-env=vulkan1.1
                -O
                "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
                -o "${SHADER_OUTPUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
        COMMENT "Compiling ${SHADER_SOURCE} -> SPIR-V"
        VERBATIM
    )
endfunction()
