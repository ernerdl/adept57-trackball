cmake_minimum_required(VERSION 3.13)

# pico-sdk location: -DPICO_SDK_PATH=... or the PICO_SDK_PATH environment variable
if(DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "pico-sdk location")
else()
    set(PICO_SDK_PATH "$ENV{HOME}/pico-sdk" CACHE PATH "pico-sdk location")
endif()
set(PICO_BOARD waveshare_rp2040_zero)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(trackball_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(trackball_bridge
    src/main.c
    src/usb_descriptors.c
)

target_include_directories(trackball_bridge PRIVATE src)

option(DEBUG_EXT_TO_LEFT "Harici butonlar sol tık olarak da raporlansın (görünür test)" OFF)
option(DEBUG_AUTO_CLICK "2sn'de bir otomatik sol tık (TX yolu testi)" OFF)
if(DEBUG_AUTO_CLICK)
    target_compile_definitions(trackball_bridge PRIVATE DEBUG_AUTO_CLICK=1)
endif()
if(DEBUG_EXT_TO_LEFT)
    target_compile_definitions(trackball_bridge PRIVATE DEBUG_EXT_TO_LEFT=1)
endif()

target_link_libraries(trackball_bridge PRIVATE
    pico_stdlib
    pico_multicore
    tinyusb_device
    tinyusb_host
    tinyusb_board
    tinyusb_pico_pio_usb
    pico_bootsel_via_double_reset
    pico_flash
    hardware_flash
)

pico_add_extra_outputs(trackball_bridge)
