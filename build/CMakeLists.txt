cmake_minimum_required(VERSION 3.15)
if(NOT CMAKE_C_COMPILER)
find_program(GCC_COMPILER gcc)
find_program(CLANG_COMPILER clang)
if(GCC_COMPILER)
set(CMAKE_C_COMPILER "${GCC_COMPILER}" CACHE PATH "C compiler" FORCE)
message(STATUS "Automatically selected C compiler: gcc")
elseif(CLANG_COMPILER)
set(CMAKE_C_COMPILER "${CLANG_COMPILER}" CACHE PATH "C compiler" FORCE)
message(STATUS "Automatically selected C compiler: clang")
else()
message(FATAL_ERROR "Neither gcc nor clang found. Please install a C compiler.")
endif()
endif()
project(securewipe LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug or Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/source")
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
pkg_check_modules(PLATFORM_QUIET QUIET glib-2.0 IMPORTED_TARGET)
if(TARGET PkgConfig::PLATFORM_QUIET)
set(PLATFORM_FOUND TRUE)
endif()
endif()
if(NOT PLATFORM_FOUND)
find_library(PLATFORM_LIBRARY m)
find_path(PLATFORM_INCLUDE_DIR stdio.h)
if(PLATFORM_LIBRARY AND PLATFORM_INCLUDE_DIR)
add_library(Platform::Std UNKNOWN IMPORTED)
set_target_properties(Platform::Std PROPERTIES
IMPORTED_LOCATION ${PLATFORM_LIBRARY}
INTERFACE_INCLUDE_DIRECTORIES ${PLATFORM_INCLUDE_DIR}
)
set(PLATFORM_FOUND TRUE)
else()
message(FATAL_ERROR "Standard C library not found. Please install libc development package.")
endif()
endif()
if(WIN32)
set(WIN_LIBS_FOUND TRUE)
message(STATUS "Windows platform libraries will be linked directly")
endif()
set(VERSION_H "${SOURCE_DIR}/module/metadata.c")
file(READ "${VERSION_H}" VERSION_CONTENT)
function(get_version_macro name output)
string(REGEX MATCH "#define[ \t]+${name}[ \t]+\"([^\"]*)\"" match "${VERSION_CONTENT}")
if(CMAKE_MATCH_1)
set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
else()
set(${output} "" PARENT_SCOPE)
endif()
endfunction()
get_version_macro("SECURE_WIPE_NAME" PRODUCT_NAME)
get_version_macro("SECURE_WIPE_DESCRIPTION" FILE_DESCRIPTION)
get_version_macro("SECURE_WIPE_VERSION" PRODUCT_VERSION)
get_version_macro("SECURE_WIPE_LICENSE" LICENSE)
get_version_macro("SECURE_WIPE_LICENSE" COPYRIGHT)
get_version_macro("SECURE_WIPE_AUTHOR" AUTHOR)
get_version_macro("SECURE_WIPE_CONTACT" CONTACT)
get_version_macro("SECURE_WIPE_HOMEPAGE" HOMEPAGE)
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
if(WIN32)
target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt setupapi cfgmgr32 advapi32 kernel32)
elseif(TARGET PkgConfig::PLATFORM_QUIET)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::PLATFORM_QUIET)
elseif(TARGET Platform::Std)
target_link_libraries(${PROJECT_NAME} PRIVATE Platform::Std)
endif()
target_compile_options(${PROJECT_NAME} PRIVATE
-Wall -Wextra -pedantic
-finput-charset=UTF-8 -fexec-charset=UTF-8
)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
get_filename_component(SOURCE_ABS "${SOURCE_DIR}" ABSOLUTE)
get_filename_component(BINARY_ABS "${CMAKE_CURRENT_BINARY_DIR}" ABSOLUTE)
target_compile_options(${PROJECT_NAME} PRIVATE
-O0 -g
-fdebug-prefix-map=${SOURCE_ABS}=.
-fdebug-prefix-map=${BINARY_ABS}=.
-fno-ident
-gno-record-gcc-switches
)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
target_compile_options(${PROJECT_NAME} PRIVATE -O2 -DNDEBUG -g0)
target_link_options(${PROJECT_NAME} PRIVATE -s -Wl,--build-id=none)
else()
message(WARNING "Unknown build type '${CMAKE_BUILD_TYPE}'. Defaulting to Debug flags.")
target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g)
endif()