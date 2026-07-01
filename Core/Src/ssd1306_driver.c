#include "ssd1306_driver.h"

// 顯存緩衝區：128x64 / 8 = 1024 Bytes
static uint8_t SSD1306_Buffer[128 * 8];

// 寫入指令：控制字節 0x00 代表接下來發送的字節是指令
void ssd1306_WriteCommand(I2C_HandleTypeDef *hi2c, uint8_t command) {
    HAL_I2C_Mem_Write(hi2c, SSD1306_I2C_ADDR, 0x00, 1, &command, 1, HAL_MAX_DELAY);
}

// 簡單的 OLED 初始化序列
uint8_t ssd1306_Init(I2C_HandleTypeDef *hi2c) {
    ssd1306_WriteCommand(hi2c, 0xAE); // 關閉螢幕
    ssd1306_WriteCommand(hi2c, 0x20); // 設定記憶體定址模式
    ssd1306_WriteCommand(hi2c, 0x00); // 選擇水平定址模式 (Horizontal Addressing Mode)
    ssd1306_WriteCommand(hi2c, 0xB0); // 設定頁位址
    ssd1306_WriteCommand(hi2c, 0xC8); // 設定 COM 掃描方向
    ssd1306_WriteCommand(hi2c, 0x00); // 設定低欄位址
    ssd1306_WriteCommand(hi2c, 0x10); // 設定高欄位址
    ssd1306_WriteCommand(hi2c, 0x40); // 設定起始行
    ssd1306_WriteCommand(hi2c, 0x81); // 對比度控制
    ssd1306_WriteCommand(hi2c, 0xFF); // 設定對比度為最大值 (最亮)
    ssd1306_WriteCommand(hi2c, 0xA1); // 設定段重對映
    ssd1306_WriteCommand(hi2c, 0xA6); // 正常顯示
    ssd1306_WriteCommand(hi2c, 0xA8); // 設定多工比例 (Multiplex Ratio)
    ssd1306_WriteCommand(hi2c, 0x3F); // 設定 Mux Ratio 為 63 (對應 64 行垂直高度)
    ssd1306_WriteCommand(hi2c, 0xAF); // 開啟螢幕

    return 1; // 初始化成功
}

//它會修改記憶體中的 SSD1306_Buffer 陣列。
// 若傳入 0（黑），則將整個緩衝區清零；
// 若傳入非零值（白），則將所有點陣位元設為 1。
void ssd1306_Fill(uint8_t color) {
    for (int i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = (color == 0) ? 0x00 : 0xFF;
    }
}

//將緩衝區資料寫入硬體。
// 將 1024 Bytes 的顯存資料分為 8 個頁面（Page）
// 透過 I2C 一頁一頁地傳輸給 SSD1306
void ssd1306_UpdateScreen(I2C_HandleTypeDef *hi2c) {
    for (uint8_t i = 0; i < 8; i++) {
        ssd1306_WriteCommand(hi2c, 0xB0 + i);
        ssd1306_WriteCommand(hi2c, 0x00);
        ssd1306_WriteCommand(hi2c, 0x10);
        // 寫入數據：控制字節 0x40 代表後續為顯存數據
        HAL_I2C_Mem_Write(hi2c, SSD1306_I2C_ADDR, 0x40, 1, &SSD1306_Buffer[i * 128], 128, HAL_MAX_DELAY);
    }
}







