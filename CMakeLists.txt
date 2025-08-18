cmake_minimum_required(VERSION 3.15)

project(xclipse_tools_wrapper LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(XCLIPSE_ENABLE_ANDROID_LOG "Enable Android logging" OFF)

add_library(xclipse_wrapper SHARED
    src/xclipse_wrapper.c
)

target_compile_definitions(xclipse_wrapper PRIVATE $<$<BOOL:${XCLIPSE_ENABLE_ANDROID_LOG}>:XCLIPSE_ANDROID_LOG>)

if(UNIX)
    target_link_libraries(xclipse_wrapper PRIVATE dl)
endif()

set_target_properties(xclipse_wrapper PROPERTIES
    OUTPUT_NAME "xclipse_wrapper"
    PREFIX "lib"
)

install(TARGETS xclipse_wrapper
    LIBRARY DESTINATION usr/lib
)

