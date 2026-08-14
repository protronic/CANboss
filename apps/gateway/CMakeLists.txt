# CANboss-Gateway: NDJSON-Interface fuer Webapps (lib/jsonapi) ueber
# eine UART (native_sim: PTY/stdio, stm32h573i_dk: ST-Link-VCP).
#
#   west build -b native_sim/native/64 CANboss/apps/gateway -d build-gw
#   west build -b stm32h573i_dk       CANboss/apps/gateway -d build-gw-h573

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss_gateway LANGUAGES C)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CANOPEN_DIR ${REPO_ROOT}/modules/CANopenNode)
set(CBLIB ${REPO_ROOT}/lib)

target_sources(app PRIVATE
    src/main.c
    # NDJSON-Dispatcher + RAM-Firmware-Slots
    ${CBLIB}/jsonapi/jsonapi.c
    ${CBLIB}/jsonapi/jsonapi_json.c
    ${CBLIB}/jsonapi/jsonapi_fw_ram.c
    # gemeinsame CANopen-Schicht
    ${CBLIB}/canopen/can_if.c
    ${CBLIB}/canopen/can_zephyr.c
    ${CBLIB}/canopen/osal_zephyr.c
    ${CBLIB}/canopen/co_node.c
    ${CBLIB}/canopen/port/CO_driver.c
    # CANopenNode-Stack inkl. Gateway-ASCII (CiA 309-3); Konfiguration
    # via CO_DRIVER_CUSTOM -> src/CO_driver_custom.h
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
    ${CANOPEN_DIR}/309/CO_gateway_ascii.c
)

target_include_directories(app PRIVATE
    src
    ${CBLIB}/jsonapi
    ${CBLIB}/canopen
    ${CBLIB}/canopen/port
    ${CANOPEN_DIR}
)

# Master-OD (canboss_master.eds; eingecheckte generierte Dateien)
include(${CBLIB}/od/od_codegen.cmake)
canboss_od_add(canboss_master)

target_compile_definitions(app PRIVATE
    CO_DRIVER_CUSTOM
    CB_FW_RAM_SLOTS=${CONFIG_CANBOSS_GW_FW_SLOTS}
    CB_FW_RAM_SLOT_SIZE=${CONFIG_CANBOSS_GW_FW_SLOT_SIZE}
)
