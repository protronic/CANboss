# Demo-Knoten "IO-Modul" (Node 16) - CANopenNode-Slave mit Simulation.
#   west build -b native_sim/native/64 CANboss/apps/nodes/demo_io
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canboss_demo_io LANGUAGES C)

set(NODE_OD demo_io)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/node.cmake)
