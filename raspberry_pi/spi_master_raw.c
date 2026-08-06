// 啟用 POSIX.1-2008 API 宣告，如 clock_gettime() 與 clock_nanosleep()
#define _POSIX_C_SOURCE 200809L

#include <errno.h>                
#include <fcntl.h>               
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include "system_packet.h"  
#include "oled_display.h"

#define SPI_DEVICE          "/dev/spidev0.0"    // SPI Bus 0，CE0 (Chip Select 0)
#define SPI_MODE_SETTING    SPI_MODE_0          // SPI Mode 0: CPOL=0, CPHA=0
#define SPI_BITS_PER_WORD   8U                  // 每個 SPI 傳輸單位的位元數
#define SPI_SPEED_HZ        10000U              // SPI 傳輸時脈，決定單次 60-byte transaction 的速度
#define SPI_READ_PERIOD_MS  100U                // 每 100 ms 啟動一次 SPI transaction，讀取頻率為 10 Hz
#define VIBRATION_THRESHOLD_G  0.10f            // 加速度總量偏離靜止狀態 1 g 超過 0.1 g，就先認定有明顯震動

#define PI_COMMAND_NONE          0x00U          // Raspberry Pi 傳給 STM32 的簡單命令。
#define PI_COMMAND_LED_PULSE     0x02U          // 0x00 代表沒有命令，0x01 代表切換 LED。

//監測封包連續性狀態
typedef struct
{
    uint32_t previous_sequence;
    uint32_t previous_timestamp_ms;
    int has_previous_packet;
} PacketContinuityState;

// 確保 SystemDataPacketV0 結構大小符合預期
_Static_assert(
    sizeof(SystemDataPacketV0) == SYSTEM_PACKET_SIZE,
    "SystemDataPacketV0 size must be 60 bytes"
);

/*
 * 等待在下一次 SPI transaction 傳給 STM32 的命令。
 * 程式目前是單執行緒，因此不需要 mutex。
 */
static uint8_t pending_pi_command = PI_COMMAND_NONE;

/**
  * @brief  把前 16 bytes 標頭印出來
  */
static void dump_bytes(
    const uint8_t *data,
    size_t length
)
{
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }

    printf("\n");
}

/**
  * @brief  印出封包數值
  */
static void print_packet_summary(const SystemDataPacketV0 *packet)
{
    float accel_x_g = packet->accel_x_raw / 16384.0f;
    float accel_y_g = packet->accel_y_raw / 16384.0f;
    float accel_z_g = packet->accel_z_raw / 16384.0f;

    float gyro_x_dps = packet->gyro_x_raw / 131.0f;
    float gyro_y_dps = packet->gyro_y_raw / 131.0f;
    float gyro_z_dps = packet->gyro_z_raw / 131.0f;

    printf(
        "Packet: seq=%u, timestamp=%u ms\n",
        packet->sequence,
        packet->timestamp_ms
    );

    printf(
        "Accel(g): X=%.2f Y=%.2f Z=%.2f | "
        "Gyro(dps): X=%.1f Y=%.1f Z=%.1f\n",
        accel_x_g,
        accel_y_g,
        accel_z_g,
        gyro_x_dps,
        gyro_y_dps,
        gyro_z_dps
    );
}

/**
  * @brief  負責偵測是否有明顯震動
  */
static int detect_vibration(
    const SystemDataPacketV0 *packet,
    float *magnitude_out,
    float *deviation_out
)
{
    if ((packet == NULL) ||
        (magnitude_out == NULL) ||
        (deviation_out == NULL)) {
        return 0;
    }

    float accel_x_g =
        packet->accel_x_raw / 16384.0f;

    float accel_y_g =
        packet->accel_y_raw / 16384.0f;

    float accel_z_g =
        packet->accel_z_raw / 16384.0f;

    //平方合開根號
    float magnitude = sqrtf(
        accel_x_g * accel_x_g +
        accel_y_g * accel_y_g +
        accel_z_g * accel_z_g
    );

    /*
     * 靜止時 magnitude 約為 1 g。
     * 計算目前數值偏離 1 g 多少。
     * 使用浮點數絕對值
     */
    float deviation = fabsf(magnitude - 1.0f);

    *magnitude_out = magnitude;
    *deviation_out = deviation;

    return deviation >= VIBRATION_THRESHOLD_G;
}

// OLED 初始化成功後設為 1；更新失敗時設回 0。
static int oled_ready = 0;

/**
  * @brief  負責偵測震動事件，並在狀態改變時顯示訊息
  */
static void process_vibration(
    const SystemDataPacketV0 *packet
)
{
    static int previous_vibration = 0;

    //加速度向量大小
    float magnitude = 0.0f;
    //偏離量
    float deviation = 0.0f;

    int vibration = detect_vibration(
        packet,
        &magnitude,
        &deviation
    );

    printf(
        "Magnitude=%.3f g | deviation=%.3f g\n",
        magnitude,
        deviation
    );

    /*
     * 只有從 NORMAL 變成 VIBRATION 時，
     * 顯示一次事件訊息。
     */
    if ((vibration != 0) &&
        (previous_vibration == 0)) {
        printf(
            "\n"
            "================================\n"
            "  VIBRATION DETECTED\n"
            "  Magnitude: %.3f g\n"
            "================================\n\n",
            magnitude
        );

        /*
        * 下一次 SPI transaction 通知 STM32：
        * 讓 LED 亮一下。
        */
        pending_pi_command = PI_COMMAND_LED_PULSE;

        if (oled_ready != 0) {
            oled_display_event_start(
                magnitude,
                time(NULL)
            );
        }
    }

    /*
     * 同一次震動持續期間，只更新這筆事件的峰值。
     */
    if ((vibration != 0) &&
        (oled_ready != 0)) {
        oled_display_event_update_peak(magnitude);
    }

    /*
     * 從震動恢復正常時也顯示一次。
     */
    if ((vibration == 0) &&
        (previous_vibration != 0)) {
        printf("[STATE] Returned to NORMAL\n");
    }

    /*
     * SPI 每 100 ms 讀一次，因此 OLED 也最多每 100 ms 更新一次。
     */
    if ((oled_ready != 0) &&
        (oled_display_update(vibration != 0, magnitude) != 0)) {
        fprintf(stderr, "OLED update disabled after I2C error\n");
        oled_display_close();
        oled_ready = 0;
    }

    previous_vibration = vibration;
}

/**
  * @brief  負責 sequence 與 timestamp 連續性檢查
  */
static void validate_packet_continuity(
    const SystemDataPacketV0 *packet,
    PacketContinuityState *state
)
{
    if (state->has_previous_packet != 0) {
        if (packet->sequence == state->previous_sequence) {
            fprintf(
                stderr,
                "Warning: duplicated sequence=%u\n",
                packet->sequence
            );
        } else if (packet->sequence < state->previous_sequence) {
            fprintf(
                stderr,
                "Warning: sequence moved backward: "
                "previous=%u, current=%u\n",
                state->previous_sequence,
                packet->sequence
            );
        }

        if (packet->timestamp_ms == state->previous_timestamp_ms) {
            fprintf(
                stderr,
                "Warning: duplicated timestamp=%u ms\n",
                packet->timestamp_ms
            );
        } else if (packet->timestamp_ms <
                   state->previous_timestamp_ms) {
            fprintf(
                stderr,
                "Warning: timestamp moved backward: "
                "previous=%u ms, current=%u ms\n",
                state->previous_timestamp_ms,
                packet->timestamp_ms
            );
        }
    }

    state->previous_sequence = packet->sequence;
    state->previous_timestamp_ms = packet->timestamp_ms;
    state->has_previous_packet = 1;
}

/**
  * @brief  開啟 SPI 裝置和設定 SPI 參數
  */
static int spi_open_and_configure(
    const char *device,
    uint8_t mode,
    uint8_t bits_per_word,
    uint32_t speed_hz
)
{
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", device, strerror(errno));
        return -1;
    }

    //以下三段用Linux SPI Driver設定 Raspberry Pi 的 SPI 參數
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) == -1) {
        perror("SPI_IOC_WR_MODE");
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) == -1) {
        perror("SPI_IOC_WR_BITS_PER_WORD");
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) == -1) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ");
        close(fd);
        return -1;
    }

    printf(
        "SPI configured: mode=%u, bits=%u, speed=%u Hz\n",
        mode,
        bits_per_word,
        speed_hz
    );

    return fd;
}

/**
  * @brief  執行一次完整的 SPI 傳輸，送出 tx_buf，同時把收到的資料放進 rx_buf。同時負責送與收
  */
static int spi_transfer(
    int fd,
    const uint8_t *tx_buf,
    uint8_t *rx_buf,
    size_t length,
    uint32_t speed_hz,
    uint8_t bits_per_word
)
{
    struct spi_ioc_transfer transfer = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = length,
        .speed_hz = speed_hz,
        .bits_per_word = bits_per_word,
        .delay_usecs = 0,
        .cs_change = 0
    };

    // SPI_IOC_MESSAGE(1) 一個包含 1 個 spi_ioc_transfer 的 SPI Message
    //回傳實際完成傳輸的 Bytes 數
    //同時送、收封包。全雙工
    int result = ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);

    if (result < 0) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }

    if ((size_t)result != length) {
        fprintf(
            stderr,
            "SPI transfer length mismatch: expected=%zu, actual=%d\n",
            length,
            result
        );
        return -1;
    }

    return 0;
}

/**
  * @brief  負責將 raw bytes 解析成 SystemDataPacketV0
  */
static int parse_system_packet_v0(
    const uint8_t *raw_buf,
    size_t raw_buf_size,
    SystemDataPacketV0 *packet
)
{
    if ((raw_buf == NULL) || (packet == NULL)) {
        fprintf(stderr, "Packet parse argument is NULL\n");
        return -1;
    }

    if (raw_buf_size < sizeof(*packet)) {
        fprintf(
            stderr,
            "Packet parse buffer too small: required=%zu, actual=%zu\n",
            sizeof(*packet),
            raw_buf_size
        );
        return -1;
    }

    memcpy(
        packet,
        raw_buf,
        sizeof(*packet)
    );

    return 0;
}

/**
  * @brief  執行一次完整的 SPI 傳輸，送出 tx_buf，同時把收到的資料放進 rx_buf。同時負責送與收
  */
static int spi_read_packet(
    int fd,
    SystemDataPacketV0 *packet,
    uint8_t *raw_buf,
    size_t raw_buf_size,
    uint32_t speed_hz,
    uint8_t bits_per_word
)
{
    if (raw_buf_size < sizeof(*packet)) {
        fprintf(
            stderr,
            "Raw buffer too small: required=%zu, actual=%zu\n",
            sizeof(*packet),
            raw_buf_size
        );
        return -1;
    }

    //清除舊資料或初始化外部傳入的 Buffer，重新填成 0
    memset(raw_buf, 0, raw_buf_size);

    uint8_t tx_buf[SYSTEM_PACKET_SIZE] = {0};

    uint8_t command_to_send = pending_pi_command;

    tx_buf[0] = command_to_send;

    /*
    * SPI 全雙工傳輸：
    * tx_buf 傳給 STM32，
    * STM32 的感測封包放進 raw_buf。
    */
    if (spi_transfer(
            fd,
            tx_buf,
            raw_buf,
            sizeof(*packet),
            speed_hz,
            bits_per_word
        ) < 0) {
        return -1;
    }

    /*
    * 只有 SPI 傳輸成功後才清除命令。
    */
    if (command_to_send != PI_COMMAND_NONE)
    {
        pending_pi_command = PI_COMMAND_NONE;

        printf(
            "Sent command to STM32: LED_PULSE (0x%02X)\n",
            command_to_send
        );
    }

    if (parse_system_packet_v0(
            raw_buf,
            raw_buf_size,
            packet
        ) < 0) {
        return -1;
    }

if (SystemPacket_Validate(packet) < 0)
{
    static uint32_t invalid_count = 0U;
    invalid_count++;

    fprintf(
        stderr,
        "Invalid packet #%u\n",
        invalid_count
    );

    fprintf(
        stderr,
        "magic=0x%08X version=%u header=%u size=%u "
        "seq=%u timestamp=%u checksum=0x%08X\n",
        packet->magic,
        packet->version,
        packet->header_size,
        packet->packet_size,
        packet->sequence,
        packet->timestamp_ms,
        packet->checksum32
    );

    printf("Raw first 16 bytes: ");
    dump_bytes(raw_buf, 16U);

    return 1;
}

    return 0;
}

/**
  * @brief  讓 Raspberry Pi 固定每 100 ms 讀一次 SPI，而不是因為 usleep() 的累積誤差讓週期越跑越偏
  */
static void timespec_add_ms(
    struct timespec *time_value,
    uint32_t milliseconds
)
{
    time_value->tv_sec += milliseconds / 1000U;

    time_value->tv_nsec +=
        (long)(milliseconds % 1000U) * 1000000L;

    if (time_value->tv_nsec >= 1000000000L) {
        time_value->tv_sec +=
            time_value->tv_nsec / 1000000000L;

        time_value->tv_nsec %=
            1000000000L;
    }
}

/**
  * @brief  比較兩個 timespec 時間，判斷誰比較早、誰比較晚
  */
static int timespec_compare(
    const struct timespec *left,
    const struct timespec *right
)
{
    if (left->tv_sec < right->tv_sec) {
        return -1;
    }

    if (left->tv_sec > right->tv_sec) {
        return 1;
    }

    if (left->tv_nsec < right->tv_nsec) {
        return -1;
    }

    if (left->tv_nsec > right->tv_nsec) {
        return 1;
    }

    return 0;
}


//一個停止旗標 ， sig_atomic_t 保證在 Signal 中可以安全存取
static volatile sig_atomic_t stop_requested = 0;

/**
  * @brief  當使用者按 Ctrl+C 時，不是立刻強制結束程式，而是通知主程式「該停止了」，由主程式完成善後工作後再正常結束。
  */
static void handle_sigint(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/**
  * @brief 關閉 SPI 與 OLED 資源
  */
static void cleanup_resources(int spi_fd)
{
    if (spi_fd >= 0) {
        close(spi_fd);
    }

    if (oled_ready != 0) {
        oled_display_close();
        oled_ready = 0;
    }
}

int main(void)
{
    //收到 SIGINT，不要用預設方式，改成先呼叫 handle_sigint()
    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    //OLED 故障時，SPI 監測仍然可以繼續執行
    if (oled_display_init() != 0) {
        fprintf(
            stderr,
            "OLED initialization failed; SPI monitoring will continue\n"
        );
    } else {
        oled_ready = 1;

        if (oled_display_update(false, 0.0f) != 0) {
            fprintf(stderr, "OLED initial screen update failed\n");
            oled_display_close();
            oled_ready = 0;
        }
    }

    const char *device = SPI_DEVICE;

    uint8_t mode = SPI_MODE_SETTING;
    uint8_t bits_per_word = SPI_BITS_PER_WORD;
    uint32_t speed_hz = SPI_SPEED_HZ;

    int fd = spi_open_and_configure(
        device,
        mode,
        bits_per_word,
        speed_hz
    );

    if (fd < 0) {
        cleanup_resources(-1);
        return 1;
    }

    uint8_t rx_buf[SYSTEM_PACKET_SIZE] = {0};
    SystemDataPacketV0 packet = {0};
    PacketContinuityState continuity_state = {0};

    struct timespec next_deadline = {0};

    //取得目前的時間，next_deadline作為第一次讀取 SPI 的基準時間
    if (clock_gettime(
            CLOCK_MONOTONIC,  //開機後經過多久
            &next_deadline
        ) != 0) {
        perror("clock_gettime");
        cleanup_resources(fd);
        return 1;
    }

    uint64_t read_count = 0U;

    while (stop_requested == 0) {
        /*
         * 每輪都從上一個目標時間增加固定週期，
         * 而不是從本輪工作完成時間重新計算。
         */
        timespec_add_ms(
            &next_deadline,
            SPI_READ_PERIOD_MS
        );

        read_count++;

        //進行SPI交易
        int read_result = spi_read_packet(
            fd,
            &packet,
            rx_buf,
            sizeof(rx_buf),
            speed_hz,
            bits_per_word
        );

        if (read_result < 0) {
            cleanup_resources(fd);
            return 1;
        }

        if (read_result == 0) {
            // 封包有效，進行連續性檢查
            validate_packet_continuity(&packet, &continuity_state);

            //printf("Read %llu ",(unsigned long long)read_count);
            //print_packet_summary(&packet);

            //震動檢查
            process_vibration(&packet);
        }

        struct timespec current_time = {0};

        //取得當下時間
        if (clock_gettime(
                CLOCK_MONOTONIC,
                &current_time
            ) != 0) {
            perror("clock_gettime");
            cleanup_resources(fd);
            return 1;
        }

        /*
        * 若本輪工作已超過 deadline，不連續追趕錯過的週期。
        * 從目前時間重新安排下一次讀取，不要一筆 SPI 傳輸剛結束，下一筆又立刻開始，中間完全沒有間隔
        */
        if (timespec_compare(
                &current_time,
                &next_deadline
            ) >= 0) {
            next_deadline = current_time;

            timespec_add_ms(
                &next_deadline,
                SPI_READ_PERIOD_MS
            );
        }

        int sleep_result;

        //讓程式穩定等到下一個 SPI 讀取時間
        //如果等待途中被一般 Signal 打斷，就繼續等
        do {
            sleep_result = clock_nanosleep(
                CLOCK_MONOTONIC,
                TIMER_ABSTIME,
                &next_deadline,
                NULL
            );
        } while (sleep_result == EINTR);

        if (sleep_result != 0) {
            fprintf(
                stderr,
                "clock_nanosleep failed: %s\n",
                strerror(sleep_result)
            );

            cleanup_resources(fd);
            return 1;
        }
    }

    printf(
        "\nStop requested. Total read attempts: %llu\n",
        (unsigned long long)read_count
    );
    
    cleanup_resources(fd);

    return 0;
}