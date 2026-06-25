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

  uint8_t mpu6050_addr = 0xD0; // MPU6050 的 I2C 位址 (AD0 接 GND)
  uint8_t who_am_i_reg = 0x75; // WhoAmI 暫存器位址
  uint8_t mpu_id = 0;
  
  uint8_t pwr_mgmt_1_reg = 0x6B; // 電源管理暫存器 1
  uint8_t wake_up_data = 0x00;   // 寫入 0x00 解除睡眠模式

  printf("Scanning I2C Bus...\r\n");
  
  // 1. 檢查裝置是否存在
  if (HAL_I2C_IsDeviceReady(&hi2c1, mpu6050_addr, 3, 1000) == HAL_OK) {
      printf("Device Found at 0xD0!\r\n");
      
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

      // 1. 寫入 CONFIG 暫存器 (設定濾波器)
      if (HAL_I2C_Mem_Write(&hi2c1, mpu6050_addr, config_reg, I2C_MEMADD_SIZE_8BIT, &config_data, 1, 1000) == HAL_OK) {
          printf("  -> DLPF Configured (0x1A = 0x03)\r\n");
      } else {
          printf("  -> ERROR: Failed to config DLPF!\r\n");
      }

      // 2. 寫入 SMPLRT_DIV 暫存器 (設定分頻)
      if (HAL_I2C_Mem_Write(&hi2c1, mpu6050_addr, smplrt_div_reg, I2C_MEMADD_SIZE_8BIT, &smplrt_div_data, 1, 1000) == HAL_OK) {
          printf("  -> Sample Rate Configured to 200Hz (0x19 = 0x04)\r\n");
      } else {
          printf("  -> ERROR: Failed to config Sample Rate!\r\n");
      }
      // ===============================================
      
  } else {
      printf("ERROR: Device NOT Found at 0xD0!\r\n");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
