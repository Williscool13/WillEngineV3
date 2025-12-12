cmake_minimum_required(VERSION 3.20)

project(offsetAllocator)

add_library(${PROJECT_NAME}
        offsetAllocator.cpp
        offsetAllocator.hpp
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)