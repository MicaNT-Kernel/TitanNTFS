cmake_minimum_required(VERSION 3.24)
project(TitanNTFS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(titanntfs_core INTERFACE)
target_include_directories(titanntfs_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(titanntfs_tests tests/test_ntfs.cpp)
target_link_libraries(titanntfs_tests PRIVATE titanntfs_core)

add_executable(titanntfs_format src/tools/titanntfs_format.cpp)
target_link_libraries(titanntfs_format PRIVATE titanntfs_core)

add_executable(titanntfs_check src/tools/titanntfs_check.cpp)
target_link_libraries(titanntfs_check PRIVATE titanntfs_core)
