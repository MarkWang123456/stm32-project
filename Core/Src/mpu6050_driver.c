#include "mpu6050_driver.h"

// MPU6050 初始化函式：檢查 ID 並載入預設設定
HAL_StatusTypeDef MPU6050_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t check = 0U;
    uint8_t data = 0U;
    HAL_StatusTypeDef status;

    HAL_Delay(100U);

    // 1. 檢查 WHO_AM_I 暫存器，確認硬體連線狀態
    status = HAL_I2C_Mem_Read(
        hi2c,
        MPU6050_ADDR,
        MPU6050_WHO_AM_I,
        I2C_MEMADD_SIZE_8BIT,
        &check,
        1U,
        1000U
    );

    if (status != HAL_OK)
    {
        return status;
    }

    if (check != MPU6050_WHO_AM_I_VALUE) return HAL_ERROR; // 若 ID 不符，返回錯誤

    // 2. 喚醒 MPU6050：寫入預設電源管理設定 (取消睡眠模式)
    data = MPU6050_DEFAULT_PWR_MGMT;
    status = HAL_I2C_Mem_Write(
        hi2c,
        MPU6050_ADDR,
        MPU6050_PWR_MGMT_1,
        I2C_MEMADD_SIZE_8BIT,
        &data,
        1U,
        1000U
    );

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(100U);

    // 3. 設定採樣率分頻：決定數據更新頻率
    data = MPU6050_DEFAULT_SMPLRT_DIV;
    status = HAL_I2C_Mem_Write(
        hi2c,
        MPU6050_ADDR,
        MPU6050_SMPLRT_DIV,
        I2C_MEMADD_SIZE_8BIT,
        &data,
        1U,
        1000U
    );

    if (status != HAL_OK)
    {
        return status;
    }

    // 4. 設定 DLPF (數位低通濾波器)：平滑化加速度與陀螺儀數據
    data = MPU6050_DEFAULT_CONFIG;
    status = HAL_I2C_Mem_Write(
        hi2c,
        MPU6050_ADDR,
        MPU6050_CONFIG,
        I2C_MEMADD_SIZE_8BIT,
        &data,
        1U,
        1000U
    );

    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_OK;
}

// 整合讀取：一次讀取所有感測數據 (包含加速度與陀螺儀)
HAL_StatusTypeDef MPU6050_ReadAll(I2C_HandleTypeDef *hi2c, MPU6050_Data *accel, MPU6050_Data *gyro) {
    uint8_t raw[14];
    // 從 ACCEL_XOUT_H (0x3B) 開始連續讀取 14 Bytes
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR, MPU6050_ACCEL_XOUT_H, I2C_MEMADD_SIZE_8BIT, raw, 14, 1000);
    
    if (status != HAL_OK) {
        return status;
    }

    // 加速度計 raw
    accel->raw_x = (int16_t)((raw[0] << 8) | raw[1]);
    accel->raw_y = (int16_t)((raw[2] << 8) | raw[3]);
    accel->raw_z = (int16_t)((raw[4] << 8) | raw[5]);

    // 加速度計換算值
    accel->x = accel->raw_x / 16384.0f;
    accel->y = accel->raw_y / 16384.0f;
    accel->z = accel->raw_z / 16384.0f;

    // 陀螺儀 raw
    gyro->raw_x = (int16_t)((raw[8] << 8) | raw[9]);
    gyro->raw_y = (int16_t)((raw[10] << 8) | raw[11]);
    gyro->raw_z = (int16_t)((raw[12] << 8) | raw[13]);

    // 陀螺儀換算值
    gyro->x = gyro->raw_x / 131.0f;
    gyro->y = gyro->raw_y / 131.0f;
    gyro->z = gyro->raw_z / 131.0f;

    return HAL_OK;
}