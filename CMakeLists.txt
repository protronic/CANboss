# Zephyr-Build des CANboss-Monitors (der POSIX/Linux-Build laeuft
# weiterhin ueber das Makefile).
#
#   west build -b native_sim/native/64 CANboss
#   ./build/zephyr/zephyr.exe                    # --help fuer native_sim-Optionen

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss LANGUAGES C)

# Generierte Knotentabellen (make gen / tools/eds2tui.py)
file(GLOB canboss_gen_sources ${CMAKE_CURRENT_SOURCE_DIR}/src/gen/canboss_net_*.c)

target_sources(app PRIVATE
    src/main_zephyr.c
    src/ui.c
    src/tui.c
    src/tui_io_zephyr.c
    src/can_if.c
    src/can_zephyr.c
    src/co_node.c
    src/osal_zephyr.c
    src/sdo_value.c
    ${canboss_gen_sources}
    port/CO_driver.c
    od/canboss_master.c
    # CANopenNode-Stack (Submodul, protronic-Fork); nicht benoetigte
    # Module (LSS, LEDs, SRDO, Gateway) sind per CO_CONFIG in
    # port/CO_driver_target.h deaktiviert.
    CANopenNode/CANopen.c
    CANopenNode/301/CO_ODinterface.c
    CANopenNode/301/CO_NMT_Heartbeat.c
    CANopenNode/301/CO_HBconsumer.c
    CANopenNode/301/CO_Emergency.c
    CANopenNode/301/CO_SDOserver.c
    CANopenNode/301/CO_SDOclient.c
    CANopenNode/301/CO_PDO.c
    CANopenNode/301/CO_SYNC.c
    CANopenNode/301/CO_TIME.c
    CANopenNode/301/CO_fifo.c
    CANopenNode/301/crc16-ccitt.c
)

target_include_directories(app PRIVATE
    src
    src/gen
    port
    od
    CANopenNode
)
