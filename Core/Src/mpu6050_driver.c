#include "mpu6050_driver.h"

// MPU6050 初始化函式：檢查 ID 並載入預設設定
uint8_t MPU6050_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t check, data;

    // 1. 檢查 WHO_AM_I 暫存器，確認硬體連線狀態
    HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, MPU6050_WHO_AM_I, I2C_MEMADD_SIZE_8BIT, &check, 1, 1000);
    if (check != MPU6050_WHO_AM_I_VALUE) return 0; // 若 ID 不符，返回錯誤

    // 2. 喚醒 MPU6050：寫入預設電源管理設定 (取消睡眠模式)
    data = MPU6050_DEFAULT_PWR_MGMT;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, MPU6050_PWR_MGMT_1, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);

    // 3. 設定採樣率分頻：決定數據更新頻率
    data = MPU6050_DEFAULT_SMPLRT_DIV;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, MPU6050_SMPLRT_DIV, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);

    // 4. 設定 DLPF (數位低通濾波器)：平滑化加速度與陀螺儀數據
    data = MPU6050_DEFAULT_CONFIG;
    HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR, MPU6050_CONFIG, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);

    return 1; // 初始化成功
}

// 整合讀取：一次讀取所有感測數據 (包含加速度與陀螺儀)
void MPU6050_ReadAll(I2C_HandleTypeDef *hi2c, MPU6050_Data *accel, MPU6050_Data *gyro) {
    uint8_t raw[14];
    // 從 ACCEL_XOUT_H (0x3B) 開始連續讀取 14 Bytes
    HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, I2C_MEMADD_SIZE_8BIT, raw, 14, 1000);
    
    // 加速度計
    accel->x = (int16_t)(raw[0] << 8 | raw[1]) / 16384.0f;
    accel->y = (int16_t)(raw[2] << 8 | raw[3]) / 16384.0f;
    accel->z = (int16_t)(raw[4] << 8 | raw[5]) / 16384.0f;

    // 陀螺儀 (raw[6], raw[7] 為溫度，故跳過)
    gyro->x = (int16_t)(raw[8] << 8 | raw[9]) / 131.0f;
    gyro->y = (int16_t)(raw[10] << 8 | raw[11]) / 131.0f;
    gyro->z = (int16_t)(raw[12] << 8 | raw[13]) / 131.0f;
}