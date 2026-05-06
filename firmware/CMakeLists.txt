cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(coffe_grinder_firmware)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# rest of project
add_executable(coffe_grinder_firmware
coffe_grinder_firmware.c
)


# ?
pico_set_binary_type(coffe_grinder_firmware copy_to_ram)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(coffe_grinder_firmware pico_stdlib hardware_pio hardware_adc hardware_pwm)

# enable / disable hardware blocks
pico_enable_stdio_usb(coffe_grinder_firmware 1)
pico_enable_stdio_uart(coffe_grinder_firmware 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(coffe_grinder_firmware)
