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

#define MPU_SAMPLE_PERIOD_MS    10U
#define MPU_LOG_EVERY_SAMPLES   100U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
uint8_t spi_rx_buf[SYSTEM_PACKET_SIZE] = {0};

static SystemDataPacketV0 packet_working = {0};
static SystemDataPacketV0 packet_snapshot = {0};

volatile uint8_t spi_txrx_done = 0;
volatile uint8_t spi_error_seen = 0U;
volatile uint32_t spi_error_code = HAL_SPI_ERROR_NONE;
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
static void SPI_StartTransfer(void);
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

    uint32_t sampleCount = 0U;

    const TickType_t samplePeriod =
        pdMS_TO_TICKS(MPU_SAMPLE_PERIOD_MS);

    /*
    * Allow Windows time to finish USB CDC enumeration.
    * USB has already been initialized once in main.c.
    */
    osDelay(2000U);
    
    packet_working.magic = SYSTEM_PACKET_MAGIC;
    packet_working.version = SYSTEM_PACKET_VERSION;
    packet_working.header_size = SYSTEM_PACKET_HEADER_SIZE;
    packet_working.packet_size = SYSTEM_PACKET_SIZE;

    //用SPI發送給樹梅派前 先進行一次快照 避免傳送過程中資料被更新
    SystemPacket_PublishSnapshot();
    SPI_StartTransfer();

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

        packet_working.sequence++;
        packet_working.timestamp_ms = HAL_GetTick();

        packet_working.accel_x_raw = accel.raw_x;
        packet_working.accel_y_raw = accel.raw_y;
        packet_working.accel_z_raw = accel.raw_z;

        packet_working.gyro_x_raw = gyro.raw_x;
        packet_working.gyro_y_raw = gyro.raw_y;
        packet_working.gyro_z_raw = gyro.raw_z;

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

        if (spi_txrx_done != 0U)
        {
          spi_txrx_done = 0U;

          printf(
              "SPI RX: %02X %02X %02X %02X\r\n",
              spi_rx_buf[0],
              spi_rx_buf[1],
              spi_rx_buf[2],
              spi_rx_buf[3]
          );

          SystemPacket_PublishSnapshot();
          SPI_StartTransfer();
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
        vTaskDelayUntil(&xLastWakeTime, samplePeriod);
    }
  /* USER CODE END StartTestTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static void SPI_StartTransfer(void)
{
    if (HAL_SPI_TransmitReceive_IT(
            &hspi1,
            (uint8_t *)&packet_snapshot,
            spi_rx_buf,
            SYSTEM_PACKET_SIZE) != HAL_OK)
    {
        printf("SPI TxRx start failed.\r\n");
    }
    else
    {
        printf("SPI waiting for 60-byte full-duplex transfer...\r\n");
    }
}

static void SystemPacket_PublishSnapshot(void)
{
    memcpy(
        &packet_snapshot,
        &packet_working,
        sizeof(packet_snapshot)
    );

    packet_snapshot.checksum32 = SystemPacket_CalcChecksum32(&packet_snapshot);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        spi_txrx_done = 1U;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_error_code = HAL_SPI_GetError(hspi);
        spi_error_seen = 1U;
    }
}
/* USER CODE END Application */

