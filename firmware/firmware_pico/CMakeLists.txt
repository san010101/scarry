# ============================================================================
# CMakeLists.txt — прошивка Raspberry Pi Pico для картриджа «Эльф»
#
# Сборка (как обычный проект Pico SDK):
#   1) Скопируй pico_sdk_import.cmake из pico-sdk в эту папку
#   2) export PICO_SDK_PATH=/путь/к/pico-sdk
#   3) mkdir build && cd build && cmake .. && make
#
# Требуются библиотеки SD + FatFs (из pico-extras):
#   git clone https://github.com/raspberrypi/pico-extras.git
#   export PICO_EXTRAS_PATH=/путь/к/pico-extras
# ============================================================================

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(alf_cartridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Release)

pico_sdk_init()

add_executable(alf_cartridge
    main.c
    ssd1306.c
)

target_include_directories(alf_cartridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Ядро SDK
target_link_libraries(alf_cartridge PRIVATE
    pico_stdlib
    hardware_spi
    hardware_i2c
    hardware_gpio
    hardware_clocks
)

# SD-карта + FatFs (pico-extras: pico_sd_card и fatfs)
target_link_libraries(alf_cartridge PRIVATE
    pico_sd_card
    fatfs
)

pico_enable_stdio_usb(alf_cartridge 0)
pico_enable_stdio_uart(alf_cartridge 0)

pico_add_extra_outputs(alf_cartridge)
