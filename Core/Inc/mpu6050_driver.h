#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include "stm32f4xx_hal.h"

// MPU6050 I2C 地址 (AD0 接 GND)(左移 1 位變成 0xD0)
#define MPU6050_ADDR (0x68 << 1)

// MPU6050 常用暫存器位址
#define MPU6050_SMPLRT_DIV   0x19 // 採樣率分頻暫存器 (Sample Rate Divider)
#define MPU6050_CONFIG       0x1A // 設定 DLPF 低通濾波器與幀同步
#define MPU6050_PWR_MGMT_1   0x6B // 電源管理暫存器 1 (用於喚醒晶片)
#define MPU6050_WHO_AM_I     0x75 // 設備身份 ID 暫存器 (回傳值應為 0x68)
#define MPU6050_ACCEL_XOUT_H 0x3B // 加速度計 X 軸高位元數據起始暫存器

// MPU6050 預設初始化參數
#define MPU6050_DEFAULT_PWR_MGMT   0x00 // 喚醒模式
#define MPU6050_DEFAULT_SMPLRT_DIV 0x04 // 採樣率分頻
#define MPU6050_DEFAULT_CONFIG     0x03 // DLPF 設定

// 設備身份識別值
#define MPU6050_WHO_AM_I_VALUE 0x68 // MPU6050 預期回傳的 ID

// 加速度計與陀螺儀數據結構
typedef struct {
    float x;
    float y;
    float z;
} MPU6050_Data;

// 函式宣告
uint8_t MPU6050_Init(I2C_HandleTypeDef *hi2c);
void MPU6050_ReadAll(I2C_HandleTypeDef *hi2c, MPU6050_Data *accel, MPU6050_Data *gyro);

#endif