cmake_minimum_required(VERSION 3.12)

project(
    vtpc
    VERSION 0.1
    DESCRIPTION "VT Page Cache"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(lib)
add_subdirectory(test)
