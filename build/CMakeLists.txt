cmake_minimum_required(VERSION 3.15)

project(securewipe LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

if(WIN32 AND NOT CMAKE_C_COMPILER)
    find_program(MSVC_COMPILER cl)
    if(MSVC_COMPILER)
        set(CMAKE_C_COMPILER "${MSVC_COMPILER}" CACHE PATH "C compiler" FORCE)
        message(STATUS "Selected MSVC compiler: cl")
    endif()
endif()

set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/source")
set(VERSION_H "${SOURCE_DIR}/module/metadata.c")

if(EXISTS "${VERSION_H}")
    file(READ "${VERSION_H}" VERSION_CONTENT)
    function(get_version_macro name output)
        string(REGEX MATCH "#define[ \t]+${name}[ \t]+\"([^\"]*)\"" match "${VERSION_CONTENT}")
        if(CMAKE_MATCH_1)
            set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        else()
            set(${output} "" PARENT_SCOPE)
        endif()
    endfunction()

    get_version_macro("SECURE_WIPE_NAME"         PRODUCT_NAME)
    get_version_macro("SECURE_WIPE_DESCRIPTION"  FILE_DESCRIPTION)
    get_version_macro("SECURE_WIPE_VERSION"      PRODUCT_VERSION)
    get_version_macro("SECURE_WIPE_LICENSE"      LICENSE)
    get_version_macro("SECURE_WIPE_COPYRIGHT"    COPYRIGHT)
    get_version_macro("SECURE_WIPE_AUTHOR"       AUTHOR)
    get_version_macro("SECURE_WIPE_CONTACT"      CONTACT)
    get_version_macro("SECURE_WIPE_HOMEPAGE"     HOMEPAGE)
endif()

file(GLOB_RECURSE SOURCES "${SOURCE_DIR}/*.c")
list(SORT SOURCES)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PRODUCT_NAME="${PRODUCT_NAME}"
    FILE_DESCRIPTION="${FILE_DESCRIPTION}"
    PRODUCT_VERSION="${PRODUCT_VERSION}"
    LICENSE="${LICENSE}"
    COPYRIGHT="${COPYRIGHT}"
    AUTHOR="${AUTHOR}"
    CONTACT="${CONTACT}"
    HOMEPAGE="${HOMEPAGE}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${SOURCE_DIR}
    ${SOURCE_DIR}/module
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /nologo /TC /utf-8)
    target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt setupapi cfgmgr32 advapi32 kernel32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W0 /Ox /GL /arch:AVX2 /DNDEBUG)
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -w -O3 -flto -march=native -DNDEBUG)
        target_link_options(${PROJECT_NAME} PRIVATE -s -flto -Wl,--build-id=none)
        if(APPLE)
            target_link_options(${PROJECT_NAME} PRIVATE -Wl,-dead_strip)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Od /Zi /RTC1)
        target_link_options(${PROJECT_NAME} PRIVATE /DEBUG)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -O0 -ggdb3)
    endif()
endif()