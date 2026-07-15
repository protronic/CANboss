# Zephyr-Build des CANboss-Monitors (der POSIX/Linux-Build laeuft
# weiterhin ueber das Makefile in diesem Verzeichnis).
#
#   west build -b native_sim/native/64 CANboss/apps/monitor
#   ./build/zephyr/zephyr.exe          # --help fuer native_sim-Optionen

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss_monitor LANGUAGES C)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CANOPEN_DIR ${REPO_ROOT}/modules/CANopenNode)
set(CBLIB ${REPO_ROOT}/lib)

# Generierte Knotentabellen (python3 tools/eds2tui.py im Repo-Root)
file(GLOB canboss_gen_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/canboss_net_*.c)

target_sources(app PRIVATE
    src/main_zephyr.c
    src/ui.c
    src/tui.c
    src/tui_io_zephyr.c
    src/sdo_value.c
    ${canboss_gen_sources}
    # gemeinsame CANopen-Schicht (lib/canopen); Master-OD siehe unten
    ${CBLIB}/canopen/can_if.c
    ${CBLIB}/canopen/can_zephyr.c
    ${CBLIB}/canopen/osal_zephyr.c
    ${CBLIB}/canopen/co_node.c
    ${CBLIB}/canopen/port/CO_driver.c
    # CANopenNode-Stack (Submodul, protronic-Fork); nicht benoetigte
    # Module (LSS, LEDs, SRDO, Gateway) sind per CO_CONFIG in
    # lib/canopen/port/CO_driver_target.h deaktiviert.
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

target_include_directories(app PRIVATE
    src
    src/gen
    ${CBLIB}/canopen
    ${CBLIB}/canopen/port
    ${CANOPEN_DIR}
)

# Master-OD: zur Buildzeit aus eds/canboss_master.eds generiert, wenn der
# EDSSharp-CLI da ist; sonst die eingecheckten Dateien aus lib/od/
include(${CBLIB}/od/od_codegen.cmake)
canboss_od_add(canboss_master)

# Berry-Skript-REPL (Taste 's' in der TUI; gemeinsame Schicht lib/berry_od)
if(CONFIG_CANBOSS_BERRY)
    include(${CBLIB}/berry_od/berry.cmake)
endif()
