# Raspberry Pi SPI master with SSD1306 OLED support

CC := gcc
TARGET := spi_master_raw
BUILD_DIR := build

COMMON_INC := ../Core/Inc
COMMON_SRC := ../Core/Src/system_packet.c

OBJECTS := \
	$(BUILD_DIR)/spi_master_raw.o \
	$(BUILD_DIR)/system_packet.o \
	$(BUILD_DIR)/oled_display.o

DEPS := $(OBJECTS:.o=.d)

CPPFLAGS := -I$(COMMON_INC) -I.
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -MMD -MP
LDLIBS := -lm

.PHONY: all clean rebuild run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/spi_master_raw.o: spi_master_raw.c oled_display.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/system_packet.o: $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/oled_display.o: oled_display.c oled_display.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

-include $(DEPS)