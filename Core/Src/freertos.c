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
#include <string.h>
#include "i2c.h"
#include "spi.h"
#include "mpu6050_driver.h"
#include "system_packet.h"  
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TEST_LED_PORT       GPIOC
#define TEST_LED_PIN        GPIO_PIN_13

#define MPU_SAMPLE_PERIOD_MS    10U   // 每 10 ms(100 Hz) 讀一次 MPU6050
#define MPU_LOG_EVERY_SAMPLES   100U  // 每讀 100 筆資料(1 Hz) 才印一次 Log
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint8_t spi_rx_buf[SYSTEM_PACKET_SIZE] = {0};           //SPI 接收資料的緩衝區

static SystemDataPacketV0 packet_working = {0};         //SPI 接收資料的工作區，會被更新成最新的資料
static SystemDataPacketV0 packet_snapshot = {0};        //SPI 接收資料的快照區，會被鎖定成一個穩定的資料集，供 SPI 傳輸使用

volatile uint8_t spi_txrx_done = 0;                     //SPI 傳輸完成旗標，ISR 會設為 1，Task 會在下一個週期處理
volatile uint8_t spi_error_seen = 0U;                   //SPI 傳輸錯誤旗標，ISR 會設為 1，Task 會在下一個週期處理
volatile uint32_t spi_error_code = HAL_SPI_ERROR_NONE;  //SPI 傳輸錯誤碼，ISR 會設為錯誤碼，Task 會在下一個週期處理
volatile uint32_t spi_complete_count = 0U;              //SPI 傳輸完成次數，ISR 會每次完成時加 1，Task 會在下一個週期處理
volatile uint8_t spi_recovery_needed = 0U;              //SPI 傳輸錯誤需要復原旗標，Task 會在下一個週期處理
/* USER CODE END Variables */
/* Definitions for testTask */
osThreadId_t testTaskHandle;
const osThreadAttr_t testTask_attributes = {
  .name = "testTask",
  .stack_size = 512 * 4,  //配置2KB的Stack 給這個 Task
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static HAL_StatusTypeDef SPI_StartTransfer(void);
static void SystemPacket_PublishSnapshot(void);
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
void StartTestTask(void *argument)
{
  /* USER CODE BEGIN StartTestTask */

    (void)argument;

    MPU6050_Data accel = {0};
    MPU6050_Data gyro = {0};

    //採樣次數計數器
    uint32_t sampleCount = 0U;

    //把 MPU6050 的 10 ms 採樣週期換算成 FreeRTOS 的 Tick
    const TickType_t samplePeriod =
        pdMS_TO_TICKS(MPU_SAMPLE_PERIOD_MS);

    // 2 秒的延遲，讓 Windows 有時間辨識 STM32 的 USB 虛擬序列埠。
    osDelay(2000U);
    
    packet_working.magic = SYSTEM_PACKET_MAGIC;
    packet_working.version = SYSTEM_PACKET_VERSION;
    packet_working.header_size = SYSTEM_PACKET_HEADER_SIZE;
    packet_working.packet_size = SYSTEM_PACKET_SIZE;

    //用SPI發送給樹梅派前 先進行一次快照 避免傳送過程中資料被更新
    SystemPacket_PublishSnapshot();
    // 若 SPI 啟動失敗，不會觸發完成 Callback。
    // 因此手動設回完成旗標，讓 Task 下一輪重新嘗試啟動 SPI，避免系統誤以為 SPI 一直忙碌而無法再次啟動。
    if (SPI_StartTransfer() != HAL_OK)
    {
        spi_txrx_done = 1U;
    }

    printf("\r\nStartTestTask started.\r\n");
    printf("MPU6050 sampling: 100 Hz, USB log: 1 Hz\r\n");

    //再把「現在」設成週期排程起點
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
      //SPI 發生錯誤時，先將 SPI 狀態復原

      if (spi_recovery_needed != 0U)
      {
        //中止 SPI1 目前未完成或卡住的傳輸，並讓 HAL 的 SPI 狀態恢復
        //成功 Abort 後，交給正常重新掛載流程處理。
        if (HAL_SPI_Abort(&hspi1) == HAL_OK)
        {
            spi_recovery_needed = 0U;
            //要求下方正常流程重新建立 snapshot 並重新掛載 SPI。
            spi_txrx_done = 1U;
        }
      }

      // 優先處理已完成的 SPI 傳輸，盡快準備下一次 Raspberry Pi 讀取。
      //一般完成或錯誤恢復成功後，重新掛載 SPI。
      if ((spi_recovery_needed == 0U) && (spi_txrx_done != 0U))
      {
        // 先消耗目前這次完成事件 如果啟動失敗，再把重試旗標設回去。
        spi_txrx_done = 0U;

        SystemPacket_PublishSnapshot();

        // SPI 掛載失敗時，設回旗標並於下一週期重試
        if (SPI_StartTransfer() != HAL_OK)
        {
            spi_txrx_done = 1U;
        }
      }
      //透過 I2C1 讀取 MPU6050
      HAL_StatusTypeDef imu_status =MPU6050_ReadAll(&hi2c1, &accel, &gyro);

      packet_working.sequence++;
      packet_working.timestamp_ms = HAL_GetTick();

      if (imu_status == HAL_OK) {
          packet_working.accel_x_raw = accel.raw_x;
          packet_working.accel_y_raw = accel.raw_y;
          packet_working.accel_z_raw = accel.raw_z;

          packet_working.gyro_x_raw = gyro.raw_x;
          packet_working.gyro_y_raw = gyro.raw_y;
          packet_working.gyro_z_raw = gyro.raw_z;

          packet_working.sensor_status |= SENSOR_STATUS_IMU_VALID;
      }else {
          /*
          * 不更新 accel/gyro，所以封包保留上一筆有效資料。
          */
          packet_working.sensor_status &= ~SENSOR_STATUS_IMU_VALID;
          packet_working.system_flags |= SYSTEM_FLAG_I2C_ERROR;
          packet_working.i2c_error_count++;
      }

      sampleCount++;
      // 每讀 100 筆資料(1 Hz) 才印一次 Log
      if (sampleCount >= MPU_LOG_EVERY_SAMPLES)
      {
        sampleCount = 0U;

        /* One-second heartbeat. */
        HAL_GPIO_TogglePin(TEST_LED_PORT, TEST_LED_PIN);

        // printf(
        //     "Acc(g): X=%.2f Y=%.2f Z=%.2f | "
        //     "Gyr(dps): X=%.1f Y=%.1f Z=%.1f\r\n",
        //     accel.x,
        //     accel.y,
        //     accel.z,
        //     gyro.x,
        //     gyro.y,
        //     gyro.z
        // );

        printf(
            "SPI complete=%lu | state=%d | error=0x%08lX\r\n",
            (unsigned long)spi_complete_count,
            (int)HAL_SPI_GetState(&hspi1),
            (unsigned long)HAL_SPI_GetError(&hspi1)
        );
      }


      if (spi_error_seen != 0U)
      {
        uint32_t error = spi_error_code;

        spi_error_seen = 0U;

        printf(
            "SPI ERROR: 0x%08lX\r\n",
            (unsigned long)error
        );
      }
      // xLastWakeTime 上一次喚醒的基準時間
      // samplePeriod每次間隔多少 Tick，讓 StartTestTask 按照 固定節奏執行
      vTaskDelayUntil(&xLastWakeTime, samplePeriod);
    }
  /* USER CODE END StartTestTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  開始一次 SPI 傳輸
  */
static HAL_StatusTypeDef SPI_StartTransfer(void)
{
    HAL_StatusTypeDef status;
    //非阻塞 啟動一次 SPI 全雙工傳輸
    status = HAL_SPI_TransmitReceive_IT(
        &hspi1,
        (uint8_t *)&packet_snapshot,
        spi_rx_buf,
        SYSTEM_PACKET_SIZE
    );

    if (status != HAL_OK)
    {
        printf(
            "SPI TxRx start failed: status=%d state=%d error=0x%08lX\r\n",
            (int)status,
            (int)HAL_SPI_GetState(&hspi1),
            (unsigned long)HAL_SPI_GetError(&hspi1)
        );
    }

    return status;
}

/**
  * @brief  用來更新目前的 System Packet 資料(snapshot)
  */
static void SystemPacket_PublishSnapshot(void)
{
    memcpy(
        &packet_snapshot,
        &packet_working,
        sizeof(packet_snapshot)
    );

    packet_snapshot.checksum32 = SystemPacket_CalcChecksum32(&packet_snapshot);
}

/**
  * @brief  SPI 全雙工收發完成時，由 HAL 自動呼叫。
  */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        spi_complete_count++;
        spi_txrx_done = 1U;
    }
}

/**
  * @brief  SPI 傳輸期間發生錯誤時，由 HAL 自動呼叫。
  */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_error_code = HAL_SPI_GetError(hspi);
        spi_error_seen = 1U;
        spi_recovery_needed = 1U;
    }
}
/* USER CODE END Application */

