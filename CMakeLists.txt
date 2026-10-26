cmake_minimum_required(VERSION 3.24)
project(TitanNTFS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanNTFS Core Library
add_library(titanntfs_core INTERFACE)
target_include_directories(titanntfs_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable
add_executable(titanntfs_tests tests/test_ntfs.cpp)
target_link_libraries(titanntfs_tests PRIVATE titanntfs_core)

# CLI Formatting Tool
add_executable(titanntfs_format src/tools/titanntfs_format.cpp)
target_link_libraries(titanntfs_format PRIVATE titanntfs_core)

# CLI Inspection & Scrubber Tool
add_executable(titanntfs_check src/tools/titanntfs_check.cpp)
target_link_libraries(titanntfs_check PRIVATE titanntfs_core)

# Background Service Daemon
add_executable(titanntfs_svc src/service/titanntfs_svc.cpp)
target_link_libraries(titanntfs_svc PRIVATE titanntfs_core)
if (WIN32)
    target_link_libraries(titanntfs_svc PRIVATE advapi32)
endif()

if (WIN32)
    # WinFsp Mount Driver Bridge
    add_executable(titanntfs_mount src/mount/winfsp_mount.cpp)
    target_link_libraries(titanntfs_mount PRIVATE titanntfs_core)

    # Standalone Installer Packager
    add_executable(titanntfs_packager src/tools/titanntfs_packager.cpp)
    target_link_libraries(titanntfs_packager PRIVATE titanntfs_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanntfs_setup WIN32 src/installer/titanntfs_setup.cpp)
    target_link_libraries(titanntfs_setup PRIVATE titanntfs_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanntfs_setup titanntfs_format titanntfs_check titanntfs_mount titanntfs_svc titanntfs_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanntfs_setup POST_BUILD
        COMMAND titanntfs_packager $<TARGET_FILE:titanntfs_setup>
                $<TARGET_FILE:titanntfs_format>
                $<TARGET_FILE:titanntfs_check>
                $<TARGET_FILE:titanntfs_mount>
                $<TARGET_FILE:titanntfs_svc>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanntfs_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding all binaries, WinFsp MSI, and assets into standalone titanntfs_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanNTFS")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NTFS File System Engine, WinFsp Mount Bridge, and MFT Recovery for Windows NT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

install(TARGETS titanntfs_tests titanntfs_format titanntfs_check titanntfs_svc RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanntfs_mount titanntfs_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

set(CPACK_GENERATOR "ZIP")
include(CPack)

# CTest registration
enable_testing()
add_test(NAME TitanNtfsCoreTests COMMAND titanntfs_tests)
