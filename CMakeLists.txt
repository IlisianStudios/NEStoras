cmake_minimum_required(VERSION 3.20)

project(NEStoras LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)
find_package(SDL2_ttf REQUIRED CONFIG)

add_executable(NEStoras
    src/main.c
    src/cpu.c
    src/bus.c
    src/instruction.c
    src/cartridge.c
    src/mappers.c
        include/apu.h
        src/apu.c
        src/timing.c
        include/timing.h
        src/ringbuffer.c
        include/ringbuffer.h
        src/debug_window.c
        include/debug_window.h
        include/ppu.h
        src/ppu.c
)

target_include_directories(NEStoras PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(NEStoras PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
)

if(WIN32)
    # Windowed subsystem (no console) only for non-Debug builds; Debug keeps
    # the console so printf / fprintf(stderr, ...) stay visible.
    set_target_properties(NEStoras PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    )
endif()

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