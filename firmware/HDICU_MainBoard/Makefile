#############################################################################
# Makefile for HDICU-ZKB01A Main Board Firmware
# Target: STM32F103VET6 (Cortex-M3, 72MHz, 512KB Flash, 64KB RAM)
# Toolchain: arm-none-eabi-gcc
#
# Usage:
#   make          — build firmware.elf + firmware.bin
#   make clean    — remove build artifacts
#   make flash    — flash via st-flash (ST-Link)
#   make size     — show section sizes
#
# Prerequisites:
#   - arm-none-eabi-gcc (GNU ARM Embedded Toolchain)
#   - STM32F1xx HAL library (place in Lib/STM32F1xx_HAL_Driver/)
#   - FreeRTOS kernel (place in Lib/FreeRTOS/)
#   - CMSIS headers (place in Lib/CMSIS/)
#############################################################################

# Toolchain
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

# Target
TARGET = firmware
BUILD_DIR = build

# MCU flags
CPU = -mcpu=cortex-m3
FPU =
FLOAT-ABI =
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# Sources — Application
C_SOURCES = \
  main.c \
  App/main_app.c \
  App/data/app_data.c \
  App/tasks/tasks.c \
  App/freertos_hooks.c \
  Protocol/common/protocol_frame.c \
  Protocol/ipad/ipad_protocol.c \
  Protocol/screen/screen_protocol.c \
  Sensors/co2/co2_sensor.c \
  Sensors/o2/o2_sensor.c \
  Sensors/ntc/ntc_sensor.c \
  Sensors/jfc103/jfc103_sensor.c \
  Control/temp/temp_control.c \
  Control/humidity/humidity_control.c \
  Control/oxygen/oxygen_control.c \
  Control/timers/control_timers.c \
  Control/interlocks/interlock.c \
  Drivers/uart/uart_driver.c \
  Drivers/adc/adc_driver.c \
  Drivers/pwm/pwm_driver.c \
  Drivers/gpio/relay_driver.c \
  Drivers/flash/flash_storage.c

# Sources — HAL (requires Lib/STM32F1xx_HAL_Driver/)
C_SOURCES += \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c \
  Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
  Lib/system_stm32f1xx.c

# Sources — FreeRTOS (requires Lib/FreeRTOS/)
C_SOURCES += \
  Lib/FreeRTOS/Source/tasks.c \
  Lib/FreeRTOS/Source/queue.c \
  Lib/FreeRTOS/Source/list.c \
  Lib/FreeRTOS/Source/timers.c \
  Lib/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c \
  Lib/FreeRTOS/Source/portable/MemMang/heap_1.c

# ASM sources
ASM_SOURCES = \
  startup_stm32f103xe.s

# Include paths
C_INCLUDES = \
  -I. \
  -IBSP \
  -IApp/data \
  -IApp/tasks \
  -IProtocol/common \
  -IProtocol/ipad \
  -IProtocol/screen \
  -ISensors/co2 \
  -ISensors/o2 \
  -ISensors/ntc \
  -ISensors/jfc103 \
  -IControl/temp \
  -IControl/humidity \
  -IControl/oxygen \
  -IControl/timers \
  -IControl/interlocks \
  -IDrivers/uart \
  -IDrivers/adc \
  -IDrivers/pwm \
  -IDrivers/gpio \
  -IDrivers/flash \
  -IInc \
  -ILib/STM32F1xx_HAL_Driver/Inc \
  -ILib/CMSIS/Device/ST/STM32F1xx/Include \
  -ILib/CMSIS/Include \
  -ILib/FreeRTOS/Source/include \
  -ILib/FreeRTOS/Source/portable/GCC/ARM_CM3

# Compile flags
C_DEFS = \
  -DSTM32F103xE \
  -DUSE_HAL_DRIVER

CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) -Wall -Wextra -fdata-sections -ffunction-sections -std=c99
ASFLAGS = $(MCU)

# Release vs Debug
ifdef DEBUG
  CFLAGS += -g -gdwarf-2 -O0
else
  CFLAGS += -Os
endif

# Linker
LDSCRIPT = STM32F103VETx_FLASH.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

#############################################################################
# Build rules
#############################################################################

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin size

OBJECTS = $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(CP) -O binary $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SZ) $<

flash: $(BUILD_DIR)/$(TARGET).bin
	st-flash write $< 0x08000000

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean flash size
