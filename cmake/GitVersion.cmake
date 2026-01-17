function(get_version_from_git)
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    if(NOT GIT_TAG)
        set(GIT_TAG "v0.0.0-unknown")
    endif()

    # Strip leading 'v' if present
    string(REGEX REPLACE "^v" "" CLEAN_TAG "${GIT_TAG}")

    # Regex to parse Semantic Versioning: Major.Minor.Patch[-Suffix]
    # Matches: 1.0.0, 1.0.0-beta, 1.0.0-4-g123456
    if(CLEAN_TAG MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(.*)?$")
        set(ver_major ${CMAKE_MATCH_1})
        set(ver_minor ${CMAKE_MATCH_2})
        set(ver_patch ${CMAKE_MATCH_3})
        set(ver_suffix ${CMAKE_MATCH_4})
    else()
        # Fallback for non-semver tags
        set(ver_major 0)
        set(ver_minor 0)
        set(ver_patch 0)
        set(ver_suffix "-${CLEAN_TAG}")
    endif()

    # Export variables to parent scope
    set(PROJECT_VERSION_MAJOR ${ver_major} PARENT_SCOPE)
    set(PROJECT_VERSION_MINOR ${ver_minor} PARENT_SCOPE)
    set(PROJECT_VERSION_PATCH ${ver_patch} PARENT_SCOPE)
    set(PROJECT_VERSION_SUFFIX ${ver_suffix} PARENT_SCOPE)
    set(PROJECT_VERSION "${ver_major}.${ver_minor}.${ver_patch}${ver_suffix}" PARENT_SCOPE)
    set(GIT_COMMIT_HASH "${GIT_TAG}" PARENT_SCOPE) # Use full tag as "hash/desc" for now
endfunction()
