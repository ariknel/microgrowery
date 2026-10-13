# Build for CH32F4P6 (WCH, ARM Cortex-M4F). Needs WCH's EVT peripheral
# library vendored under Libraries/ — not included here, see README.md for
# what to download and where it goes. Paths below match WCH's typical EVT
# package layout as of this writing; adjust LIB_DIR/STARTUP/LDSCRIPT if
# your extracted copy is laid out differently.

TARGET     = fan_controller
BUILD_DIR  = build

LIB_DIR    = Libraries
CMSIS_DIR  = $(LIB_DIR)/CMSIS
PERIPH_DIR = $(LIB_DIR)/Peripheral
STARTUP    = $(LIB_DIR)/Startup/startup_ch32f40x.S
LDSCRIPT   = $(LIB_DIR)/Link.ld

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# CH32F4 is an ARM Cortex-M4F core (hardware FPU) — verify FPU ABI
# (hard vs soft float) matches what your Libraries/ build expects; WCH's
# EVT examples typically use hard-float.
MCU_FLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16

INCLUDES = \
    -IUser \
    -I$(CMSIS_DIR)/Core \
    -I$(CMSIS_DIR)/Device \
    -I$(PERIPH_DIR)/inc

CFLAGS  = $(MCU_FLAGS) $(INCLUDES) -Wall -Og -g3 -ffunction-sections -fdata-sections -std=c11
LDFLAGS = $(MCU_FLAGS) -T$(LDSCRIPT) -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs

SRCS = \
    User/main.c \
    User/ch32f4xx_it.c \
    $(wildcard $(PERIPH_DIR)/src/*.c)

OBJS = $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.c=.o))) $(BUILD_DIR)/startup.o
vpath %.c User $(PERIPH_DIR)/src

.PHONY: all clean

all: $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin
	$(SIZE) $(BUILD_DIR)/$(TARGET).elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/startup.o: $(STARTUP) | $(BUILD_DIR)
	$(CC) $(MCU_FLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)
