/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Minimal MPU6050 FreeRTOS task
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "i2c.h"
#include "mpu6050_driver.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TEST_LED_PORT       GPIOC
#define TEST_LED_PIN        GPIO_PIN_13

#define MPU_SAMPLE_PERIOD_MS    10U
#define MPU_LOG_EVERY_SAMPLES   100U
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
osThreadId_t testTaskHandle;

const osThreadAttr_t testTask_attributes =
{
    .name = "TestTask",

    /*
     * 2048 bytes provides more headroom for USB printf() with float formats.
     * This task is dynamically allocated through heap_4.c.
     */
    .stack_size = 2048U,
    .priority = (osPriority_t)osPriorityNormal,
};
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void StartTestTask(void *argument);
/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void);

/**
  * @brief FreeRTOS initialization.
  */
void MX_FREERTOS_Init(void)
{
    /*
     * Only one task is created in this baseline.
     * No mutex, BME280 task, OLED task, timer or queue is created.
     */
    testTaskHandle =
        osThreadNew(StartTestTask, NULL, &testTask_attributes);

    if (testTaskHandle == NULL)
    {
        Error_Handler();
    }
}

/**
  * @brief  Read MPU6050 at 100 Hz and print data at 1 Hz.
  */
static void StartTestTask(void *argument)
{
    (void)argument;

    MPU6050_Data accel = {0};
    MPU6050_Data gyro = {0};

    uint32_t sampleCount = 0U;

    const TickType_t samplePeriod =
        pdMS_TO_TICKS(MPU_SAMPLE_PERIOD_MS);

    /*
     * Allow Windows time to finish USB CDC enumeration.
     * xLastWakeTime must be captured after this initial delay.
     */
    osDelay(2000U);

    printf("\r\nStartTestTask started.\r\n");
    printf("MPU6050 sampling: 100 Hz, USB log: 1 Hz\r\n");

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        /*
         * This is currently the only I2C1 user, so no mutex is needed.
         */
        MPU6050_ReadAll(&hi2c1, &accel, &gyro);

        sampleCount++;

        if (sampleCount >= MPU_LOG_EVERY_SAMPLES)
        {
            sampleCount = 0U;

            /* Task heartbeat: toggle onboard LED once per second. */
            HAL_GPIO_TogglePin(TEST_LED_PORT, TEST_LED_PIN);

            printf(
                "Acc(g): X=%.2f Y=%.2f Z=%.2f | "
                "Gyr(dps): X=%.1f Y=%.1f Z=%.1f\r\n",
                accel.x,
                accel.y,
                accel.z,
                gyro.x,
                gyro.y,
                gyro.z
            );
        }

        vTaskDelayUntil(&xLastWakeTime, samplePeriod);
    }
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */
