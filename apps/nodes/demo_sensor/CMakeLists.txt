# Demo-Knoten "Klimasensor" (Node 48) - CANopenNode-Slave mit Simulation.
#   west build -b native_sim/native/64 CANboss/apps/nodes/demo_sensor
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss_demo_sensor LANGUAGES C)

set(NODE_OD demo_sensor)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/node.cmake)
