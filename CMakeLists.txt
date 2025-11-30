cmake_minimum_required(VERSION 3.9)
MESSAGE(STATUS "CMAKE_ROOT: " ${CMAKE_ROOT})

project(DelayPredict)

# Build in 64bits
set(BUILD_USE_64BITS on)

# Turn on the verbose
set(CMAKE_VERBOSE_MAKEFILE ON)

# CXX target properties
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(USE_WERROR ON)

set(CMAKE_BUILD_TYPE Debug)

# find_package(OpenMP)

set(CMAKE_CXX_FLAGS "-g -std=c++17 -lstdc++fs ${CMAKE_CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS"${CMAKE_EXE_LINKER_FLAGS}")

# 设置全局include目录（使用绝对路径）
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/Parser-SPEF/parser-spef)
include_directories(${PROJECT_SOURCE_DIR}/Parser-SPEF)  # 添加这行以支持pegtl的相对路径包含
include_directories(${PROJECT_SOURCE_DIR}/eigen-3.4.0)
include_directories(${PROJECT_SOURCE_DIR}/Tree)
include_directories(${PROJECT_SOURCE_DIR}/Read_train)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)

# 不需要的注释掉
add_executable(course Tree/course.cpp Read_train/Read.cpp)
target_link_libraries(course stdc++fs)

add_executable(parser_test Tree/parser_test.cpp)
target_link_libraries(parser_test stdc++fs)

