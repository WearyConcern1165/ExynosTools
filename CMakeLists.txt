cmake_minimum_required(VERSION 3.15)

project(ExynosTools LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(XCLIPSE_ENABLE_ANDROID_LOG "Enable Android logging" OFF)

# Shader embedding: compile GLSL to SPIR-V and convert to C headers
find_program(GLSLC glslc)
if(NOT GLSLC)
  find_program(GLSLANGVALIDATOR glslangValidator)
endif()
find_program(XXD xxd)

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(GLSL_SRC
  assets/shaders/src/bc4.comp
  assets/shaders/src/bc5.comp
)

set(EMBED_HEADERS)
foreach(src ${GLSL_SRC})
  get_filename_component(name ${src} NAME_WE)
  set(spv ${GENERATED_DIR}/${name}.spv)
  set(hdr ${GENERATED_DIR}/${name}_spv.h)
  if(GLSLC)
    add_custom_command(OUTPUT ${spv}
      COMMAND ${GLSLC} -O -mfmt=num -fshader-stage=compute ${CMAKE_SOURCE_DIR}/${src} -o ${spv}
      DEPENDS ${src}
      COMMENT "Compiling ${src} -> ${spv} (glslc)"
    )
  elseif(GLSLANGVALIDATOR)
    add_custom_command(OUTPUT ${spv}
      COMMAND ${GLSLANGVALIDATOR} -V -o ${spv} ${CMAKE_SOURCE_DIR}/${src}
      DEPENDS ${src}
      COMMENT "Compiling ${src} -> ${spv} (glslangValidator)"
    )
  else()
    message(FATAL_ERROR "No GLSL compiler found (glslc or glslangValidator)")
  endif()
  if(NOT XXD)
    message(FATAL_ERROR "xxd tool not found")
  endif()
  add_custom_command(OUTPUT ${hdr}
    COMMAND ${XXD} -i ${spv} > ${hdr}
    DEPENDS ${spv}
    COMMENT "Embedding ${spv} -> ${hdr}"
  )
  list(APPEND EMBED_HEADERS ${hdr})
endforeach()

add_custom_target(embedded_shaders DEPENDS ${EMBED_HEADERS})

add_library(xeno_wrapper SHARED
    src/xeno_wrapper.c
    src/bc_emulate.c
    src/features_patch.c
    src/detect.c
    src/perf_conf.c
    src/logging.c
    src/app_profile.c
)

add_dependencies(xeno_wrapper embedded_shaders)
target_include_directories(xeno_wrapper PRIVATE ${GENERATED_DIR})

target_compile_definitions(xeno_wrapper PRIVATE $<$<BOOL:${XCLIPSE_ENABLE_ANDROID_LOG}>:XCLIPSE_ANDROID_LOG>)

find_package(Threads REQUIRED)
if(UNIX)
    target_link_libraries(xeno_wrapper PRIVATE dl Threads::Threads)
endif()

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