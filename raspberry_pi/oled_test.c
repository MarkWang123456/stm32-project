#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define I2C_DEVICE "/dev/i2c-1"
#define SSD1306_ADDR 0x3C
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_PAGES (SSD1306_HEIGHT / 8)

static int write_command(int fd, uint8_t command)
{
    uint8_t packet[2] = {0x00, command};
    ssize_t written = write(fd, packet, sizeof(packet));
    return (written == (ssize_t)sizeof(packet)) ? 0 : -1;
}

static int write_data(int fd, const uint8_t *data, size_t length)
{
    uint8_t packet[17];

    while (length > 0U)
    {
        size_t chunk = (length > 16U) ? 16U : length;
        packet[0] = 0x40;
        memcpy(&packet[1], data, chunk);

        if (write(fd, packet, chunk + 1U) != (ssize_t)(chunk + 1U))
        {
            return -1;
        }

        data += chunk;
        length -= chunk;
    }

    return 0;
}

static int ssd1306_init(int fd)
{
    static const uint8_t init_commands[] = {
        0xAE,       /* Display off */
        0x20, 0x00, /* Horizontal addressing mode */
        0xB0,       /* Page start address */
        0xC8,       /* COM scan direction remapped */
        0x00,       /* Low column address */
        0x10,       /* High column address */
        0x40,       /* Display start line */
        0x81, 0x7F, /* Contrast */
        0xA1,       /* Segment remap */
        0xA6,       /* Normal display */
        0xA8, 0x3F, /* Multiplex ratio: 64 */
        0xA4,       /* Display follows RAM */
        0xD3, 0x00, /* Display offset */
        0xD5, 0x80, /* Display clock */
        0xD9, 0xF1, /* Pre-charge */
        0xDA, 0x12, /* COM pins config */
        0xDB, 0x40, /* VCOM detect */
        0x8D, 0x14, /* Charge pump on */
        0xAF        /* Display on */
    };

    for (size_t i = 0; i < sizeof(init_commands); ++i)
    {
        if (write_command(fd, init_commands[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int ssd1306_fill(int fd, uint8_t value)
{
    uint8_t row[SSD1306_WIDTH];
    memset(row, value, sizeof(row));

    for (uint8_t page = 0; page < SSD1306_PAGES; ++page)
    {
        if (write_command(fd, (uint8_t)(0xB0U + page)) != 0 ||
            write_command(fd, 0x00) != 0 ||
            write_command(fd, 0x10) != 0 ||
            write_data(fd, row, sizeof(row)) != 0)
        {
            return -1;
        }
    }

    return 0;
}

int main(void)
{
    int fd = open(I2C_DEVICE, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "open %s failed: %s\n", I2C_DEVICE, strerror(errno));
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, SSD1306_ADDR) < 0)
    {
        fprintf(stderr, "select I2C address 0x%02X failed: %s\n",
                SSD1306_ADDR, strerror(errno));
        close(fd);
        return 1;
    }

    if (ssd1306_init(fd) != 0)
    {
        fprintf(stderr, "SSD1306 initialization failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    /* Light every pixel for one second, then clear the screen. */
    if (ssd1306_fill(fd, 0xFF) != 0)
    {
        fprintf(stderr, "SSD1306 fill failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("SSD1306 test: all pixels ON for 1 second.\n");
    sleep(1);

    if (ssd1306_fill(fd, 0x00) != 0)
    {
        fprintf(stderr, "SSD1306 clear failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("SSD1306 test complete.\n");
    close(fd);
    return 0;
}