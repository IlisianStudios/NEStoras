cmake_minimum_required(VERSION 3.20)

project(NEStoras LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED CONFIG)

add_executable(NEStoras
    src/main.cpp
    src/cpu.c
        include/bus.h
        src/bus.c
)

target_include_directories(NEStoras PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(NEStoras PRIVATE SDL2::SDL2)

target_compile_options(NEStoras PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)