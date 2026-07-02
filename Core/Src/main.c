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
#include "cmsis_os.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h> // 引入標準輸入輸出函式庫
#include "usbd_cdc_if.h" // 引入 USB CDC 傳輸函式庫
#include "mpu6050_driver.h"
#include "bme280_driver.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
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

// 跨檔案共享的全域 BME280 校準參數
BME280_Calib_Data bme_calib;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include "usbd_core.h"

/*
 * 目前你的 I2C1 實際使用：
 * PB6 = I2C1_SCL
 * PB7 = I2C1_SDA
 *
 * MPU6050 / BME280 / OLED 都應該並聯到這一組線上。
 */
#define I2C1_RECOVERY_GPIO_PORT   GPIOB
#define I2C1_RECOVERY_SCL_PIN     GPIO_PIN_6
#define I2C1_RECOVERY_SDA_PIN     GPIO_PIN_7

/*
 * I2C1 Bus Recovery
 *
 * 用途：
 *   當 STM32 被 NRST reset，但外部 I2C slave 沒有斷電時，
 *   slave 可能還卡在上一筆 I2C transaction 中。
 *
 *   這時 SDA 可能被 slave 拉 Low，導致 I2C bus 不是 idle 狀態，
 *   STM32 重新開機後就可能掃不到任何 I2C 裝置。
 *
 * 做法：
 *   1. 先把 SCL/SDA 暫時改成 GPIO open-drain
 *   2. 釋放 SCL/SDA，讓上拉電阻把線拉 High
 *   3. 手動對 SCL 打 9 個 clock
 *   4. 手動產生 STOP condition
 *   5. 後面再呼叫 MX_I2C1_Init()，把 PB6/PB7 交回 I2C peripheral
 */
void I2C1_BusRecovery(void) {
  //建立一個 GPIO 設定用的結構變數，並把裡面所有欄位清成 0。
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*
    * 要操作 GPIOB，必須先開 GPIOB clock。
    * 因為 PB6/PB7 都屬於 GPIOB。
    */
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*
    * 暫時把 PB6/PB7 設成一般 GPIO open-drain 輸出。
    *
    * 為什麼用 open-drain？
    *   I2C 的線路不能主動硬推 High。
    *   High = 放手，靠上拉電阻拉到 3.3V
    *   Low  = 晶片內部 transistor 導通到 GND
    *
    * 這樣才不會跟外部 MPU6050 / BME280 / OLED 打架。
    */
  GPIO_InitStruct.Pin = I2C1_RECOVERY_SCL_PIN | I2C1_RECOVERY_SDA_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(I2C1_RECOVERY_GPIO_PORT, &GPIO_InitStruct);

  /*
    * 先釋放 SCL / SDA。
    * GPIO_PIN_SET   = 輸出 High
    * GPIO_PIN_RESET = 輸出 Low
    * 在 open-drain 模式下，WritePin SET 的意思不是主動輸出 3.3V，
    * 而是「放手，不拉 Low」。
    *
    * 如果外部裝置也沒有拉 Low，
    * SCL/SDA 會被上拉電阻拉回 High。
    */
  HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SCL_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(1);

  /*
    * 手動對 SCL 打 9 個 clock。
    *
    * I2C 每傳 1 byte 是：
    *   8 個 data bit + 1 個 ACK/NACK bit
    *
    * 如果某個 slave 卡在傳輸中間，
    * 這 9 個 clock 可以讓它把目前這個 byte 流程走完，
    * 有機會釋放 SDA。
    */
  for (int i = 0; i < 9; i++) {
      //SCL 拉 Low
      HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SCL_PIN, GPIO_PIN_RESET);
      HAL_Delay(1);

      //SCL 放手，讓它回 High
      HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SCL_PIN, GPIO_PIN_SET);
      HAL_Delay(1);
  }

  /*
    * 手動產生 I2C STOP condition。
    *
    * STOP condition 定義：
    *   當 SCL = High 時，SDA 從 Low 變 High。
    * 這會告訴 I2C slave：
    *   這筆 transaction 結束了，回到 idle 狀態。
    */

  //先確保 SDA 被拉 Low
  HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SDA_PIN, GPIO_PIN_RESET);
  HAL_Delay(1);

  // 確保 SCL 是 High
  HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SCL_PIN, GPIO_PIN_SET);
  HAL_Delay(1);

  // SDA 從 Low 釋放成 High 這一瞬間就是 STOP condition
  HAL_GPIO_WritePin(I2C1_RECOVERY_GPIO_PORT, I2C1_RECOVERY_SDA_PIN, GPIO_PIN_SET);
  HAL_Delay(1);
}


// 告訴編譯器 hUsbDeviceFS 在其他檔案裡
extern USBD_HandleTypeDef hUsbDeviceFS;

// 覆寫 _write 函數，讓 printf 透過 USB CDC 輸出
int _write(int file, char *ptr, int len) {
    // 如果 USB 還沒被電腦成功識別並掛載，直接丟棄數據，絕對不要阻塞！
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
        return len; 
    }

    // 超時機制 最多等 5 次，送不出去就算了，避免死鎖
    uint8_t timeout = 5; 
    while(CDC_Transmit_FS((uint8_t*)ptr, len) == USBD_BUSY && timeout > 0) {
        // 在 RTOS 環境下，儘量使用 osDelay 釋放 CPU
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            osDelay(1);//睡覺並讓出 CPU（非阻塞）
        } else {
            HAL_Delay(1); // 阻塞等待 1 毫秒 因為這時RTOS還沒有啟動 沒有其他任務
        }
        timeout--;
    }
    return len;
}

void I2C_Scan(void)
{
    printf("Scanning I2C bus...\r\n");

    for (uint8_t addr = 1; addr < 128; addr++)
    {
        HAL_StatusTypeDef result;

        result = HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 2, 20);

        if (result == HAL_OK)
        {
            printf("I2C device found at 0x%02X\r\n", addr);
        }
    }

    printf("I2C scan done.\r\n");
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
  // 確保reset stm32會順道初始化前先 recovery
  I2C1_BusRecovery();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  MX_USB_DEVICE_Init();
  
  // 給 USB Host 一點時間完成 Enumeration。
  // MX_USB_DEVICE_Init() 只是啟動 USB Device，
  // 不代表 Windows 已經建立好 COM Port。
  uint32_t usb_wait_start = HAL_GetTick();

  while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
      if (HAL_GetTick() - usb_wait_start > 3000) {
          break;
      }
      HAL_Delay(10);
  }

  if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
      printf("===== STM32 Data Logger Booting... =====\r\n");
      HAL_Delay(3000);
  }

  //第一次掃描：確認硬體接線與三個 I2C slave 都存在
  I2C_Scan();
  HAL_Delay(3000);

   // 初始化 MPU6050
  if (MPU6050_Init(&hi2c1)) {
      printf("MPU6050 Initialized Successfully!\r\n");
  } else {
      printf("ERROR: MPU6050 Init Failed!\r\n");
  }

  // 初始化 BME280
  if (BME280_Init(&hi2c1)) {
      BME280_Read_Calibration(&hi2c1, &bme_calib);
      printf("BME280 Initialized Successfully!\r\n");
  } else {
      printf("ERROR: BME280 Init Failed!\r\n");
  }
  
  // 初始化 ssd1306
  if (ssd1306_Init()) {
      // 清除顯示緩衝區
      // ssd1306_Fill(0x01); 
      // ssd1306_UpdateScreen();
      printf("SSD1306 Initialized Successfully!\r\n");
  } else {
      printf("ERROR: SSD1306 Init Failed!\r\n");
  }

  //第二次掃描：確認 OLED 初始化後，I2C bus 沒被搞壞
  I2C_Scan();
  printf("I2C scan after SSD1306 init done.\r\n");
  /* USER CODE END 2 */

  /* Init scheduler */
  // 初始化 RTOS kernel 本體
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  // 建立我自己的 RTOS 物件
  MX_FREERTOS_Init();

  /* Start scheduler */
  // 之後，CPU 的控制權就全權交給了排程器
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // CPU 的控制權就全權交給了排程器後就不會再跑這裡的while了
  while (1) {
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
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM5 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM5)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
