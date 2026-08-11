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

# FT813-Display: Framebuffer-Treiber ODER DRAW_EVE-SPI-Glue
if(CONFIG_CANBOSSTOUCH_DRAW_EVE)
    # Lokale Kopie: Upstream-flush_cb laesst den CMD-Burst zwischen Frames
    # offen (SPI-Korruption durch Touch-Reads) und wartet ohne Timeout.
    target_sources(app PRIVATE
        drivers/lv_draw_eve_zephyr.c
        drivers/lv_draw_eve_display.c
    )
elseif(CONFIG_SPI)
    # Legacy: dummer RGB565-Framebuffer in RAM_G (nicht fuer stm32h573i_dk)
    target_sources(app PRIVATE drivers/ft813.c)
endif()

target_include_directories(app PRIVATE
    src
    app
    app/generated
    drivers
    ${CBLIB}/canopen
    ${CBLIB}/canopen/port
    ${CANOPEN_DIR}
    # canboss_types.h nutzt "lvgl/lvgl.h": Elternverzeichnis des
    # LVGL-Moduls aus dem west-Manifest
    ${ZEPHYR_LVGL_MODULE_DIR}/..
)

# Master-OD: zur Buildzeit aus eds/canboss_master.eds generiert, wenn der
# EDSSharp-CLI da ist; sonst die eingecheckten Dateien aus lib/od/
include(${CBLIB}/od/od_codegen.cmake)
canboss_od_add(canboss_master)

# ---------------------------------------------------------------- Berry
if(CONFIG_CANBOSSTOUCH_BERRY)
    # gemeinsame Berry-Schicht: VM, od_*-Bindings, REPL, coc-Codegen
    # (libm: auf native_sim Teil der Host-glibc; Embedded bringt die
    # Toolchain sie selbst mit)
    include(${CBLIB}/berry_od/berry.cmake)
    # Shell-Kommando "berry" (Ausgabe an die Zephyr-Shell)
    target_sources(app PRIVATE src/berry_shell.c)
endif()
