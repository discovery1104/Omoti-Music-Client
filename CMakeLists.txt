cmake_minimum_required(VERSION 3.22)

project(Omoti VERSION 1.0)

if (NOT MSVC)
    message(FATAL_ERROR "Compiler unsupported, must use MSVC")
endif ()

find_program(DOTNET_EXE dotnet REQUIRED)

# Setup build type macros

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DOmoti_DEBUG)
endif()

# Add all sources in src/* and deps/*
file(GLOB_RECURSE Omoti_SRC CONFIGURE_DEPENDS
        "src/*.h"
        "src/*.cpp"

        "deps/*.h"
        "deps/*.cpp"
)

# Enable Hot Reload for MSVC compilers if supported
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif ()

# MSVC library release ABI compatibility with game
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Assets
set(MINGW_LD
    "C:/Users/DISBOY/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin/ld.exe"
    CACHE FILEPATH "Path to MinGW ld.exe for embedding binary assets")

function(ADD_RESOURCES out_var)
    set(result)
    foreach (in_f ${ARGN})
        file(RELATIVE_PATH src_f ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${in_f})
        set(out_f "${PROJECT_BINARY_DIR}/${in_f}.o")
        add_custom_command(OUTPUT ${out_f}
                COMMAND ${MINGW_LD} -r -b binary -o ${out_f} ${src_f}
                DEPENDS ${in_f}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Building object ${out_f}"
                VERBATIM
        )
        list(APPEND result ${out_f})
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(ADD_GENERATED_RESOURCES out_var base_dir)
    set(result)
    foreach (in_f ${ARGN})
        set(out_f "${PROJECT_BINARY_DIR}/${in_f}.o")
        get_filename_component(out_dir "${out_f}" DIRECTORY)
        add_custom_command(OUTPUT ${out_f}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                COMMAND ${MINGW_LD} -r -b binary -o ${out_f} ${in_f}
                DEPENDS "${base_dir}/${in_f}"
                WORKING_DIRECTORY ${base_dir}
                COMMENT "Building generated object ${out_f}"
                VERBATIM
        )
        list(APPEND result ${out_f})
    endforeach()
    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

# Add all files in assets/ as resources

file(GLOB_RECURSE RESOURCE_FILES RELATIVE ${CMAKE_CURRENT_LIST_DIR} CONFIGURE_DEPENDS "assets/*")
add_resources(EMBEDDED_RESOURCES ${RESOURCE_FILES})

set(HELPER_PUBLISH_DIR "${CMAKE_BINARY_DIR}/assets/helper")
set(HELPER_RUNTIME_FILES
        "assets/helper/OmotiMusicHelper.exe"
        "assets/helper/D3DCompiler_47_cor3.dll"
        "assets/helper/PenImc_cor3.dll"
        "assets/helper/PresentationNative_cor3.dll"
        "assets/helper/vcruntime140_cor3.dll"
        "assets/helper/wpfgfx_cor3.dll"
)

set(HELPER_PUBLISH_OUTPUTS)
foreach(helper_rel_path ${HELPER_RUNTIME_FILES})
    list(APPEND HELPER_PUBLISH_OUTPUTS "${CMAKE_BINARY_DIR}/${helper_rel_path}")
endforeach()

add_custom_command(
        OUTPUT ${HELPER_PUBLISH_OUTPUTS}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HELPER_PUBLISH_DIR}"
        COMMAND ${DOTNET_EXE} publish "${CMAKE_SOURCE_DIR}/tools/OmotiMusicHelper/OmotiMusicHelper.csproj"
                -c Release
                -r win-x64
                --self-contained true
                /p:PublishSingleFile=true
                -o "${HELPER_PUBLISH_DIR}"
        DEPENDS
                "${CMAKE_SOURCE_DIR}/tools/OmotiMusicHelper/OmotiMusicHelper.csproj"
                "${CMAKE_SOURCE_DIR}/tools/OmotiMusicHelper/Program.cs"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Publishing OmotiMusicHelper runtime package"
        VERBATIM
)

add_generated_resources(EMBEDDED_HELPER_RESOURCES "${CMAKE_BINARY_DIR}" ${HELPER_RUNTIME_FILES})

add_library(Omoti SHARED ${Omoti_SRC} ${EMBEDDED_RESOURCES} ${EMBEDDED_HELPER_RESOURCES})
add_custom_command(TARGET Omoti POST_BUILD
        COMMAND ${CMAKE_COMMAND}
        -DSRC="$<TARGET_FILE:Omoti>"
        -DOUTDIR="E:/latiteskid"
        -DSERIES_MAJOR="2"
        -DPATCH_WIDTH="3"
        -DLATEST_NAME="latite.dll"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyBuiltDll.cmake"
        COMMENT "Copying Omoti DLL with auto-increment version to E:/latiteskid")

# enable exceptions
target_compile_options(Omoti PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,MSVC,Clang,AppleClang>:/EHsc>)

# C++ standard
set_property(TARGET Omoti PROPERTY CXX_STANDARD 23)

# Disable RTTI and set warning level
add_definitions(/GR- /D_HAS_STATIC_RTTI=0 /W4 /MT)
add_compile_definitions(_UNICODE UNICODE)

# Link libraries
target_link_libraries(Omoti PRIVATE
        # WinRT
        WindowsApp.lib
        Shlwapi.lib

        # Needed for getting app version
        Version.lib

        # Needed for DirectX hooking and Direct2D rendering
        d3d11
        d3d12
        dxguid
        dxgi

        # UI rendering
        d2d1

        # Text rendering
        dwrite
        gdi32

        # ImageNtHeader for mnemosyne
        Dbghelp

        # PlaySound for scripting
        winmm
        ole32
)

target_include_directories(Omoti PRIVATE
        src
        deps
)

add_subdirectory(src)
add_subdirectory(deps)
