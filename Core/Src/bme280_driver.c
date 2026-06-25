#include "bme280_driver.h"

// Bosch 提供的 BME280 補償運算邏輯中，需要用到這個全域變數來暫存中間值
// 這是一個 Bosch 定義的微調參數，用於氣壓與濕度計算
int32_t t_fine;

// 1. 初始化函數：檢查 ID 與連線
uint8_t BME280_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t chip_id = 0;
    uint8_t data;
    
    // 讀取 Chip ID
    HAL_I2C_Mem_Read(hi2c, BME280_I2C_ADDR, BME280_ID_REG, I2C_MEMADD_SIZE_8BIT, &chip_id, 1, 1000);
    
    if (chip_id != BME280_ID_VALUE) return 0; // 若 ID 不符，連線失敗

    // 設定濕度過採樣 (ctrl_hum)
    data = BME280_DEFAULT_CTRL_HUM;
    HAL_I2C_Mem_Write(hi2c, BME280_I2C_ADDR, BME280_CTRL_HUM, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);

    // 設定溫度與氣壓過採樣及模式 (ctrl_meas)
    data = BME280_DEFAULT_CTRL_MEAS;
    HAL_I2C_Mem_Write(hi2c, BME280_I2C_ADDR, BME280_CTRL_MEAS, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);

    return 1; // 連線與設定成功
}

// 2. 讀取校準參數：Bosch 的 DNA 數據
void BME280_Read_Calibration(I2C_HandleTypeDef *hi2c, BME280_Calib_Data *calib) {
    uint8_t buffer[24] = {0};
    uint8_t h_buf[7] = {0};
    
    // 從校準參數起始位址讀取 24 Bytes
    HAL_I2C_Mem_Read(hi2c, BME280_I2C_ADDR, BME280_CALIB_T_P_START, I2C_MEMADD_SIZE_8BIT, buffer, 24, 1000);

    calib->dig_T1 = (uint16_t)(buffer[1] << 8 | buffer[0]);
    calib->dig_T2 = (int16_t)(buffer[3] << 8 | buffer[2]);
    calib->dig_T3 = (int16_t)(buffer[5] << 8 | buffer[4]);
    
    calib->dig_P1 = (uint16_t)(buffer[7] << 8 | buffer[6]);
    calib->dig_P2 = (int16_t)(buffer[9] << 8 | buffer[8]);
    calib->dig_P3 = (int16_t)(buffer[11] << 8 | buffer[10]);
    calib->dig_P4 = (int16_t)(buffer[13] << 8 | buffer[12]);
    calib->dig_P5 = (int16_t)(buffer[15] << 8 | buffer[14]);
    calib->dig_P6 = (int16_t)(buffer[17] << 8 | buffer[16]);
    calib->dig_P7 = (int16_t)(buffer[19] << 8 | buffer[18]);
    calib->dig_P8 = (int16_t)(buffer[21] << 8 | buffer[20]);
    calib->dig_P9 = (int16_t)(buffer[23] << 8 | buffer[22]);

    // 濕度校準參數讀取
    HAL_I2C_Mem_Read(hi2c, BME280_I2C_ADDR, BME280_CALIB_H1_ADDR, I2C_MEMADD_SIZE_8BIT, &calib->dig_H1, 1, 1000);

    // 讀取 H2-H6 (0xE1-0xE7)
    HAL_I2C_Mem_Read(hi2c, BME280_I2C_ADDR, BME280_CALIB_H2_ADDR, I2C_MEMADD_SIZE_8BIT, h_buf, 7, 1000);

    // 解析 H2-H6
    calib->dig_H2 = (int16_t)((h_buf[1] << 8) | h_buf[0]);// 將高位元組左移並與低位元組進行位元合併，還原成 16-bit 數值
    calib->dig_H3 = h_buf[2];
    calib->dig_H4 = (int16_t)((h_buf[3] << 4) | (h_buf[4] & 0x0F));// 將暫存器資料組合成 12-bit 的 H4 參數
    calib->dig_H5 = (int16_t)((h_buf[5] << 4) | (h_buf[4] >> 4));
    calib->dig_H6 = (int8_t)h_buf[6];
}

// 3. 讀取數據並進行補償運算
void BME280_ReadAll(I2C_HandleTypeDef *hi2c, BME280_Calib_Data *calib, BME280_Data *data) {
    uint8_t raw[8];
    
        // 從 0xF7 開始連續讀取 8 個 Byte (P, T, H)
    if (HAL_I2C_Mem_Read(hi2c, BME280_I2C_ADDR, BME280_DATA_REG, I2C_MEMADD_SIZE_8BIT, raw, 8, 1000) != HAL_OK) return;

    int32_t adc_P = (int32_t)((raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4));
    int32_t adc_T = (int32_t)((raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4));
    int32_t adc_H = (int32_t)((raw[6] << 8) | raw[7]);

    // 溫度補償
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calib->dig_T1 << 1))) * ((int32_t)calib->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib->dig_T1)) * ((adc_T >> 4) - ((int32_t)calib->dig_T1))) >> 12) * ((int32_t)calib->dig_T3)) >> 14;
    t_fine = var1 + var2;
    data->temp = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    // 氣壓補償
    int64_t p_var1 = ((int64_t)t_fine) - 128000;
    int64_t p_var2 = p_var1 * p_var1 * (int64_t)calib->dig_P6;
    p_var2 = p_var2 + ((p_var1 * (int64_t)calib->dig_P5) << 17);
    p_var2 = p_var2 + (((int64_t)calib->dig_P4) << 35);
    p_var1 = ((p_var1 * p_var1 * (int64_t)calib->dig_P3) >> 8) + ((p_var1 * (int64_t)calib->dig_P2) << 12);
    p_var1 = (((((int64_t)1) << 47) + p_var1)) * ((int64_t)calib->dig_P1) >> 33;

    if (p_var1 != 0) {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p_var2) * 3125) / p_var1;
        p_var1 = (((int64_t)calib->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        p_var2 = (((int64_t)calib->dig_P8) * p) >> 19;
        p = ((p + p_var1 + p_var2) >> 8) + (((int64_t)calib->dig_P7) << 4);
        data->press = (float)p / 256.0f;
    }

    // 濕度補償
    int32_t v_x1 = (t_fine - (int32_t)76800);
    v_x1 = (((((adc_H << 14) - (((int32_t)calib->dig_H4) << 20) - (((int32_t)calib->dig_H5) * v_x1)) + 
              ((int32_t)16384)) >> 15) * (((((((v_x1 * ((int32_t)calib->dig_H6)) >> 10) * (((v_x1 * ((int32_t)calib->dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)calib->dig_H2) + 8192) >> 14));
    v_x1 = (v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)calib->dig_H1)) >> 4));
    v_x1 = (v_x1 < 0 ? 0 : v_x1);
    v_x1 = (v_x1 > 419430400 ? 419430400 : v_x1);
    data->hum = (float)(v_x1 >> 12) / 1024.0f;
}