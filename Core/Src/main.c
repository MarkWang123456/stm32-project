/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h> // 引入標準輸入輸出函式庫
#include "usbd_cdc_if.h" // 引入 USB CDC 傳輸函式庫
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// 覆寫 _write 函數，讓 printf 透過 USB CDC 輸出
int _write(int file, char *ptr, int len) {
    // 一直嘗試傳送，直到 USB 不再回報 BUSY 為止
    while(CDC_Transmit_FS((uint8_t*)ptr, len) == USBD_BUSY) {
        HAL_Delay(1); // 等待 1 毫秒讓上一筆資料傳完
    }
    return len;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(2000); // 開機先等兩秒，讓 USB 和感測器都準備好
  printf("===== STM32 Data Logger Booting... =====\r\n");
  // 系統狀態旗標 (0代表失敗，1代表成功)
  uint8_t system_ready = 0;

  // MPU-6000
  uint8_t mpu6050_addr = 0x68 << 1; // MPU6050 的 I2C 位址 (AD0 接 GND)(左移 1 位變成 0xD0)
  uint8_t who_am_i_reg = 0x75; // WhoAmI 暫存器位址
  uint8_t mpu_id = 0;
  
  uint8_t pwr_mgmt_1_reg = 0x6B; // 電源管理暫存器 1
  uint8_t wake_up_data = 0x00;   // 寫入 0x00 解除睡眠模式

  //BME280
  uint8_t bme280_addr = 0x77 << 1; // BME280 的 I2C 門牌 (左移 1 位變成 0xEC)
       
  uint8_t ctrl_hum_reg = 0xF2; // 濕度控制暫存器 (ctrl_hum)
  uint8_t ctrl_hum_data = 0x01;  // 0x01 代表濕度過採樣率 x1，通常有 x1, x2, x4, x8, x16 可以選）

  uint8_t ctrl_meas_reg = 0xF4; //測量控制暫存器 (ctrl_meas)
  // 0x27 的二進位是 00100111：
  // 溫度過採樣率x1 (001) | 氣壓過採樣率x1 (001) | Normal Mode (11) = 0x27
  uint8_t ctrl_meas_data = 0x27; 

  printf("Scanning I2C Bus...\r\n");
  uint8_t mpu_ok = (HAL_I2C_IsDeviceReady(&hi2c1, mpu6050_addr, 3, 1000) == HAL_OK);
  uint8_t bme_ok = (HAL_I2C_IsDeviceReady(&hi2c1, bme280_addr, 3, 1000) == HAL_OK);

  // 1. 檢查裝置是否存在
  if (mpu_ok && bme_ok) {
      printf("All Devices Found (MPU6050 & BME280)!\r\n");
      system_ready = 1; //兩個都在，標記系統準備就緒！
      
      // 2. 喚醒 MPU6050 (寫入 0x00 到 0x6B)
      if (HAL_I2C_Mem_Write(&hi2c1, mpu6050_addr, pwr_mgmt_1_reg, I2C_MEMADD_SIZE_8BIT, &wake_up_data, 1, 1000) == HAL_OK) {
          printf("MPU6050 Wake-up Command Sent!\r\n");
      } else {
          printf("ERROR: Failed to wake up MPU6050!\r\n");
      }

      // 3. 讀取 WhoAmI 驗證身分
      if (HAL_I2C_Mem_Read(&hi2c1, mpu6050_addr, who_am_i_reg, I2C_MEMADD_SIZE_8BIT, &mpu_id, 1, 1000) == HAL_OK) {
          printf("Success! MPU6050 ID is: 0x%02X\r\n", mpu_id);
      } else {
          printf("Read WhoAmI Failed!\r\n");
      }

      // === 任務 1-4：採樣率與低通濾波器配置 ===
      uint8_t config_reg = 0x1A; //暫存器 0x1A (CONFIG)：數位低通濾波器 (DLPF) 設置
      uint8_t config_data = 0x03; // DLPF_CFG = 3 (內部採樣率 1kHz，濾除高頻雜訊)
      
      uint8_t smplrt_div_reg = 0x19; //暫存器 0x19 (SMPLRT_DIV)：採樣率分頻器設置
      uint8_t smplrt_div_data = 0x04; // 分頻器 = 4，最終採樣率 = 1000Hz / (1 + 4) = 200Hz

      printf("Configuring MPU6050 Sample Rate to 200Hz...\r\n");
      // 寫入 CONFIG 暫存器 (設定濾波器)
      if (HAL_I2C_Mem_Write(&hi2c1, mpu6050_addr, config_reg, I2C_MEMADD_SIZE_8BIT, &config_data, 1, 1000) == HAL_OK) {
          printf("  -> DLPF Configured (0x1A = 0x03)\r\n");
      } else {
          printf("  -> ERROR: Failed to config DLPF!\r\n");
      }

      // 寫入 SMPLRT_DIV 暫存器 (設定分頻)
      if (HAL_I2C_Mem_Write(&hi2c1, mpu6050_addr, smplrt_div_reg, I2C_MEMADD_SIZE_8BIT, &smplrt_div_data, 1, 1000) == HAL_OK) {
          printf("  -> Sample Rate Configured to 200Hz (0x19 = 0x04)\r\n");
      } else {
          printf("  -> ERROR: Failed to config Sample Rate!\r\n");
      }


      // === 任務 1-6：BME280 初始化 ===
      printf("Initializing BME280...\r\n");
      // 4. 務必先寫入濕度設定 (0xF2)
      if (HAL_I2C_Mem_Write(&hi2c1, bme280_addr, ctrl_hum_reg, I2C_MEMADD_SIZE_8BIT, &ctrl_hum_data, 1, 1000) == HAL_OK) {
          printf("  -> BME280 Humidity Configured (0xF2 = 0x01)\r\n");
      } else {
          printf("  -> ERROR: BME280 NOT Found or Failed at 0xF2!\r\n");
      }

      // 5. 再寫入溫度、氣壓設定，並喚醒進入 Normal Mode (0xF4)
      if (HAL_I2C_Mem_Write(&hi2c1, bme280_addr, ctrl_meas_reg, I2C_MEMADD_SIZE_8BIT, &ctrl_meas_data, 1, 1000) == HAL_OK) {
          printf("  -> BME280 Temp/Press Configured & Normal Mode Started (0xF4 = 0x27)\r\n");
      } else {
          printf("  -> ERROR: BME280 Failed at 0xF4!\r\n");
      }
      
  } else {
      if (!mpu_ok) printf("ERROR: MPU6050 (0xD0) NOT Found! Check wiring.\r\n");
      if (!bme_ok) printf("ERROR: BME280 NOT Found! (Try changing address to 0x76)\r\n");
      system_ready = 0; // 標記系統失敗
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    if(system_ready == 1){
      // === 任務 1-5：連續讀取 14 Bytes 原始數據並轉換物理量 ===
      uint8_t data_reg = 0x3B; // 起始位址 (ACCEL_XOUT_H)
      // 準備一個大陣列，一次接住 14 Bytes
      // 每個數值2bytes，加速度計X、Y、Z 三軸，溫度感測器，陀螺儀X、Y、Z 三軸
      uint8_t raw_data[14];    

      // === 裝「原始數據 (生肉)」的變數 ===
      // 用來存放從 MPU6050 剛拿出來、拼裝好的 16-bit ADC 刻度值
      int16_t raw_accel_x, raw_accel_y, raw_accel_z; // 加速度原始刻度
      int16_t raw_gyro_x, raw_gyro_y, raw_gyro_z;    // 陀螺儀原始刻度

      // === 裝「實體物理量 (熟肉)」的變數 ===
      // 用來存放除以靈敏度後，人類看得懂的真實數值 (包含小數點)
      float accel_x, accel_y, accel_z; // 真實重力加速度 (單位: g)
      float gyro_x, gyro_y, gyro_z;    // 真實旋轉角速度 (單位: dps, 度/秒)

      // 使用 I2C 一口氣連續讀取 14 Bytes
      if (HAL_I2C_Mem_Read(&hi2c1, 0xD0, data_reg, I2C_MEMADD_SIZE_8BIT, raw_data, 14, 1000) == HAL_OK) {
          
          // 1. 拼裝 16-bit 原始數據 (High Byte 左移 8 位元，然後與 Low Byte 做 OR 運算)
          raw_accel_x = (int16_t)(raw_data[0] << 8 | raw_data[1]);
          raw_accel_y = (int16_t)(raw_data[2] << 8 | raw_data[3]);
          raw_accel_z = (int16_t)(raw_data[4] << 8 | raw_data[5]);
          
          // raw_data[6] 和 [7] 是溫度數據，我們先略過不看
          
          raw_gyro_x  = (int16_t)(raw_data[8] << 8 | raw_data[9]);
          raw_gyro_y  = (int16_t)(raw_data[10] << 8 | raw_data[11]);
          raw_gyro_z  = (int16_t)(raw_data[12] << 8 | raw_data[13]);

          // 2. 轉換為實體物理量 (Float)
          // 根據 Datasheet，加速度預設範圍 ±2g(最大只能量到2倍的地球重力)
          // 對應靈敏度為 16384 LSB/g(int16_t範圍是-32,768 到 +32,767，最大是2g，除二就是16384)
          accel_x = (float)raw_accel_x / 16384.0f;
          accel_y = (float)raw_accel_y / 16384.0f;
          accel_z = (float)raw_accel_z / 16384.0f;

          // 陀螺儀預設範圍 ±250 dps(Degrees Per Second)(大約是 0.7 圈)，對應靈敏度為 131 LSB/dps(32767/250)
          gyro_x = (float)raw_gyro_x / 131.0f;
          gyro_y = (float)raw_gyro_y / 131.0f;
          gyro_z = (float)raw_gyro_z / 131.0f;

          // 3. 漂亮地印出包含小數點的物理量
          printf("Acc(g): X=%.2f Y=%.2f Z=%.2f | Gyr(dps): X=%.1f Y=%.1f Z=%.1f\r\n", 
                 accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);
          
      } else {
          printf("ERROR: Failed to read MPU6050 data!\r\n");
      }

      // 我們設定的採樣率是 200Hz (每 5 毫秒一筆)
      // 但為了避免 Tera Term 畫面刷太快讓眼睛瞎掉，我們先用 Delay 控制在每秒印 20 次就好
      HAL_Delay(50); 
    } else {
      // 如果感測器有問題，每 3 秒印一次警告，不再印 0.00 洗畫面
      printf("System Halted: Hardware error. Please fix and reset.\r\n");
      HAL_Delay(3000);
    }
     

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 15;
  RCC_OscInitStruct.PLL.PLLN = 144;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
