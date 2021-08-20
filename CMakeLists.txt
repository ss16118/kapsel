cmake_minimum_required(VERSION 3.19)
project(container_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS -pthread)
include_directories(libs)

# cxxopts
add_library(cxxopts STATIC libs/cxxopts/cxxopts.hpp)
set_target_properties(cxxopts PROPERTIES LINKER_LANGUAGE CXX)

add_executable(container_cpp src/main.cpp src/constants.h src/utils.cpp src/utils.h src/container.cpp src/container.h src/semaphore.h)