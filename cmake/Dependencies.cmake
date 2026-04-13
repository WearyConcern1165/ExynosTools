function(exynos_require_submodule path label)
    if(NOT EXISTS "${path}")
        message(WARNING
            "${label} not found at ${path}.\n"
            "Run: git submodule update --init --recursive"
        )
    endif()
endfunction()

function(exynos_require_path path label)
    if(NOT EXISTS "${path}")
        message(WARNING
            "${label} not found at ${path}.\n"
            "Check EXYNOS_VULKAN_REPOS_ROOT or unpack vulkan_repositories.zip again."
        )
    endif()
endfunction()
