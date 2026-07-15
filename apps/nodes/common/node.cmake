# Gemeinsame CMake-Basis der Demo-Knoten. Erwartet:
#   NODE_OD   Name des generierten OD (z.B. demo_io) unter lib/od/
# Einbinden NACH find_package(Zephyr) und project().

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(CANOPEN_DIR ${REPO_ROOT}/modules/CANopenNode)
set(CBLIB ${REPO_ROOT}/lib)
set(NODE_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common)

target_sources(app PRIVATE
    src/main.c
    ${NODE_COMMON}/node_app.c
    # gemeinsame CANopen-Schicht (lib/canopen) + Knoten-OD
    ${CBLIB}/canopen/can_if.c
    ${CBLIB}/canopen/can_zephyr.c
    ${CBLIB}/canopen/osal_zephyr.c
    ${CBLIB}/canopen/co_node.c
    ${CBLIB}/canopen/port/CO_driver.c
    ${CBLIB}/od/${NODE_OD}/${NODE_OD}.c
    # CANopenNode-Stack (Submodul, protronic-Fork)
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
    ${NODE_COMMON}
    ${CBLIB}/canopen
    ${CBLIB}/canopen/port
    ${CBLIB}/od/${NODE_OD}
    ${CANOPEN_DIR}
)
