cmake_minimum_required(VERSION 3.19)
project(kapsel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS -pthread)
include_directories(libs)

# cxxopts
add_library(cxxopts STATIC libs/cxxopts/cxxopts.hpp)
set_target_properties(cxxopts PROPERTIES LINKER_LANGUAGE CXX)

# loguru
add_library(loguru STATIC libs/loguru/loguru.cpp libs/loguru/loguru.hpp)

add_executable(kapsel src/main.cpp src/constants.h src/utils.cpp src/utils.h src/container.cpp src/container.h)
target_link_libraries(kapsel PRIVATE cxxopts loguru ${CMAKE_DL_LIBS})