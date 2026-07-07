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

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* USER CODE END Variables */
/* Definitions for testTask */
osThreadId_t testTaskHandle;
const osThreadAttr_t testTask_attributes = {
  .name = "testTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* USER CODE END FunctionPrototypes */

void StartTestTask(void *argument);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of testTask */
  testTaskHandle = osThreadNew(StartTestTask, NULL, &testTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  if (testTaskHandle == NULL) {
    Error_Handler();
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartTestTask */
/**
  * @brief  Function implementing the testTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTestTask */
void StartTestTask(void *argument) {
    /* USER CODE BEGIN StartTestTask */

    (void)argument;

    MPU6050_Data accel = {0};
    MPU6050_Data gyro = {0};

    uint32_t sampleCount = 0U;

    const TickType_t samplePeriod =
        pdMS_TO_TICKS(MPU_SAMPLE_PERIOD_MS);

    /*
    * Allow Windows time to finish USB CDC enumeration.
    * USB has already been initialized once in main.c.
    */
    osDelay(2000U);

    printf("\r\nStartTestTask started.\r\n");
    printf("MPU6050 sampling: 100 Hz, USB log: 1 Hz\r\n");

    /*
    * Capture the initial wake time after the enumeration delay,
    * otherwise vTaskDelayUntil() would try to catch up immediately.
    */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        /*
        * This is currently the only I2C1 user,
        * so no I2C mutex is required.
        */
        MPU6050_ReadAll(&hi2c1, &accel, &gyro);

        sampleCount++;

        if (sampleCount >= MPU_LOG_EVERY_SAMPLES)
        {
            sampleCount = 0U;

            /* One-second heartbeat. */
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
    /* USER CODE END StartTestTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */

