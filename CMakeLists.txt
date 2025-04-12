# 设置CMake的最低版本要求
cmake_minimum_required(VERSION 3.10)

# 定义项目名称
project(npusim_project LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# # 设置编译选项
# set(SYSTEMC_HOME "/root/fdh/drca/systemC")  # 修改为实际的SystemC路径
# 从环境变量读取 SYSTEMC_HOME
if(NOT DEFINED ENV{SYSTEMC_HOME})
    message(FATAL_ERROR "The environment variable SYSTEMC_HOME is not set. Please export SYSTEMC_HOME before running CMake.")
endif()

set(SYSTEMC_HOME $ENV{SYSTEMC_HOME})
# set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -Wno-unused-result -Wno-ignored-pragmas -Wno-unknown-attributes -DBROAD_W=16")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DBROAD_W=16")

# SystemC库路径
link_directories(${SYSTEMC_HOME}/lib-linux64)

# 包含目录
include_directories(
    ${SYSTEMC_HOME}/include
    ./llm/include
)

# 搜索所有.cpp文件
file(GLOB_RECURSE SRC_FILES ./llm/*.cpp)# Remove 'a.cpp' from the list of sources
list(FILTER SRC_FILES EXCLUDE REGEX ".*/test_dramsys\\.cpp$")
add_subdirectory(DRAMSys)
# 定义生成的目标
message(STATUS "Source files: ${SRC_FILES}")

# find_package(Cairo REQUIRED)
# include_directories(${CAIRO_INCLUDE_DIRS})



add_executable(npusim ${SRC_FILES})
target_link_libraries(npusim
    PUBLIC
    systemc cairo sfml-graphics sfml-system sfml-window m pthread
        DRAMSys::libdramsys
        DRAMSys::util
        DRAMSys::config
)
target_compile_options(npusim PRIVATE -Ofast -Wno-unused-result -Wno-ignored-pragmas -Wno-unknown-attributes -DBROAD_W=16)


# # 调试版本
# add_executable(npusim_debug ${SRC_FILES})
# # set_target_properties(npusim_debug PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS_DEBUG}")
# # target_link_libraries(npusim_debug systemc sfml-graphics sfml-system sfml-window m)
# target_link_libraries(npusim_debug
#     PUBLIC
#     systemc cairo sfml-graphics sfml-system sfml-window m pthread
#         DRAMSys::libdramsys
#         DRAMSys::util
#         DRAMSys::config
# )
# target_compile_options(npusim_debug PRIVATE -g -Ofast -Wno-unused-result -Wno-ignored-pragmas -Wno-unknown-attributes -DBROAD_W=16)

# 清理目标的功能
# set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "npusim;npusim_debug")
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "npusim")
# add_library(systemc INTERFACE)

# add_library(SystemC::systemc ALIAS systemc)

