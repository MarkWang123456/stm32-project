#define _POSIX_C_SOURCE 200809L

#include "oled_display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OLED_I2C_DEVICE "/dev/i2c-1"
#define OLED_I2C_ADDRESS 0x3C

#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U
#define SSD1306_PAGE_COUNT (SSD1306_HEIGHT / 8U)
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_PAGE_COUNT)

#define FONT_WIDTH 5U
#define FONT_HEIGHT 7U
#define FONT_ADVANCE 6U

static int oled_fd = -1;
static uint8_t framebuffer[SSD1306_BUFFER_SIZE];

static OledVibrationEvent event_history[OLED_EVENT_HISTORY_SIZE];
static size_t event_count = 0U;

/*
 * Five-column, 5x7 glyphs.
 * Each byte represents one vertical column; bit 0 is the top pixel.
 */
static const uint8_t glyph_space[FONT_WIDTH] = {0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t glyph_dash[FONT_WIDTH]  = {0x08, 0x08, 0x08, 0x08, 0x08};
static const uint8_t glyph_dot[FONT_WIDTH]   = {0x00, 0x60, 0x60, 0x00, 0x00};
static const uint8_t glyph_colon[FONT_WIDTH] = {0x00, 0x36, 0x36, 0x00, 0x00};

static const uint8_t digit_glyphs[10][FONT_WIDTH] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}  /* 9 */
};

static const uint8_t upper_glyphs[26][FONT_WIDTH] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}  /* Z */
};

static const uint8_t *font_get_glyph(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return digit_glyphs[(size_t)(ch - '0')];
    }

    if ((ch >= 'A') && (ch <= 'Z'))
    {
        return upper_glyphs[(size_t)(ch - 'A')];
    }

    switch (ch)
    {
        case '-':
            return glyph_dash;
        case '.':
            return glyph_dot;
        case ':':
            return glyph_colon;
        case ' ':
        default:
            return glyph_space;
    }
}

static int oled_write_command(uint8_t command)
{
    const uint8_t packet[2] = {0x00U, command};

    if (oled_fd < 0)
    {
        return -1;
    }

    return (write(oled_fd, packet, sizeof(packet)) ==
            (ssize_t)sizeof(packet)) ? 0 : -1;
}

static int oled_write_data(const uint8_t *data, size_t length)
{
    uint8_t packet[17];

    if ((oled_fd < 0) || (data == NULL))
    {
        return -1;
    }

    while (length > 0U)
    {
        const size_t chunk = (length > 16U) ? 16U : length;

        packet[0] = 0x40U;
        memcpy(&packet[1], data, chunk);

        if (write(oled_fd, packet, chunk + 1U) !=
            (ssize_t)(chunk + 1U))
        {
            return -1;
        }

        data += chunk;
        length -= chunk;
    }

    return 0;
}

static int ssd1306_initialize_controller(void)
{
    static const uint8_t commands[] = {
        0xAE,       /* Display OFF */
        0x20, 0x00, /* Horizontal addressing mode */
        0xB0,
        0xC8,       /* COM scan direction remapped */
        0x00,
        0x10,
        0x40,
        0x81, 0x7F, /* Contrast */
        0xA1,       /* Segment remap */
        0xA6,       /* Normal display */
        0xA8, 0x3F, /* Multiplex ratio */
        0xA4,
        0xD3, 0x00,
        0xD5, 0x80,
        0xD9, 0xF1,
        0xDA, 0x12,
        0xDB, 0x40,
        0x8D, 0x14, /* Charge pump ON */
        0xAF        /* Display ON */
    };

    for (size_t i = 0U; i < sizeof(commands); ++i)
    {
        if (oled_write_command(commands[i]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static void framebuffer_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

static void framebuffer_draw_pixel(
    uint8_t x,
    uint8_t y,
    bool on)
{
    size_t index;
    uint8_t mask;

    if ((x >= SSD1306_WIDTH) || (y >= SSD1306_HEIGHT))
    {
        return;
    }

    index = (size_t)x +
            ((size_t)(y / 8U) * SSD1306_WIDTH);
    mask = (uint8_t)(1U << (y % 8U));

    if (on)
    {
        framebuffer[index] |= mask;
    }
    else
    {
        framebuffer[index] &= (uint8_t)~mask;
    }
}

static void framebuffer_draw_char(
    uint8_t x,
    uint8_t y,
    char ch)
{
    const uint8_t *glyph = font_get_glyph(ch);

    for (uint8_t column = 0U; column < FONT_WIDTH; ++column)
    {
        const uint8_t bits = glyph[column];

        for (uint8_t row = 0U; row < FONT_HEIGHT; ++row)
        {
            framebuffer_draw_pixel(
                (uint8_t)(x + column),
                (uint8_t)(y + row),
                ((bits >> row) & 0x01U) != 0U);
        }
    }
}

static void framebuffer_draw_text(
    uint8_t x,
    uint8_t y,
    const char *text)
{
    uint8_t cursor_x = x;

    if (text == NULL)
    {
        return;
    }

    while ((*text != '\0') &&
           ((uint16_t)cursor_x + FONT_WIDTH <= SSD1306_WIDTH))
    {
        framebuffer_draw_char(cursor_x, y, *text);
        cursor_x = (uint8_t)(cursor_x + FONT_ADVANCE);
        ++text;
    }
}

static int framebuffer_flush(void)
{
    if (oled_write_command(0x21U) != 0 ||
        oled_write_command(0x00U) != 0 ||
        oled_write_command((uint8_t)(SSD1306_WIDTH - 1U)) != 0 ||
        oled_write_command(0x22U) != 0 ||
        oled_write_command(0x00U) != 0 ||
        oled_write_command((uint8_t)(SSD1306_PAGE_COUNT - 1U)) != 0)
    {
        return -1;
    }

    return oled_write_data(framebuffer, sizeof(framebuffer));
}

static void format_event_line(
    char *buffer,
    size_t buffer_size,
    size_t index)
{
    struct tm local_time;
    char time_buffer[6];

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return;
    }

    if (index >= event_count)
    {
        (void)snprintf(
            buffer,
            buffer_size,
            "%zu --:-- --.--G",
            index + 1U);
        return;
    }

    if (localtime_r(
            &event_history[index].timestamp,
            &local_time) == NULL)
    {
        (void)snprintf(
            time_buffer,
            sizeof(time_buffer),
            "--:--");
    }
    else
    {
        (void)strftime(
            time_buffer,
            sizeof(time_buffer),
            "%H:%M",
            &local_time);
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "%zu %s %.2fG",
        index + 1U,
        time_buffer,
        event_history[index].peak_magnitude);
}

int oled_display_init(void)
{
    if (oled_fd >= 0)
    {
        return 0;
    }

    oled_fd = open(OLED_I2C_DEVICE, O_RDWR);
    if (oled_fd < 0)
    {
        fprintf(
            stderr,
            "open %s failed: %s\n",
            OLED_I2C_DEVICE,
            strerror(errno));
        return -1;
    }

    if (ioctl(oled_fd, I2C_SLAVE, OLED_I2C_ADDRESS) < 0)
    {
        fprintf(
            stderr,
            "select OLED address 0x%02X failed: %s\n",
            OLED_I2C_ADDRESS,
            strerror(errno));
        close(oled_fd);
        oled_fd = -1;
        return -1;
    }

    if (ssd1306_initialize_controller() != 0)
    {
        fprintf(
            stderr,
            "SSD1306 initialization failed: %s\n",
            strerror(errno));
        close(oled_fd);
        oled_fd = -1;
        return -1;
    }

    framebuffer_clear();

    if (framebuffer_flush() != 0)
    {
        fprintf(
            stderr,
            "SSD1306 initial clear failed: %s\n",
            strerror(errno));
        close(oled_fd);
        oled_fd = -1;
        return -1;
    }

    return 0;
}

int oled_display_update(bool vibration, float magnitude)
{
    char line[24];

    if (oled_fd < 0)
    {
        return -1;
    }

    framebuffer_clear();

    framebuffer_draw_text(
        0U,
        0U,
        vibration ? "STATUS:VIBRATION" : "STATUS:NORMAL");

    (void)snprintf(line, sizeof(line), "MAG:%.3fG", magnitude);
    framebuffer_draw_text(0U, 8U, line);

    framebuffer_draw_text(0U, 16U, "RECENT EVENTS");

    for (size_t i = 0U; i < OLED_EVENT_HISTORY_SIZE; ++i)
    {
        format_event_line(line, sizeof(line), i);
        framebuffer_draw_text(
            0U,
            (uint8_t)(24U + (i * 8U)),
            line);
    }

    if (framebuffer_flush() != 0)
    {
        fprintf(
            stderr,
            "SSD1306 update failed: %s\n",
            strerror(errno));
        return -1;
    }

    return 0;
}

void oled_display_event_start(float magnitude, time_t timestamp)
{
    const size_t move_count =
        (event_count < OLED_EVENT_HISTORY_SIZE)
            ? event_count
            : (OLED_EVENT_HISTORY_SIZE - 1U);

    if (move_count > 0U)
    {
        memmove(
            &event_history[1],
            &event_history[0],
            move_count * sizeof(event_history[0]));
    }

    event_history[0].timestamp = timestamp;
    event_history[0].peak_magnitude = magnitude;

    if (event_count < OLED_EVENT_HISTORY_SIZE)
    {
        ++event_count;
    }
}

void oled_display_event_update_peak(float magnitude)
{
    if ((event_count > 0U) &&
        (magnitude > event_history[0].peak_magnitude))
    {
        event_history[0].peak_magnitude = magnitude;
    }
}

size_t oled_display_event_count(void)
{
    return event_count;
}

void oled_display_clear_history(void)
{
    memset(event_history, 0, sizeof(event_history));
    event_count = 0U;
}

void oled_display_close(void)
{
    if (oled_fd < 0)
    {
        return;
    }

    framebuffer_clear();
    (void)framebuffer_flush();

    close(oled_fd);
    oled_fd = -1;
}