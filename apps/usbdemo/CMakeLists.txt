# USB-Bring-up-Demo fuer STM32H573I-DK: CDC-ACM + Zephyr-Shell + Berry-GPIO.
#
#   west build -b stm32h573i_dk CANboss/apps/usbdemo -d build-usbdemo
#   west flash -d build-usbdemo
#
# ST-Link-VCP (115200): Logs + uart:~$  (berry led(0, true))
# User-USB CN17:        1209:0001, zweite Shell usb:~$ wenn Enumeration klappt

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss_usbdemo LANGUAGES C)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(BERRY_DIR ${REPO_ROOT}/modules/berry)
set(BERRY_OD_DIR ${REPO_ROOT}/lib/berry_od)
set(BERRY_GEN ${BERRY_DIR}/generate)

file(GLOB berry_core_sources ${BERRY_DIR}/src/*.c)

add_custom_command(
    OUTPUT ${BERRY_GEN}/be_const_strtab.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BERRY_GEN}
    COMMAND ${PYTHON_EXECUTABLE} ${BERRY_DIR}/tools/coc/coc
            ${BERRY_DIR}/src ${BERRY_DIR}/default
            -o ${BERRY_GEN}
            -c ${BERRY_OD_DIR}/berry_conf.h
    DEPENDS ${BERRY_OD_DIR}/berry_conf.h ${berry_core_sources}
    WORKING_DIRECTORY ${BERRY_DIR}
    COMMENT "Berry: Konstanten-Tabellen generieren (tools/coc)"
)
add_custom_target(usbdemo_berry_gen DEPENDS ${BERRY_GEN}/be_const_strtab.h)
add_dependencies(app usbdemo_berry_gen)

target_sources(app PRIVATE
    src/main.c
    src/usb.c
    ${REPO_ROOT}/lib/usbc/usbc_h573_dk.c
    src/berry_gpio.c
    src/berry_port.c
    ${berry_core_sources}
    ${BERRY_DIR}/default/be_modtab.c
)

target_include_directories(app BEFORE PRIVATE ${BERRY_OD_DIR} src)
target_include_directories(app PRIVATE ${REPO_ROOT}/lib/usbc)

target_include_directories(app PRIVATE ${BERRY_DIR}/src)
