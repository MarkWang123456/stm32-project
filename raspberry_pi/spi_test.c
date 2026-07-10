#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    const char *device = "/dev/spidev0.0";

    int fd = open(device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", device, strerror(errno));
        return 1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits_per_word = 8;
    uint32_t speed_hz = 10000;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) == -1) {
        perror("SPI_IOC_WR_MODE");
        close(fd);
        return 1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) == -1) {
        perror("SPI_IOC_WR_BITS_PER_WORD");
        close(fd);
        return 1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) == -1) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ");
        close(fd);
        return 1;
    }

    uint8_t tx_buf[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t rx_buf[4] = {0};

    struct spi_ioc_transfer transfer = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = (unsigned long)rx_buf,
        .len = sizeof(tx_buf),
        .speed_hz = speed_hz,
        .bits_per_word = bits_per_word,
        .delay_usecs = 0,
        .cs_change = 0
    };

    printf(
        "TX: %02X %02X %02X %02X\n",
        tx_buf[0],
        tx_buf[1],
        tx_buf[2],
        tx_buf[3]
    );

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        perror("SPI_IOC_MESSAGE");
        close(fd);
        return 1;
    }

    printf(
        "RX: %02X %02X %02X %02X\n",
        rx_buf[0],
        rx_buf[1],
        rx_buf[2],
        rx_buf[3]
    );

    close(fd);
    return 0;
}