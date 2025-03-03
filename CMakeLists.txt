# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Pico-W-A2DP-Sink C CXX ASM)

# Show memory usage summary
add_link_options(
    -Wl,--print-memory-usage
)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Pico-W-A2DP-Sink main.c)

pico_set_program_name(Pico-W-A2DP-Sink "Pico-W-A2DP-Sink")
pico_set_program_version(Pico-W-A2DP-Sink "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Pico-W-A2DP-Sink 0)
pico_enable_stdio_usb(Pico-W-A2DP-Sink 0)

# Add the standard library to the build
target_link_libraries(Pico-W-A2DP-Sink
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(Pico-W-A2DP-Sink PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add directories
add_subdirectory(audio_i2s)
add_subdirectory(bluetooth)

# Add any user requested libraries
target_link_libraries(Pico-W-A2DP-Sink
        bluetooth
        pico_cyw43_arch_none
)

pico_add_extra_outputs(Pico-W-A2DP-Sink)
