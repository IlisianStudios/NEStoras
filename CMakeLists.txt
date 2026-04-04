cmake_minimum_required(VERSION 3.20)

project(NEStoras LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(NEStoras
    src/main.c
    src/cpu.c
    src/bus.c
    src/instruction.c
    src/cartridge.c
    src/mappers.c
)

target_include_directories(NEStoras PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(NEStoras PRIVATE SDL2::SDL2)

target_compile_options(NEStoras PRIVATE
    $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

# Define the source and destination paths
set(RES_FILE_LOG "${CMAKE_CURRENT_SOURCE_DIR}/testlog/nestest.log") # Adjust path to your folder
set(DEST_FILE_LOG "${CMAKE_CURRENT_BINARY_DIR}/nestest.log")
# Define the source and destination paths
set(RES_FILE_ROM "${CMAKE_CURRENT_SOURCE_DIR}/testroms/nestest.nes") # Adjust path to your folder
set(DEST_FILE_ROM "${CMAKE_CURRENT_BINARY_DIR}/nestest.nes")

# Copy the file during the build process
add_custom_command(
        TARGET NEStoras POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${RES_FILE_LOG}"
        "${DEST_FILE_LOG}"
        COMMENT "Copying test resource to build directory"
)

add_custom_command(
        TARGET NEStoras POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${RES_FILE_ROM}"
        "${DEST_FILE_ROM}"
        COMMENT "Copying test resource to build directory"
)