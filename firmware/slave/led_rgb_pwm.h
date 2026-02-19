/**
 * SpIOpen slave – XIAO RP2040 analog RGB LED (not Neopixel).
 * GPIO 16 = Green, 17 = Red, 25 = Blue; active-low, PWM for 0–255 intensity.
 */
#ifndef LED_RGB_PWM_H
#define LED_RGB_PWM_H

#include <stdint.h>

/** Initialize PWM on GPIO 16, 17, 25. Call once at startup. */
void led_rgb_pwm_init(void);

/** Set LED color from 0–255 per channel. Active-low: 255 = full on, 0 = off. */
void led_rgb_set(uint8_t r, uint8_t g, uint8_t b);

#endif /* LED_RGB_PWM_H */
