# Makefile for HDICU Screen Board — GD32F303RCT6
# Bare-metal, no HAL, register-level access

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

TARGET = screen_fw
BUILD_DIR = build

CPU = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
MCU = $(CPU)

C_SOURCES = main.c
ASM_SOURCES = startup.s

CFLAGS = $(MCU) -Os -std=c99 -Wall -fdata-sections -ffunction-sections
LDSCRIPT = GD32F303RC_FLASH.ld
LDFLAGS = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) -Wl,--gc-sections -lc -lm -lnosys $(CFLAGS_EXTRA)

all: $(BUILD_DIR)/$(TARGET).bin size

$(BUILD_DIR)/$(TARGET).elf: $(C_SOURCES) $(ASM_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(C_SOURCES) $(ASM_SOURCES) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CP) -O binary $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

size: $(BUILD_DIR)/$(TARGET).elf
	$(SZ) $<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean size
