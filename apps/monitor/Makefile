# CANboss-Monitor - CANopen-Parametermonitor (C-Port von CANboss-rs)
# POSIX/Linux-Build mit direktem SocketCAN (der Zephyr-Build laeuft
# ueber west, siehe CMakeLists.txt).
#
# Ziele (aus apps/monitor/ aufrufen):
#   make            baut ./canboss (SocketCAN-Variante)
#   make gen        erzeugt src/gen/ neu aus ../../eds (tools/eds2tui.py)
#   make test       baut und startet den Selbsttest
#   make clean      raeumt Build-Artefakte weg

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=gnu11

ROOT    := ../..
CANOPEN := $(ROOT)/modules/CANopenNode
CBLIB   := $(ROOT)/lib

CFLAGS  += -Isrc -Isrc/gen -I$(CBLIB)/canopen -I$(CBLIB)/canopen/port \
           -I$(CBLIB)/od/canboss_master -I$(CANOPEN)
LDFLAGS += -pthread

BUILD := build

# CANopenNode-Stack (Submodul, protronic-Fork). Nicht benoetigte Module
# (LSS, LEDs, SRDO, Gateway) sind per CO_CONFIG in
# lib/canopen/port/CO_driver_target.h deaktiviert.
CANOPEN_SRCS := \
	$(CANOPEN)/CANopen.c \
	$(CANOPEN)/301/CO_ODinterface.c \
	$(CANOPEN)/301/CO_NMT_Heartbeat.c \
	$(CANOPEN)/301/CO_HBconsumer.c \
	$(CANOPEN)/301/CO_Emergency.c \
	$(CANOPEN)/301/CO_SDOserver.c \
	$(CANOPEN)/301/CO_SDOclient.c \
	$(CANOPEN)/301/CO_PDO.c \
	$(CANOPEN)/301/CO_SYNC.c \
	$(CANOPEN)/301/CO_TIME.c \
	$(CANOPEN)/301/CO_fifo.c \
	$(CANOPEN)/301/crc16-ccitt.c

APP_SRCS := \
	$(CBLIB)/od/canboss_master/canboss_master.c \
	$(CBLIB)/canopen/port/CO_driver.c \
	$(CBLIB)/canopen/can_if.c \
	$(CBLIB)/canopen/can_socketcan.c \
	$(CBLIB)/canopen/can_serial.c \
	$(CBLIB)/canopen/co_node.c \
	$(CBLIB)/canopen/osal_posix.c \
	src/sdo_value.c \
	src/tui.c \
	src/tui_io_posix.c \
	src/ui.c \
	src/main.c \
	$(wildcard src/gen/canboss_net_*.c)

SRCS := $(CANOPEN_SRCS) $(APP_SRCS)
# Objekte flach im Build-Verzeichnis (Dateinamen sind eindeutig);
# die Quellverzeichnisse liefert VPATH.
OBJS := $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
VPATH := src src/gen $(CBLIB)/canopen $(CBLIB)/canopen/port \
         $(CBLIB)/od/canboss_master $(CANOPEN) $(CANOPEN)/301

TEST_SRCS := tests/test_values.c src/sdo_value.c $(wildcard src/gen/canboss_net_*.c)

all: canboss

canboss: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

gen:
	cd $(ROOT) && python3 tools/eds2tui.py --network eds/network.json --out apps/monitor/src/gen

test: $(BUILD)/test_values
	$(BUILD)/test_values

$(BUILD)/test_values: $(TEST_SRCS) src/canboss.h src/sdo_value.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS)

clean:
	rm -rf $(BUILD) canboss
