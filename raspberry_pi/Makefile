# Raspberry Pi SPI master build

CC := gcc
TARGET := spi_master_raw
BUILD_DIR := build

# This Makefile is intended to be placed in:
#   stm32-project/raspberry_pi/
COMMON_INC := ../Core/Inc
COMMON_SRC := ../Core/Src/system_packet.c

SOURCES := spi_master_raw.c $(COMMON_SRC)
OBJECTS := $(BUILD_DIR)/spi_master_raw.o \
           $(BUILD_DIR)/system_packet.o
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS := -I$(COMMON_INC)
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -MMD -MP
LDLIBS := -lm

.PHONY: all clean rebuild run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/spi_master_raw.o: spi_master_raw.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/system_packet.o: $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

-include $(DEPS)
