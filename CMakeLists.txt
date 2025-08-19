cmake_minimum_required(VERSION 3.15)

project(ExynosTools LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(XCLIPSE_ENABLE_ANDROID_LOG "Enable Android logging" OFF)

# Find required tools for shader compilation
find_program(GLSLC_EXECUTABLE glslc REQUIRED)
find_program(XXD_EXECUTABLE xxd REQUIRED)

# Shader compilation function
function(compile_shader_to_header SHADER_SOURCE HEADER_OUTPUT VARIABLE_NAME)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
    set(SPIRV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    
    # Create shaders directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
    
    # Compile GLSL to SPIR-V
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE} -I${CMAKE_SOURCE_DIR}/assets/shaders/src ${SHADER_SOURCE} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER_SOURCE} ${CMAKE_SOURCE_DIR}/assets/shaders/src/bc_common.glsl
        COMMENT "Compiling ${SHADER_SOURCE} to SPIR-V"
    )
    
    # Convert SPIR-V to C header
    add_custom_command(
        OUTPUT ${HEADER_OUTPUT}
        COMMAND ${XXD_EXECUTABLE} -i ${SPIRV_OUTPUT} > ${HEADER_OUTPUT}
        COMMAND sed -i "s/unsigned char/static const unsigned char/g" ${HEADER_OUTPUT}
        COMMAND sed -i "s/${SHADER_NAME}_spv/${VARIABLE_NAME}/g" ${HEADER_OUTPUT}
        DEPENDS ${SPIRV_OUTPUT}
        COMMENT "Converting ${SPIRV_OUTPUT} to C header"
    )
endfunction()

# Compile shaders to headers
set(SHADER_HEADERS_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY ${SHADER_HEADERS_DIR})

compile_shader_to_header(
    "${CMAKE_SOURCE_DIR}/assets/shaders/src/bc4.comp"
    "${SHADER_HEADERS_DIR}/bc4_shader.h"
    "bc4_shader_spv"
)

compile_shader_to_header(
    "${CMAKE_SOURCE_DIR}/assets/shaders/src/bc5.comp"
    "${SHADER_HEADERS_DIR}/bc5_shader.h"
    "bc5_shader_spv"
)

add_library(xeno_wrapper SHARED
    src/xeno_wrapper.c
    src/bc_emulate.c
    src/features_patch.c
    src/detect.c
    src/perf_conf.c
    src/logging.c
    src/app_profile.c
    src/hud.c
    ${SHADER_HEADERS_DIR}/bc4_shader.h
    ${SHADER_HEADERS_DIR}/bc5_shader.h
)

target_compile_definitions(xeno_wrapper PRIVATE $<$<BOOL:${XCLIPSE_ENABLE_ANDROID_LOG}>:XCLIPSE_ANDROID_LOG>)

find_package(Threads REQUIRED)
if(UNIX)
    target_link_libraries(xeno_wrapper PRIVATE dl Threads::Threads)
endif()

# Add include directory for generated shader headers
target_include_directories(xeno_wrapper PRIVATE ${SHADER_HEADERS_DIR})

set_target_properties(xeno_wrapper PROPERTIES
    OUTPUT_NAME "xeno_wrapper"
    PREFIX "lib"
)

install(TARGETS xeno_wrapper
    LIBRARY DESTINATION usr/lib
)

install(FILES usr/share/meta.json DESTINATION usr/share)
install(FILES etc/exynostools/performance_mode.conf DESTINATION etc/exynostools)
install(DIRECTORY etc/exynostools/profiles DESTINATION etc/exynostools)