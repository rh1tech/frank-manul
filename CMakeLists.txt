# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD iris_m2)
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_PLATFORM rp2350-arm-s)

include(pico_sdk_import.cmake)

project(frank_manul C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# DispHSTX display driver (DVI via HSTX peripheral)
add_subdirectory(lib/DispHSTX DispHSTX)

# Unified PS/2 driver (keyboard + mouse via PIO)
add_subdirectory(drivers/ps2)

# USB HID option
option(USB_HID "Enable USB HID keyboard (disables USB serial console)" OFF)

# Main executable
add_executable(frank_manul
    src/main.c
    src/display.c
    src/framebuf.c
    src/keyboard.c
    src/keyboard_ps2.c
    src/keyboard_usb.c
    src/serial.c
    src/sound.c
    src/audio.c
    src/font.c
    src/flash.c
    src/browser_config.c
    src/netcard.c
    src/url.c
    src/http.c
    src/html.c
    src/render.c
    src/browser.c
    src/wifi_setup.c
)

# I2S audio PIO program
add_subdirectory(drivers/audio)

# PIO UART for netcard (TX on GPIO21, RX on GPIO20)
add_subdirectory(drivers/uart_pio)

# PSRAM driver (init + allocator)
add_library(psram_driver
    drivers/psram_init.c
    drivers/psram_allocator.c
)
target_include_directories(psram_driver PUBLIC ${CMAKE_CURRENT_LIST_DIR}/drivers)
target_compile_definitions(psram_driver PRIVATE PSRAM_MAX_FREQ_MHZ=100)
target_link_libraries(psram_driver pico_stdlib)

target_include_directories(frank_manul PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/drivers/ps2
    ${CMAKE_CURRENT_LIST_DIR}/drivers
)

# Force-include our DispHSTX config so the library sees it before its own _config.h
target_compile_options(frank_manul PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/src/config.h)

# Suppress warnings from ported code
target_compile_options(frank_manul PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
)

# Read version from version.txt ("major minor" format)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" _VERSION_LINE)
list(GET _VERSION_LINE 0 _VERSION_LINE)
string(REPLACE " " ";" _VERSION_PARTS "${_VERSION_LINE}")
list(GET _VERSION_PARTS 0 FL_VER_MAJOR)
list(GET _VERSION_PARTS 1 FL_VER_MINOR)
if(FL_VER_MINOR LESS 10)
    set(FL_VERSION_STR "${FL_VER_MAJOR}.0${FL_VER_MINOR}")
else()
    set(FL_VERSION_STR "${FL_VER_MAJOR}.${FL_VER_MINOR}")
endif()

target_compile_definitions(frank_manul PRIVATE
    FRANKMANUL_VERSION_STR="${FL_VERSION_STR}"
    PICO_MALLOC_PANIC=0
    PSRAM_MAX_FREQ_MHZ=100
)

# USB HID configuration
if(USB_HID)
    message(STATUS "USB HID keyboard enabled")
    message(STATUS "  USB CDC stdio will be DISABLED")
    set(USB_HID_ENABLED ON CACHE BOOL "" FORCE)
    add_subdirectory(drivers/usbhid)
    target_link_libraries(frank_manul usbhid)
    target_include_directories(frank_manul PRIVATE ${CMAKE_CURRENT_LIST_DIR}/drivers/usbhid)
    target_compile_definitions(frank_manul PRIVATE USB_HID=1)
    pico_enable_stdio_usb(frank_manul 0)
    pico_enable_stdio_uart(frank_manul 0)
else()
    message(STATUS "USB HID keyboard disabled")
    message(STATUS "  USB CDC stdio enabled (5 second startup delay)")
    pico_enable_stdio_usb(frank_manul 1)
    pico_enable_stdio_uart(frank_manul 0)
endif()

# TinyUSB HID class header needed for HID key constants (used by keyboard.c)
target_include_directories(frank_manul PRIVATE
    ${PICO_SDK_PATH}/lib/tinyusb/src
)

target_link_libraries(frank_manul
    DispHSTX
    ps2_driver
    psram_driver
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_gpio
    hardware_uart
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_pwm
    hardware_sync
    hardware_xip_cache
)

pico_add_extra_outputs(frank_manul)
