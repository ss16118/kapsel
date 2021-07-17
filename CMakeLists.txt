cmake_minimum_required(VERSION 3.19)
project(container_cpp)

set(CMAKE_CXX_STANDARD 14)

add_executable(container_cpp src/main.cpp src/command_types.h src/utils.cpp src/utils.h)