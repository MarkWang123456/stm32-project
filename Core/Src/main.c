/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : MPU6050 + USB CDC + FreeRTOS minimal stable baseline
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"  //CMSIS-RTOS介面標頭檔
#include "i2c.h"
#include "spi.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "usbd_core.h" //USB Device 核心層的介面
#include "usbd_cdc_if.h" //USB CDC(Communication Device Class) 類別的使用者介面
#include "mpu6050_driver.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
//板子 LED 是 Active Low
#define TEST_LED_PORT       GPIOC           //GPIO Port C
#define TEST_LED_PIN        GPIO_PIN_13     //GPIO Pin 13 (Test LED)
#define TEST_LED_ON         GPIO_PIN_RESET  //Low  → LED 亮
#define TEST_LED_OFF        GPIO_PIN_SET    //High → LED 熄滅
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
//  USB Device 的控制結構型別
extern USBD_HandleTypeDef hUsbDeviceFS;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
//設定 STM32 的系統時鐘
void SystemClock_Config(void);
//初始化 FreeRTOS 定義在freertos.c
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
//初始化 PC13 LED
static void TestLED_Init(void);
//在 Error_Handler() 裡，讓 LED 閃爍時產生間隔
static void BusyDelay(volatile uint32_t count);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  將 printf() 的輸出重新導向到 USB CDC (Virtual COM Port)，讓 printf() 能透過 USB 傳到電腦。
  *
  * 注意事項：
  * - USB 尚未完成枚舉 (Enumeration) 時，不會等待，而是直接丟棄此次輸出。
  * - FreeRTOS Scheduler 已啟動時，若 USB 忙碌 (BUSY)，會使用 osDelay() 在 USB 忙碌時暫時讓出 CPU。
  * - Scheduler 尚未啟動時，不會等待 USB 空閒，以避免系統卡住。
  */
int _write(int file, char *ptr, int len) {
    // 告訴編譯器這個參數我故意不用，不要警告我。
    (void)file;

    //檢查資料是否合法
    if ((ptr == NULL) || (len <= 0)) {
        return 0;
    }

    //檢查 USB 是否已完成連線
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
        return len;
    }
    //最多重試 100 次
    for (uint32_t retry = 0U; retry < 100U; retry++) {
      //嘗試透過 USB 傳送
      if (CDC_Transmit_FS((uint8_t *)ptr, (uint16_t)len) == USBD_OK) {
        return len;
      }
      //Scheduler 是否已啟動
      if (osKernelGetState() == osKernelRunning) {
        osDelay(1U);
      } else {
        /*
          * Scheduler 尚未啟動時，不使用 HAL_Delay() 等待，
          * 避免在 RTOS 初始化期間造成阻塞，因此直接放棄此次輸出。
          */
        return len;
      }
    }
    /* 超過重試次數仍無法傳送，直接放棄此次輸出，避免 Task 被卡住。 */
    return len;
}

/**
  * @brief  初始化 PC13 LED
  */
static void TestLED_Init(void) {
  //初始化
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  //開啟 GPIOC 時鐘
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitStruct.Pin = TEST_LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;   //推挽輸出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;           //不使用上拉或下拉電阻
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  //低速率輸出
  //初始化 GPIOC Pin 13 為推挽輸出模式 PC13 以後要當 GPIO 輸出使用
  HAL_GPIO_Init(TEST_LED_PORT, &GPIO_InitStruct);
  //這腳位現輸出什麼電位 現在立刻把 PC13 輸出 High(熄滅)
  HAL_GPIO_WritePin(TEST_LED_PORT, TEST_LED_PIN, TEST_LED_OFF);
}

/**
  * @brief  在 Error_Handler() 裡，讓 LED 閃爍時產生間隔 
  */
static void BusyDelay(volatile uint32_t count) {
  while (count > 0U) {
    //不做任何事情的 CPU 指令
      __NOP();
      count--;
  }
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
  MX_I2C2_Init();
  MX_SPI1_Init();
  // Initialize USB Device
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
    /*
    * PC13 heartbeat/error LED initialization.
    * Kept in USER CODE so CubeMX regeneration will not remove it.
    */
    TestLED_Init();

    /*
    * Initialize MPU6050 before starting FreeRTOS.
    * Do not print here because USB enumeration may not be complete.
    */
    HAL_StatusTypeDef imu_init_status = MPU6050_Init(&hi2c1);
    if (imu_init_status != HAL_OK)
    {
      printf(
        "MPU6050 init failed: status=%d, i2c_error=0x%08lX\r\n",
        (int)imu_init_status,
        (unsigned long)HAL_I2C_GetError(&hi2c1)
      );

      Error_Handler();
    }
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();   //建立 RTOS
  MX_FREERTOS_Init();     //建立所有 Task

  /* Start scheduler */
  osKernelStart();        //開始執行 Task

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /*
    * 到這了代表osKernelStart()啟動是失敗
    */
    Error_Handler();
  }
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief  設定 STM32 的系統時鐘
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
//如果是 TIM5 的更新中斷，就呼叫 HAL_IncTick() 提供 HAL 的時間基準
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

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = TEST_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(TEST_LED_PORT, &GPIO_InitStruct);

    while (1) {
      //把目前 GPIO 的狀態反轉（Toggle）。
      HAL_GPIO_TogglePin(TEST_LED_PORT, TEST_LED_PIN);
      BusyDelay(1500000U);
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
    (void)file;
    (void)line;

    Error_Handler();
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
