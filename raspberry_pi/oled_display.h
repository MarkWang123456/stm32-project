#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_EVENT_HISTORY_SIZE 5U

typedef struct
{
    time_t timestamp;
    float peak_magnitude;
} OledVibrationEvent;

/**
 * @brief Open /dev/i2c-1 and initialize the SSD1306 display.
 *
 * @return 0 on success, -1 on failure.
 */
int oled_display_init(void);

/**
 * @brief Update the OLED main screen.
 *
 * This function only redraws the display. It does not add a new event.
 *
 * @param vibration true when the current state is VIBRATION.
 * @param magnitude Current acceleration magnitude in g.
 *
 * @return 0 on success, -1 on I2C/display failure.
 */
int oled_display_update(bool vibration, float magnitude);

/**
 * @brief Start and record a new vibration event.
 *
 * Call this only when the state changes from NORMAL to VIBRATION.
 *
 * @param magnitude Initial magnitude in g.
 * @param timestamp Event start time. Pass time(NULL) for current system time.
 */
void oled_display_event_start(float magnitude, time_t timestamp);

/**
 * @brief Update the peak value of the current vibration event.
 *
 * Call this while the state remains VIBRATION.
 *
 * @param magnitude Current magnitude in g.
 */
void oled_display_event_update_peak(float magnitude);

/**
 * @brief Return the number of vibration events currently stored.
 *
 * @return A value from 0 to OLED_EVENT_HISTORY_SIZE.
 */
size_t oled_display_event_count(void);

/**
 * @brief Clear all stored vibration event history.
 */
void oled_display_clear_history(void);

/**
 * @brief Clear the OLED, close the I2C device, and release resources.
 */
void oled_display_close(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */