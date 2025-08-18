cmake_minimum_required(VERSION 3.15)

project(ExynosTools LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(XCLIPSE_ENABLE_ANDROID_LOG "Enable Android logging" OFF)

add_library(xeno_wrapper SHARED
    src/xeno_wrapper.c
    src/bc_emulate.c
    src/features_patch.c
    src/detect.c
    src/perf_conf.c
)

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


