# Zephyr-Build der CANbossTouch-App (LVGL-UI + CANopenNode + Berry).
#
#   west build -b native_sim/native/64 CANboss/apps/touch
#   ./build/zephyr/zephyr.exe          # LVGL-Fenster (SDL2) + Shell auf stdin
#
#   west build -b stm32h573i_dk CANboss/apps/touch && west flash
#
# Headless (ohne SDL2):
#   west build -b native_sim/native/64 CANboss/apps/touch -- \
#     -DEXTRA_CONF_FILE=overlays/headless.conf \
#     -DEXTRA_DTC_OVERLAY_FILE=overlays/headless.overlay

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canbosstouch LANGUAGES C)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CANOPEN_DIR ${REPO_ROOT}/modules/CANopenNode)
set(BERRY_DIR ${REPO_ROOT}/modules/berry)
set(CBLIB ${REPO_ROOT}/lib)

# EDS-generierte LVGL-Screens + PoC-Konfiguration
# (python3 tools/eds2lvgl.py bzw. tools/poc2lvgl.py im Repo-Root)
file(GLOB canboss_gen_sources ${CMAKE_CURRENT_SOURCE_DIR}/app/generated/*.c)

target_sources(app PRIVATE
    src/main.c
    src/canboss_sdo_zephyr.c
    src/canboss_poc_can_zephyr.c
    # gemeinsame CANopen-Schicht (lib/canopen) + Master-OD
    ${CBLIB}/canopen/can_if.c
    ${CBLIB}/canopen/can_zephyr.c
    ${CBLIB}/canopen/osal_zephyr.c
    ${CBLIB}/canopen/co_node.c
    ${CBLIB}/canopen/port/CO_driver.c
    ${CBLIB}/od/canboss_master/canboss_master.c
    # App-Schicht (LVGL-Screens)
    app/canboss_ui.c
    app/canboss_poc.c
    ${canboss_gen_sources}
    # CANopenNode-Stack (Submodul, protronic-Fork); nicht benoetigte
    # Module sind per CO_CONFIG in lib/canopen/port deaktiviert.
    ${CANOPEN_DIR}/CANopen.c
    ${CANOPEN_DIR}/301/CO_ODinterface.c
    ${CANOPEN_DIR}/301/CO_NMT_Heartbeat.c
    ${CANOPEN_DIR}/301/CO_HBconsumer.c
    ${CANOPEN_DIR}/301/CO_Emergency.c
    ${CANOPEN_DIR}/301/CO_SDOserver.c
    ${CANOPEN_DIR}/301/CO_SDOclient.c
    ${CANOPEN_DIR}/301/CO_PDO.c
    ${CANOPEN_DIR}/301/CO_SYNC.c
    ${CANOPEN_DIR}/301/CO_TIME.c
    ${CANOPEN_DIR}/301/CO_fifo.c
    ${CANOPEN_DIR}/301/crc16-ccitt.c
)

# FT813-Displaytreiber (Hardware-Targets mit SPI; DT-Binding unter
# dts/bindings/display/protronic,ft813.yaml)
target_sources_ifdef(CONFIG_SPI app PRIVATE drivers/ft813.c)

target_include_directories(app PRIVATE
    src
    app
    app/generated
    ${CBLIB}/canopen
    ${CBLIB}/canopen/port
    ${CBLIB}/od/canboss_master
    ${CANOPEN_DIR}
    # canboss_types.h nutzt "lvgl/lvgl.h": Elternverzeichnis des
    # LVGL-Moduls aus dem west-Manifest
    ${ZEPHYR_LVGL_MODULE_DIR}/..
)

# ---------------------------------------------------------------- Berry
if(CONFIG_CANBOSSTOUCH_BERRY)
    # Konstanten-Tabellen der Berry-VM generieren (tools/coc, Python).
    # Ergebnis liegt wie beim Berry-Makefile in <berry>/generate/.
    set(BERRY_GEN ${BERRY_DIR}/generate)
    file(GLOB berry_core_sources ${BERRY_DIR}/src/*.c)

    add_custom_command(
        OUTPUT ${BERRY_GEN}/be_const_strtab.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BERRY_GEN}
        COMMAND ${PYTHON_EXECUTABLE} ${BERRY_DIR}/tools/coc/coc
                ${BERRY_DIR}/src ${BERRY_DIR}/default
                -o ${BERRY_GEN}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/berry/berry_conf.h
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/berry/berry_conf.h ${berry_core_sources}
        WORKING_DIRECTORY ${BERRY_DIR}
        COMMENT "Berry: Konstanten-Tabellen generieren (tools/coc)"
    )
    add_custom_target(berry_gen DEPENDS ${BERRY_GEN}/be_const_strtab.h)
    add_dependencies(app berry_gen)

    target_sources(app PRIVATE
        src/canboss_berry.c
        src/berry_port.c
        ${berry_core_sources}
        ${BERRY_DIR}/default/be_modtab.c
    )
    # Reihenfolge wichtig: zuerst unsere berry_conf.h, dann berry/src
    target_include_directories(app BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/berry)
    target_include_directories(app PRIVATE ${BERRY_DIR}/src)
    # libm: auf native_sim Teil der Host-glibc; auf Embedded-Targets
    # bringt die Toolchain (picolibc/newlib) libm selbst mit.
endif()
