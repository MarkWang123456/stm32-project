#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEVICE          "/dev/spidev0.0"
#define SPI_MODE_SETTING    SPI_MODE_0
#define SPI_BITS_PER_WORD   8U
#define SPI_SPEED_HZ        10000U
#define SPI_RAW_DATA_SIZE   4U

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

    uint8_t rx_buf[SPI_RAW_DATA_SIZE] = {0};

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

        printf("Read %d ", i + 1);
        print_bytes("RX", rx_buf, sizeof(rx_buf));

        usleep(100000);
    }

    close(fd);
    return 0;
}