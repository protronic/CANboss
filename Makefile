# =============================================================================
# CANbossTouch - STM32H573I-DK + gen4-FT813-70CTP-CLB als CANopen-Bediengeraet
#
# Build mit der STMicroelectronics-Toolchain gnu-tools-for-stm32
# (https://github.com/STMicroelectronics/gnu-tools-for-stm32, in
# STM32CubeIDE enthalten). Pfad zur Toolchain ueber GCC_PATH setzen, z.B.:
#
#   make GCC_PATH=$(HOME)/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.\
#        externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_*/tools/bin
#
# Ohne GCC_PATH wird arm-none-eabi-gcc aus dem PATH verwendet (Arm GNU
# Toolchain, identische Codebasis wie gnu-tools-for-stm32).
#
# Ziele:
#   make            Firmware bauen (build/CANbossTouch.elf/.hex/.bin)
#   make gen        LVGL-Screens aus den EDS-/PoC-Dateien neu generieren
#   make host       Linux/SDL2-Host-Build der UI (siehe Makefile.host)
#   make clean
# =============================================================================

TARGET = CANbossTouch
BUILD_DIR = build
DEBUG = 1
OPT = -Og

# -----------------------------------------------------------------------------
# Toolchain (gnu-tools-for-stm32 / arm-none-eabi)
# -----------------------------------------------------------------------------
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

PYTHON ?= python3

# -----------------------------------------------------------------------------
# MCU: STM32H573IIK3Q (Cortex-M33, FPU fpv5-sp-d16)
# -----------------------------------------------------------------------------
CPU = -mcpu=cortex-m33
FPU = -mfpu=fpv5-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

C_DEFS = \
-DUSE_HAL_DRIVER \
-DSTM32H573xx \
-DCO_DRIVER_CUSTOM

AS_DEFS =

# -----------------------------------------------------------------------------
# Quellen
# -----------------------------------------------------------------------------
CORE_SOURCES = $(wildcard Core/Src/*.c)

HAL_SOURCES = $(filter-out %_template.c, $(wildcard Drivers/STM32H5xx_HAL_Driver/Src/*.c))

FREERTOS_DIR = Middlewares/Third_Party/FreeRTOS/Source
FREERTOS_SOURCES = \
$(wildcard $(FREERTOS_DIR)/*.c) \
$(FREERTOS_DIR)/portable/GCC/ARM_CM33_NTZ/non_secure/port.c \
$(FREERTOS_DIR)/portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c \
$(FREERTOS_DIR)/portable/MemMang/heap_4.c \
$(FREERTOS_DIR)/CMSIS_RTOS_V2/cmsis_os2.c

LVGL_DIR = Middlewares/Third_Party/LVGL/lvgl
LVGL_SOURCES = $(shell find $(LVGL_DIR)/src -name '*.c')

CANOPEN_DIR = Middlewares/CANopen/CANopenNode
CANOPEN_SOURCES = \
$(CANOPEN_DIR)/CANopen.c \
$(wildcard $(CANOPEN_DIR)/301/*.c) \
$(wildcard $(CANOPEN_DIR)/303/*.c) \
$(wildcard $(CANOPEN_DIR)/304/*.c) \
$(wildcard $(CANOPEN_DIR)/305/*.c) \
$(wildcard $(CANOPEN_DIR)/309/*.c) \
$(CANOPEN_DIR)/storage/CO_storage.c

CANOPEN_PORT_DIR = Middlewares/CANopen/CANopenNode_STM32
CANOPEN_PORT_SOURCES = \
$(CANOPEN_PORT_DIR)/CO_app_STM32.c \
$(CANOPEN_PORT_DIR)/CO_driver_STM32.c \
$(CANOPEN_PORT_DIR)/CO_storageBlank.c

APP_SOURCES = \
$(wildcard App/*.c) \
App/OD/OD.c \
$(wildcard App/generated/*.c)

C_SOURCES = \
$(CORE_SOURCES) \
$(HAL_SOURCES) \
$(FREERTOS_SOURCES) \
$(LVGL_SOURCES) \
$(CANOPEN_SOURCES) \
$(CANOPEN_PORT_SOURCES) \
$(APP_SOURCES)

ASM_SOURCES = Startup/startup_stm32h573xx.s

# -----------------------------------------------------------------------------
# Includes
# -----------------------------------------------------------------------------
C_INCLUDES = \
-ICore/Inc \
-IDrivers/STM32H5xx_HAL_Driver/Inc \
-IDrivers/STM32H5xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32H5xx/Include \
-IDrivers/CMSIS/Include \
-IMiddlewares/Third_Party/CMSIS/RTOS2/Include \
-I$(FREERTOS_DIR)/CMSIS_RTOS_V2 \
-I$(FREERTOS_DIR)/include \
-I$(FREERTOS_DIR)/portable/GCC/ARM_CM33_NTZ/non_secure \
-IMiddlewares/Third_Party/LVGL \
-I$(CANOPEN_DIR) \
-I$(CANOPEN_PORT_DIR) \
-IApp \
-IApp/OD \
-IApp/generated

# -----------------------------------------------------------------------------
# Flags
# -----------------------------------------------------------------------------
ASFLAGS = $(MCU) $(AS_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections

CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDSCRIPT = STM32H573IIKXQ_FLASH.ld

LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
          -u _printf_float

# -----------------------------------------------------------------------------
# Regeln
# -----------------------------------------------------------------------------
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# LVGL-Screens aus den EDS-Dateien generieren (Ergebnis liegt in App/generated
# und ist eingecheckt; nach Aenderungen an eds/ neu ausfuehren)
gen:
	$(PYTHON) tools/eds2lvgl.py --network eds/network.json --out App/generated
	$(PYTHON) tools/poc2lvgl.py --config poc --out App/generated

OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))

$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(BIN) $< $@

host:
	$(MAKE) -f Makefile.host

clean:
	-rm -rf $(BUILD_DIR)

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

.PHONY: all gen host clean
