#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEVICE          "/dev/spidev0.0"
#define SPI_MODE_SETTING    SPI_MODE_0
#define SPI_BITS_PER_WORD   8U
#define SPI_SPEED_HZ        10000U
#define SPI_RAW_DATA_SIZE   60U
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
    uint8_t tx_dummy[SPI_RAW_DATA_SIZE] = {0};

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

int main(void)
{
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

    // SPI 讀取循環，嘗試讀取三次有效數據
    uint8_t rx_buf[SPI_RAW_DATA_SIZE] = {0};
    SystemDataPacketV0 packet = {0};
    // 用於檢查序列號和時間戳的變量
    uint32_t previous_sequence = 0U;
    int has_previous_sequence = 0;
    // 用於檢查時間戳的變量
    uint32_t previous_timestamp_ms = 0U;
    int has_previous_timestamp = 0;

    for (int i = 0; i < 3; i++) {
        memset(rx_buf, 0, sizeof(rx_buf));

        if (spi_read_raw(
                fd,
                rx_buf,
                sizeof(rx_buf),
                speed_hz,
                bits_per_word
            ) < 0) {
            close(fd);
            return 1;
        }

        memcpy(&packet, rx_buf, sizeof(packet));

        if (validate_packet_header(&packet) < 0) {
            usleep(100000);
            continue;
        }

        if (validate_packet_checksum(&packet) < 0) {
            usleep(100000);
            continue;
        }

        if (has_previous_sequence != 0) {
            if (packet.sequence == previous_sequence) {
                fprintf(
                    stderr,
                    "Warning: duplicated sequence=%u\n",
                    packet.sequence
                );
            } else if (packet.sequence < previous_sequence) {
                fprintf(
                    stderr,
                    "Warning: sequence moved backward: previous=%u, current=%u\n",
                    previous_sequence,
                    packet.sequence
                );
            }
        }

        previous_sequence = packet.sequence;
        has_previous_sequence = 1;

        if (has_previous_timestamp != 0) {
            if (packet.timestamp_ms == previous_timestamp_ms) {
                fprintf(
                    stderr,
                    "Warning: duplicated timestamp=%u ms\n",
                    packet.timestamp_ms
                );
            } else if (packet.timestamp_ms < previous_timestamp_ms) {
                fprintf(
                    stderr,
                    "Warning: timestamp moved backward: previous=%u ms, current=%u ms\n",
                    previous_timestamp_ms,
                    packet.timestamp_ms
                );
            }
        }

        previous_timestamp_ms = packet.timestamp_ms;
        has_previous_timestamp = 1;

        printf("Read %d ", i + 1);
        print_bytes("RX", rx_buf, sizeof(rx_buf));
        print_packet_summary(&packet);

        usleep(100000);
    }

    close(fd);
    return 0;
}