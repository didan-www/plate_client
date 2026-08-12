function(plate_client_apply_project_options target)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(
        ${target}
        PRIVATE
            PLATE_CLIENT_VERSION="${PROJECT_VERSION}"
            PLATE_CLIENT_BUILD_DATE="${PLATE_CLIENT_BUILD_DATE}"
    )

    if(MINGW)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
