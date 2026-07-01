#ifndef SSD1306_DRIVER_H
#define SSD1306_DRIVER_H

#include "stm32f4xx_hal.h"

#define SSD1306_I2C_ADDR (0x3C << 1) 

uint8_t ssd1306_Init(I2C_HandleTypeDef *hi2c);
void ssd1306_Fill(uint8_t color);
void ssd1306_UpdateScreen(I2C_HandleTypeDef *hi2c);
void ssd1306_WriteCommand(I2C_HandleTypeDef *hi2c, uint8_t command);
void ssd1306_WriteString(uint16_t x, uint16_t y, char* str, FontDef font, uint8_t color);

#endif