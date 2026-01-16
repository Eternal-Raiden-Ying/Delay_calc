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

set(CMAKE_BUILD_TYPE Release)

# -----------------------------------------------------------------
# 性能提示：并行计算在 Debug 模式下效果不明显。
# 如果想看全速运行效果，可以在重新配置 CMake 时
# 手动把 CMAKE_BUILD_TYPE 改成 Release。
# -----------------------------------------------------------------

# --- 修改 1: 启用 OpenMP 查找 ---
find_package(OpenMP REQUIRED)

# 使用 CMake 默认的标准/优化配置：
# - CMAKE_CXX_STANDARD 已经保证使用 C++17
# - Debug 模式会自动带 -g，Release 模式会自动带优化参数
# 这里仅根据需要微调 Release 的优化等级
set(CMAKE_CXX_FLAGS_RELEASE "-O1 -DNDEBUG" CACHE STRING "Custom Release Flags" FORCE)

# 设置全局include目录（使用绝对路径）
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/Parser-SPEF/parser-spef)
include_directories(${PROJECT_SOURCE_DIR}/Parser-SPEF)  # 添加这行以支持pegtl的相对路径包含
include_directories(${PROJECT_SOURCE_DIR}/Tree)
include_directories(${PROJECT_SOURCE_DIR}/Read_train)
include_directories(${PROJECT_SOURCE_DIR}/Tree/resc)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)

# 不需要的注释掉
add_executable(course
    Tree/course.cpp
    Read_train/Read.cpp
    Tree/src/build_tree.cpp       # <-- new: RC tree module
    Tree/src/calc_delay.cpp       # <-- add Elmore delay module
    Tree/src/analysis.cpp        # <-- new: analysis/statistics module
    Tree/src/io.cpp              # <-- new: IO helper module
)
if(OpenMP_CXX_FOUND)
    # 链接 OpenMP::OpenMP_CXX 会自动处理编译标志(-fopenmp)和链接库
    target_link_libraries(course PRIVATE stdc++fs OpenMP::OpenMP_CXX)
else()
    target_link_libraries(course PRIVATE stdc++fs)
endif()


# add_executable(parser_test Tree/parser_test.cpp)
# target_link_libraries(parser_test stdc++fs)

