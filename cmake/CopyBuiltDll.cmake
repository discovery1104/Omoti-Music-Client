if(NOT DEFINED SRC OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "SRC and OUTDIR are required")
endif()

if(NOT DEFINED CONFIG_NAME)
    set(CONFIG_NAME "Release")
endif()

if(NOT DEFINED VERSION_BASE)
    message(FATAL_ERROR "VERSION_BASE is required")
endif()

if(NOT DEFINED DEBUG_BASENAME)
    set(DEBUG_BASENAME "OmotiMusicClient_debug")
endif()

if(NOT DEFINED RELEASE_ROOT)
    set(RELEASE_ROOT "${OUTDIR}/releases")
endif()

if(NOT DEFINED PATCH_WIDTH)
    set(PATCH_WIDTH "3")
endif()

if(NOT DEFINED LATEST_NAME)
    set(LATEST_NAME "latite.dll")
endif()

string(REGEX REPLACE "^v" "" VERSION_BASE_NORMALIZED "${VERSION_BASE}")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _ "${VERSION_BASE_NORMALIZED}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "VERSION_BASE must look like v2.6.2 or 2.6.2")
endif()

set(VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VERSION_MINOR "${CMAKE_MATCH_2}")
set(VERSION_PATCH "${CMAKE_MATCH_3}")

file(MAKE_DIRECTORY "${OUTDIR}")

function(pad_number out_var value width)
    set(result "${value}")
    string(LENGTH "${result}" result_len)
    while(result_len LESS width)
        set(result "0${result}")
        string(LENGTH "${result}" result_len)
    endwhile()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(copy_if_possible src dst)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
        RESULT_VARIABLE copy_result
        ERROR_VARIABLE copy_error
    )

    if(NOT copy_result EQUAL 0)
        message(WARNING "Copy skipped for ${dst}: ${copy_error}")
    else()
        message(STATUS "Copied Omoti DLL to ${dst}")
    endif()
endfunction()

set(latest_dst "${OUTDIR}/${LATEST_NAME}")
copy_if_possible("${SRC}" "${latest_dst}")

if(CONFIG_NAME STREQUAL "Release")
    file(MAKE_DIRECTORY "${RELEASE_ROOT}")

    file(GLOB existing_release_dirs LIST_DIRECTORIES true "${RELEASE_ROOT}/v${VERSION_MAJOR}.${VERSION_MINOR}.*")
    set(max_release_patch -1)

    foreach(path IN LISTS existing_release_dirs)
        get_filename_component(name "${path}" NAME)
        string(REGEX MATCH "^v${VERSION_MAJOR}\\.${VERSION_MINOR}\\.([0-9]+)$" _ "${name}")
        if(CMAKE_MATCH_1)
            math(EXPR parsed_patch "${CMAKE_MATCH_1}")
            if(parsed_patch GREATER max_release_patch)
                set(max_release_patch "${parsed_patch}")
            endif()
        endif()
    endforeach()

    if(max_release_patch LESS 0)
        set(next_release_patch "${VERSION_PATCH}")
    elseif(max_release_patch LESS VERSION_PATCH)
        set(next_release_patch "${VERSION_PATCH}")
    else()
        math(EXPR next_release_patch "${max_release_patch} + 1")
    endif()

    set(release_version "v${VERSION_MAJOR}.${VERSION_MINOR}.${next_release_patch}")
    set(release_dir "${RELEASE_ROOT}/${release_version}")
    file(MAKE_DIRECTORY "${release_dir}")
    copy_if_possible("${SRC}" "${release_dir}/${LATEST_NAME}")

    message(STATUS "Release output folder: ${release_dir}")
else()
    file(GLOB existing_debug_dlls "${OUTDIR}/${DEBUG_BASENAME}-v${VERSION_MAJOR}.*.dll")
    set(max_debug_patch -1)

    foreach(path IN LISTS existing_debug_dlls)
        get_filename_component(name "${path}" NAME)
        string(REGEX MATCH "^${DEBUG_BASENAME}-v${VERSION_MAJOR}\\.([0-9]+)\\.dll$" _ "${name}")
        if(CMAKE_MATCH_1)
            math(EXPR parsed_patch "${CMAKE_MATCH_1}")
            if(parsed_patch GREATER max_debug_patch)
                set(max_debug_patch "${parsed_patch}")
            endif()
        endif()
    endforeach()

    if(max_debug_patch LESS 0)
        set(next_debug_patch 0)
    else()
        math(EXPR next_debug_patch "${max_debug_patch} + 1")
    endif()

    pad_number(debug_patch_padded "${next_debug_patch}" "${PATCH_WIDTH}")
    set(debug_name "${DEBUG_BASENAME}-v${VERSION_MAJOR}.${debug_patch_padded}.dll")
    copy_if_possible("${SRC}" "${OUTDIR}/${debug_name}")

    message(STATUS "Debug output file: ${OUTDIR}/${debug_name}")
endif()
