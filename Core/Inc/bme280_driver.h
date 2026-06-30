#ifndef BME280_DRIVER_H
#define BME280_DRIVER_H

#include "stm32f4xx_hal.h"

// BME280 的 I2C 門牌 (左移 1 位變成 0xEC)
#define BME280_I2C_ADDR (0x77 << 1) 

// BME280 常用暫存器位址
#define BME280_ID_REG      0xD0  // Chip ID
#define BME280_RESET_REG   0xE0  // Reset
#define BME280_DATA_REG    0xF7  // 數據輸出起始位址 (P, T, H)
#define BME280_CTRL_HUM    0xF2  // 濕度控制暫存器
#define BME280_CTRL_MEAS   0xF4  // 測量控制暫存器

// 校準參數專用位址
#define BME280_CALIB_T_P_START 0x88 // 溫度/氣壓參數起始 (T1-P9)
#define BME280_CALIB_H1_ADDR   0xA1 // 濕度參數 H1 起始
#define BME280_CALIB_H2_ADDR  0xE1 // 濕度校準參數起始位址 (H1-H6)

// 設備身份識別值
#define BME280_ID_VALUE    0x60  // BME280 預期回傳的 ID

// BME280 預設初始化參數 (可根據需求調整)
#define BME280_DEFAULT_CTRL_HUM  0x01 // Humidity x1
  // 0x27 的二進位是 00100111：
  // 溫度過採樣率x1 (001) | 氣壓過採樣率x1 (001) | Normal Mode (11) = 0x27
#define BME280_DEFAULT_CTRL_MEAS 0x27   


// 校準參數結構體 (用來存放 Bosch 的身份證)
// dig_T1 ~ dig_T3：溫度補償參數 佔 6 bytes
// dig_P1 ~ dig_P9：氣壓補償參數 佔18 bytes
// dig_H1 ~ dig_H6：濕度補償參數 佔 6 bytes
typedef struct {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint16_t dig_P1; int16_t dig_P2; int16_t dig_P3;
    int16_t dig_P4;  int16_t dig_P5; int16_t dig_P6;
    int16_t dig_P7;  int16_t dig_P8; int16_t dig_P9;
    uint8_t  dig_H1; int16_t dig_H2; uint8_t  dig_H3;
    int16_t dig_H4;  int16_t dig_H5; int8_t   dig_H6;
} BME280_Calib_Data;

// BME280 數據結構體
typedef struct {
    float temp;
    float press;
    float hum;
} BME280_Data;

// 初始化：包含檢查 ID 與設定 Normal Mode
uint8_t BME280_Init(I2C_HandleTypeDef *hi2c);

// 讀取校準參數：將參數從感測器讀出並存入結構體
uint8_t BME280_Read_Calibration(I2C_HandleTypeDef *hi2c, BME280_Calib_Data *calib);

// 讀取原始數據並轉換成實體單位 (溫度攝氏度、氣壓百帕、濕度)
uint8_t BME280_ReadAll(I2C_HandleTypeDef *hi2c, BME280_Calib_Data *calib, BME280_Data *data);
#endif // BME280_DRIVER_H