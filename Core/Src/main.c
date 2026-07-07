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
#include "cmsis_os.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "usbd_core.h"
#include "usbd_cdc_if.h"
#include "mpu6050_driver.h"
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TEST_LED_PORT       GPIOC
#define TEST_LED_PIN        GPIO_PIN_13
#define TEST_LED_ON         GPIO_PIN_RESET
#define TEST_LED_OFF        GPIO_PIN_SET
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern USBD_HandleTypeDef hUsbDeviceFS;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);

/* USER CODE BEGIN PFP */
static void TestLED_Init(void);
static void BusyDelay(volatile uint32_t count);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Redirect printf() to USB CDC.
  *
  * Important:
  * - No printf() is used before the scheduler starts in this baseline.
  * - When USB is not configured, data is dropped instead of blocking.
  * - When the scheduler is running, BUSY is retried with osDelay().
  * - Before the scheduler is running, BUSY is not waited on.
  */
int _write(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len <= 0))
    {
        return 0;
    }

    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
    {
        return len;
    }

    for (uint32_t retry = 0U; retry < 100U; retry++)
    {
        if (CDC_Transmit_FS((uint8_t *)ptr, (uint16_t)len) == USBD_OK)
        {
            return len;
        }

        if (osKernelGetState() == osKernelRunning)
        {
            osDelay(1U);
        }
        else
        {
            /*
             * Avoid HAL_Delay() here.  A pre-scheduler printf() can occur while
             * interrupts are temporarily masked by the RTOS port.
             */
            return len;
        }
    }

    /* Timeout: drop this message rather than deadlocking the task. */
    return len;
}

/**
  * @brief  Initialize the Blackpill onboard LED on PC13.
  */
static void TestLED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = TEST_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(TEST_LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(TEST_LED_PORT, TEST_LED_PIN, TEST_LED_OFF);
}

static void BusyDelay(volatile uint32_t count)
{
    while (count > 0U)
    {
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
    /* Reset peripherals, initialize Flash and HAL time base. */
    HAL_Init();

    /* Configure the 60 MHz system clock and 48 MHz USB clock. */
    SystemClock_Config();

    /* Initialize only the peripherals needed by this baseline. */
    MX_GPIO_Init();
    TestLED_Init();
    MX_I2C1_Init();
    MX_USB_DEVICE_Init();

    /*
     * Initialize MPU6050 before starting FreeRTOS.
     * Do not print here; USB enumeration may not be complete yet.
     */
    if (!MPU6050_Init(&hi2c1))
    {
        Error_Handler();
    }

    /*
     * Correct CMSIS-OS2 startup order:
     * 1. Initialize kernel exactly once.
     * 2. Create RTOS objects in MX_FREERTOS_Init().
     * 3. Start scheduler.
     */
    if (osKernelInitialize() != osOK)
    {
        Error_Handler();
    }

    MX_FREERTOS_Init();

    if (osKernelStart() != osOK)
    {
        Error_Handler();
    }

    /* osKernelStart() must not return when successful. */
    Error_Handler();

    while (1)
    {
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

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

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(
            &RCC_ClkInitStruct,
            FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief  HAL time-base callback.
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM5)
    {
        HAL_IncTick();
    }
}

/**
  * @brief  Error handler: fast LED blinking.
  */
void Error_Handler(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = TEST_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(TEST_LED_PORT, &GPIO_InitStruct);

    while (1)
    {
        HAL_GPIO_TogglePin(TEST_LED_PORT, TEST_LED_PIN);
        BusyDelay(1500000U);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;

    Error_Handler();
}
#endif /* USE_FULL_ASSERT */
