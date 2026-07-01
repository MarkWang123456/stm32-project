/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include "bme280_driver.h"
#include "mpu6050_driver.h" 
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h> 

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
extern I2C_HandleTypeDef hi2c1;
extern BME280_Calib_Data bme_calib; // 引用 main.c 宣告的全域 BME280 校準參數
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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for Task_IMU */
osThreadId_t Task_IMUHandle;
const osThreadAttr_t Task_IMU_attributes = {
  .name = "Task_IMU",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for Task_MotorSim */
osThreadId_t Task_MotorSimHandle;
const osThreadAttr_t Task_MotorSim_attributes = {
  .name = "Task_MotorSim",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Task_OLED */
osThreadId_t Task_OLEDHandle;
const osThreadAttr_t Task_OLED_attributes = {
  .name = "Task_OLED",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartIMUTask(void *argument);
void StartMotorSimTask(void *argument);
void StartOLEDTask(void *argument);

extern void MX_USB_DEVICE_Init(void);
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
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Task_IMU */
  // 用StartIMUTask建立新執行緒
  Task_IMUHandle = osThreadNew(StartIMUTask, NULL, &Task_IMU_attributes);

  /* creation of Task_MotorSim */
  Task_MotorSimHandle = osThreadNew(StartMotorSimTask, NULL, &Task_MotorSim_attributes);

  /* creation of Task_OLED */
  Task_OLEDHandle = osThreadNew(StartOLEDTask, NULL, &Task_OLED_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartIMUTask */
/**
  * @brief  Function implementing the Task_IMU thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartIMUTask */

// =======================================================
// 任務 1：高優先權 - IMU 數據採樣任務 (執行頻率 200Hz)
// =======================================================
void StartIMUTask(void *argument)
{
  /* USER CODE BEGIN StartIMUTask */
  printf("IMU & Weather Sensor Task Started!\r\n");
  // 準備存放數據的容器
  MPU6050_Data accel = {0};
  MPU6050_Data gyro = {0};
  BME280_Data bme_data = {0};
  uint8_t bme_valid = 0;
  uint32_t read_count = 0;

  // 取得當前的系統時脈作為基準點 記錄下任務最後一次被喚醒的時間點
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 5; // 5ms的頻率 每5ms要被喚醒一次。

  /* Infinite loop */
  for(;;)
  {
    read_count++;

    // 1. 讀取 MPU6050
    MPU6050_ReadAll(&hi2c1, &accel, &gyro);

    // 2. BME280 讀取：降頻至 2Hz (每 500ms = 100 次 loop 才執行一次)
    // 氣象數據變化緩慢，且 ADC 轉換需要時間，不可用 200Hz 狂掃
    if (read_count % 100 == 0) {
        BME280_ReadAll(&hi2c1, &bme_calib, &bme_data);
        bme_valid = 1;
    }

    // 3. 確認 I2C 總線狀態，若出錯則自動重啟
    if (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
        __HAL_I2C_DISABLE(&hi2c1);
        osDelay(1);
        __HAL_I2C_ENABLE(&hi2c1);
        continue;
    }

    // 4. 終端機印出：降頻至 10Hz (每 100ms = 20 次 loop 才執行一次)
    if (read_count % 20 == 0) {
        printf("Acc(g): X=%.2f Y=%.2f Z=%.2f | Gyr(dps): X=%.1f Y=%.1f Z=%.1f\r\n", 
               accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z);
        if (bme_valid) {
          printf("BME280: Temp=%.2f C, Press=%.2f hPa, Hum=%.2f %%\r\n", 
                bme_data.temp, bme_data.press / 100.0f, bme_data.hum);
        } else {
            printf("BME280: waiting first sample...\r\n");
    }
    }

    // 確保下一次醒來的時間點是「上一次醒來時間 + 5ms」
    // 即使執行時間有波動，系統也會自動調整睡眠長度來對齊節拍
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
  /* USER CODE END StartIMUTask */
}

/* USER CODE BEGIN Header_StartMotorSimTask */
/**
* @brief Function implementing the Task_MotorSim thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartMotorSimTask */
// =======================================================
// 任務 2：中優先權 - 馬達控制與模擬任務 (執行頻率 50Hz)
// =======================================================
void StartMotorSimTask(void *argument)
{
  /* USER CODE BEGIN StartMotorSimTask */
  printf("Motor Simulation Task Started!\r\n");
  for(;;)
  {
    // [未來工作]：執行 PID 運算等
    osDelay(20); // 50Hz
  }
  /* USER CODE END StartMotorSimTask */
}


// =======================================================
// 任務 3：低優先權 - OLED 顯示與 HMI 任務 (10Hz)
// =======================================================
/* USER CODE BEGIN Header_StartOLEDTask */
/**
* @brief Function implementing the Task_OLED thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartOLEDTask */
// =======================================================
// 任務 3：低優先權 - OLED 顯示與 HMI 任務 (執行頻率 10Hz)
// =======================================================
void StartOLEDTask(void *argument)
{
  // /* USER CODE BEGIN StartOLEDTask */
  // printf("OLED HMI Display Task Started!\r\n");
  // /* Infinite loop */
  // char display_buffer[32];
  // int counter = 0;
  for(;;)
  {
    // sprintf(display_buffer, "Hello STM32! Count: %d", counter);
    // ssd1306_Fill(Black);
    // ssd1306_SetCursor(0, 0);
    // ssd1306_WriteString(display_buffer , Font_7x10, White);
    // ssd1306_UpdateScreen(); // 將畫面同步更新至實體螢幕
    
    // counter++;
    osDelay(100); // 10Hz 更新頻率
  }
  /* USER CODE END StartOLEDTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

