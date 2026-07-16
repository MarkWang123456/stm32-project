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

#define SPI_DEVICE          "/dev/spidev0.0"
#define SPI_MODE_SETTING    SPI_MODE_0
#define SPI_BITS_PER_WORD   8U
#define SPI_SPEED_HZ        10000U  // SPI 傳輸時脈，決定單次 60-byte transaction 的速度
#define SPI_READ_PERIOD_MS  100U    // 每 100 ms 啟動一次 SPI transaction，讀取頻率為 10 Hz
#define SYSTEM_PACKET_VERSION      0U
#define SYSTEM_PACKET_HEADER_SIZE  16U
#define SYSTEM_PACKET_SIZE         60U
#define SYSTEM_PACKET_MAGIC        0x30504453U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  header_size;
    uint16_t packet_size;

    uint32_t sequence;
    uint32_t timestamp_ms;

    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;

    int32_t  temperature_c_x100;
    uint32_t pressure_pa;
    uint32_t humidity_percent_x100;

    uint16_t sensor_status;
    uint16_t system_flags;

    uint32_t imu_late_count;
    uint32_t i2c_error_count;
    uint32_t i2c_recovery_count;

    uint32_t checksum32;
} SystemDataPacketV0;

typedef struct
{
    uint32_t previous_sequence;
    uint32_t previous_timestamp_ms;
    int has_previous_packet;
} PacketContinuityState;

_Static_assert(
    sizeof(SystemDataPacketV0) == SYSTEM_PACKET_SIZE,
    "SystemDataPacketV0 size must be 60 bytes"
);

static uint32_t calculate_checksum32(const SystemDataPacketV0 *packet)
{
    const uint8_t *bytes = (const uint8_t *)packet;
    uint32_t sum = 0U;

    for (size_t i = 0; i < offsetof(SystemDataPacketV0, checksum32); i++) {
        sum += bytes[i];
    }

    return sum;
}

static int validate_packet_header(const SystemDataPacketV0 *packet)
{
    if (packet->magic != SYSTEM_PACKET_MAGIC) {
        fprintf(
            stderr,
            "Invalid packet magic: expected=0x%08X, actual=0x%08X\n",
            SYSTEM_PACKET_MAGIC,
            packet->magic
        );
        return -1;
    }

    if (packet->version != SYSTEM_PACKET_VERSION) {
        fprintf(
            stderr,
            "Invalid packet version: expected=%u, actual=%u\n",
            SYSTEM_PACKET_VERSION,
            packet->version
        );
        return -1;
    }

    if (packet->header_size != SYSTEM_PACKET_HEADER_SIZE) {
        fprintf(
            stderr,
            "Invalid header size: expected=%u, actual=%u\n",
            SYSTEM_PACKET_HEADER_SIZE,
            packet->header_size
        );
        return -1;
    }

    if (packet->packet_size != SYSTEM_PACKET_SIZE) {
        fprintf(
            stderr,
            "Invalid packet size: expected=%u, actual=%u\n",
            SYSTEM_PACKET_SIZE,
            packet->packet_size
        );
        return -1;
    }

    return 0;
}

static int validate_packet_checksum(const SystemDataPacketV0 *packet)
{
    uint32_t calculated = calculate_checksum32(packet);

    if (calculated != packet->checksum32) {
        fprintf(
            stderr,
            "Invalid checksum: expected=0x%08X, actual=0x%08X\n",
            calculated,
            packet->checksum32
        );
        return -1;
    }

    return 0;
}

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

static void print_bytes(
    const char *label,
    const uint8_t *buffer,
    size_t length
)
{
    printf("%s:", label);

    for (size_t i = 0; i < length; i++) {
        printf(" %02X", buffer[i]);
    }

    printf("\n");
}

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

static int spi_read_raw(
    int fd,
    uint8_t *rx_buf,
    size_t length,
    uint32_t speed_hz,
    uint8_t bits_per_word
)
{
    uint8_t tx_dummy[SYSTEM_PACKET_SIZE] = {0};

    if (length > sizeof(tx_dummy)) {
        fprintf(
            stderr,
            "SPI read length too large: requested=%zu, maximum=%zu\n",
            length,
            sizeof(tx_dummy)
        );
        return -1;
    }

    return spi_transfer(
        fd,
        tx_dummy,
        rx_buf,
        length,
        speed_hz,
        bits_per_word
    );
}

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

    memset(raw_buf, 0, raw_buf_size);

    if (spi_read_raw(
            fd,
            raw_buf,
            sizeof(*packet),
            speed_hz,
            bits_per_word
        ) < 0) {
        return -1;
    }

    memcpy(packet, raw_buf, sizeof(*packet));

    if (validate_packet_header(packet) < 0) {
        return 1;
    }

    if (validate_packet_checksum(packet) < 0) {
        return 1;
    }

    return 0;
}

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

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

int main(void)
{

    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("signal");
        return 1;
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
        return 1;
    }

    uint8_t rx_buf[SYSTEM_PACKET_SIZE] = {0};
    SystemDataPacketV0 packet = {0};
    PacketContinuityState continuity_state = {0};

    struct timespec next_deadline = {0};

    if (clock_gettime(
            CLOCK_MONOTONIC,
            &next_deadline
        ) != 0) {
        perror("clock_gettime");
        close(fd);
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

        int read_result = spi_read_packet(
            fd,
            &packet,
            rx_buf,
            sizeof(rx_buf),
            speed_hz,
            bits_per_word
        );

        if (read_result < 0) {
            close(fd);
            return 1;
        }

        if (read_result == 0) {
            validate_packet_continuity(
                &packet,
                &continuity_state
            );

            printf("Read %llu ",(unsigned long long)read_count);
            print_bytes(
                "RX",
                rx_buf,
                sizeof(rx_buf)
            );
            print_packet_summary(&packet);
        }

        struct timespec current_time = {0};

        if (clock_gettime(
                CLOCK_MONOTONIC,
                &current_time
            ) != 0) {
            perror("clock_gettime");
            close(fd);
            return 1;
        }

        /*
        * 若本輪工作已超過 deadline，不連續追趕錯過的週期。
        * 從目前時間重新安排下一次讀取，避免背靠背 SPI transaction。
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

            close(fd);
            return 1;
        }
    }

    printf(
        "\nStop requested. Total read attempts: %llu\n",
        (unsigned long long)read_count
    );
    close(fd);
    return 0;
}