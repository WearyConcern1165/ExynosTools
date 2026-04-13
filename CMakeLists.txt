cmake_minimum_required(VERSION 3.22)
project(ExynosToolsLayer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EXYNOS_LAYER_BUILD_SHADERS "Build SPIR-V from GLSL shaders." ON)
option(EXYNOS_LAYER_EMBED_SHADERS "Embed compiled SPIR-V into the layer shared library." ON)
option(EXYNOS_LAYER_USE_SUBMODULE_DEPS "Use dependencies from external/ submodules." OFF)
option(EXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS "Use dependencies from local vulkan_repositories extract." ON)

set(EXYNOS_VULKAN_REPOS_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../vulkan_repositories_extracted/vulkan_repos"
    CACHE PATH
    "Path to extracted vulkan_repositories/vulkan_repos directory."
)
set(EXYNOS_VULKAN_HEADERS_PATH
    ""
    CACHE PATH
    "Optional path to Vulkan-Headers repository root (contains include/vulkan/vulkan.h)."
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

if(EXYNOS_LAYER_USE_SUBMODULE_DEPS)
    exynos_require_submodule("${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers" "Vulkan-Headers")
    exynos_require_submodule("${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Utility-Libraries" "Vulkan-Utility-Libraries")
    exynos_require_submodule("${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator" "VulkanMemoryAllocator (VMA)")
endif()

if(EXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS)
    exynos_require_path("${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-ExtensionLayer" "Vulkan-ExtensionLayer")
    exynos_require_path("${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-Loader" "Vulkan-Loader")
    exynos_require_path("${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-Utility-Libraries" "Vulkan-Utility-Libraries")
    exynos_require_path("${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-ValidationLayers" "Vulkan-ValidationLayers")
endif()

set(EXYNOS_VULKAN_INCLUDE_CANDIDATES "")
if(EXYNOS_LAYER_USE_SUBMODULE_DEPS)
    list(APPEND EXYNOS_VULKAN_INCLUDE_CANDIDATES
        "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers/include"
    )
endif()
if(EXYNOS_VULKAN_HEADERS_PATH)
    list(APPEND EXYNOS_VULKAN_INCLUDE_CANDIDATES
        "${EXYNOS_VULKAN_HEADERS_PATH}/include"
        "${EXYNOS_VULKAN_HEADERS_PATH}"
    )
endif()
if(DEFINED ENV{VULKAN_SDK})
    list(APPEND EXYNOS_VULKAN_INCLUDE_CANDIDATES
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
    )
endif()

find_path(EXYNOS_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h vulkan/vk_layer.h
    PATHS ${EXYNOS_VULKAN_INCLUDE_CANDIDATES}
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
if(NOT EXYNOS_VULKAN_INCLUDE_DIR)
    find_path(EXYNOS_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h vulkan/vk_layer.h)
endif()
if(NOT EXYNOS_VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR
        "Could not find Vulkan headers (vulkan/vulkan.h).\n"
        "Install Vulkan SDK or provide -DEXYNOS_VULKAN_HEADERS_PATH=<Vulkan-Headers root>."
    )
endif()

set(EXYNOS_VULKAN_UTILITY_INCLUDE_DIR "")
if(EXYNOS_LAYER_USE_SUBMODULE_DEPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Utility-Libraries/include")
    set(EXYNOS_VULKAN_UTILITY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Utility-Libraries/include")
elseif(EXYNOS_LAYER_USE_LOCAL_VULKAN_REPOS AND EXISTS "${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-Utility-Libraries/include")
    set(EXYNOS_VULKAN_UTILITY_INCLUDE_DIR "${EXYNOS_VULKAN_REPOS_ROOT}/Vulkan-Utility-Libraries/include")
endif()

if(ANDROID)
    find_library(ANDROID_LOG_LIB log REQUIRED)
    find_library(ANDROID_VULKAN_LIB vulkan REQUIRED)
endif()

add_library(VkLayer_ExynosTools SHARED
    src/layer/layer_entry.cpp
)

set_target_properties(VkLayer_ExynosTools PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
if((CMAKE_SYSTEM_NAME STREQUAL "Android" OR (UNIX AND NOT APPLE)) AND
   CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_link_options(VkLayer_ExynosTools PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    )
endif()

target_include_directories(VkLayer_ExynosTools PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${EXYNOS_VULKAN_INCLUDE_DIR}"
)
if(EXYNOS_VULKAN_UTILITY_INCLUDE_DIR)
    target_include_directories(VkLayer_ExynosTools PRIVATE "${EXYNOS_VULKAN_UTILITY_INCLUDE_DIR}")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/include")
    target_include_directories(VkLayer_ExynosTools PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanMemoryAllocator/include"
    )
endif()

if(ANDROID)
    target_link_libraries(VkLayer_ExynosTools PRIVATE
        ${ANDROID_VULKAN_LIB}
        ${ANDROID_LOG_LIB}
    )
else()
    find_package(Vulkan REQUIRED)
    target_link_libraries(VkLayer_ExynosTools PRIVATE Vulkan::Vulkan)
endif()

set(LAYER_MANIFEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_exynostools.json")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_exynostools.json.in"
    "${LAYER_MANIFEST_OUT}"
    @ONLY
)

if(EXYNOS_LAYER_BUILD_SHADERS)
    find_program(GLSLC glslc)
    if(GLSLC)
        set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

        set(SHADER_SRCS
            shaders/bc6.comp
            shaders/bc6_iv.comp
            shaders/bc7.comp
            shaders/bc7_iv.comp
            shaders/copy_image_iv.comp
            shaders/rgtc.comp
            shaders/rgtc_iv.comp
            shaders/s3tc.comp
            shaders/s3tc_iv.comp
        )

        set(SHADER_SPVS "")
        foreach(shader IN LISTS SHADER_SRCS)
            get_filename_component(shader_name "${shader}" NAME)
            set(spv "${SHADER_OUT_DIR}/${shader_name}.spv")
            add_custom_command(
                OUTPUT "${spv}"
                COMMAND "${GLSLC}"
                    -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
                    "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
                    -o "${spv}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
                VERBATIM
            )
            list(APPEND SHADER_SPVS "${spv}")
        endforeach()

        add_custom_target(exynostools_shaders ALL DEPENDS ${SHADER_SPVS})

        if(EXYNOS_LAYER_EMBED_SHADERS)
            set(EMBEDDED_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/exynostools_embedded_spirv.h")
            add_custom_command(
                OUTPUT "${EMBEDDED_SHADER_HEADER}"
                COMMAND "${CMAKE_COMMAND}"
                    -DOUTPUT_FILE=${EMBEDDED_SHADER_HEADER}
                    -DSPV_FILES=${SHADER_SPVS}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSpirvHeader.cmake"
                DEPENDS
                    ${SHADER_SPVS}
                    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSpirvHeader.cmake"
                VERBATIM
            )
            add_custom_target(exynostools_embedded_shaders ALL DEPENDS "${EMBEDDED_SHADER_HEADER}")
            add_dependencies(VkLayer_ExynosTools exynostools_embedded_shaders)
            target_include_directories(VkLayer_ExynosTools PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        endif()
    else()
        if(EXYNOS_LAYER_EMBED_SHADERS)
            message(FATAL_ERROR
                "glslc not found, but EXYNOS_LAYER_EMBED_SHADERS=ON.\n"
                "Install Vulkan SDK (glslc) or disable EXYNOS_LAYER_EMBED_SHADERS."
            )
        else()
            message(WARNING "glslc not found. Shader compilation target disabled.")
        endif()
    endif()
elseif(EXYNOS_LAYER_EMBED_SHADERS)
    message(FATAL_ERROR
        "EXYNOS_LAYER_EMBED_SHADERS=ON requires EXYNOS_LAYER_BUILD_SHADERS=ON."
    )
endif()
