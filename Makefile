# CANboss - CANopen-Parametermonitor (C-Port von CANboss-rs)
#
# Ziele:
#   make            baut ./canboss (SocketCAN-Variante)
#   make gen        erzeugt src/gen/ neu aus eds/ (tools/eds2tui.py)
#   make test       baut und startet den Selbsttest
#   make clean      raeumt Build-Artefakte weg

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=gnu11
CFLAGS  += -Isrc -Isrc/gen -Iport -Iod -ICANopenNode
LDFLAGS += -pthread

BUILD := build

# CANopenNode-Stack (Submodul, protronic-Fork). Nicht benoetigte Module
# (LSS, LEDs, SRDO, Gateway) sind per CO_CONFIG in port/CO_driver_target.h
# deaktiviert und werden nicht mitgebaut.
CANOPEN_SRCS := \
	CANopenNode/CANopen.c \
	CANopenNode/301/CO_ODinterface.c \
	CANopenNode/301/CO_NMT_Heartbeat.c \
	CANopenNode/301/CO_HBconsumer.c \
	CANopenNode/301/CO_Emergency.c \
	CANopenNode/301/CO_SDOserver.c \
	CANopenNode/301/CO_SDOclient.c \
	CANopenNode/301/CO_PDO.c \
	CANopenNode/301/CO_SYNC.c \
	CANopenNode/301/CO_TIME.c \
	CANopenNode/301/CO_fifo.c \
	CANopenNode/301/crc16-ccitt.c

APP_SRCS := \
	od/canboss_master.c \
	port/CO_driver.c \
	src/can_if.c \
	src/can_socketcan.c \
	src/can_serial.c \
	src/co_node.c \
	src/osal_posix.c \
	src/sdo_value.c \
	src/tui.c \
	src/tui_io_posix.c \
	src/ui.c \
	src/main.c \
	$(wildcard src/gen/canboss_net_*.c)

SRCS := $(CANOPEN_SRCS) $(APP_SRCS)
OBJS := $(SRCS:%.c=$(BUILD)/%.o)

TEST_SRCS := tests/test_values.c src/sdo_value.c $(wildcard src/gen/canboss_net_*.c)

all: canboss

canboss: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

gen:
	python3 tools/eds2tui.py

test: $(BUILD)/test_values
	$(BUILD)/test_values

$(BUILD)/test_values: $(TEST_SRCS) src/canboss.h src/sdo_value.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD) canboss

.PHONY: all gen test clean
