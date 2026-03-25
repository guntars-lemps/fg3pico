cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_fg3)

enable_language(CXX C ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(pico_fg3
    pico_fg3.c
    )

pico_generate_pio_header(pico_fg3 ${CMAKE_CURRENT_LIST_DIR}/pio_fg3.pio)

target_sources(pico_fg3 PRIVATE pico_fg3.c)

# pull in common dependencies
target_link_libraries(pico_fg3 pico_stdlib hardware_pio)

# create map/bin/hex file etc.
pico_add_extra_outputs(pico_fg3)

pico_enable_stdio_usb(pico_fg3 1)
pico_enable_stdio_uart(pico_fg3 0)