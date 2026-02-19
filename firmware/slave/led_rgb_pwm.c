/**
 * SpIOpen slave – XIAO RP2040 analog RGB LED (PWM).
 * Red=GPIO17, Green=GPIO16, Blue=GPIO25; active-low.
 */
#include "led_rgb_pwm.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdint.h>

#define LED_RGB_PIN_R   17u
#define LED_RGB_PIN_G   16u
#define LED_RGB_PIN_B   25u
#define LED_RGB_WRAP    255u

static uint8_t s_slice_r, s_slice_g, s_slice_b;
static uint8_t s_chan_r, s_chan_g, s_chan_b;

static void set_pwm_duty(uint8_t slice, uint8_t chan, uint8_t value)
{
    /* Active-low: 255 = LED off (duty 255), 0 = LED full on (duty 0). */
    uint16_t duty = (uint16_t)(255u - (uint16_t)value);
    pwm_set_chan_level(slice, chan, duty);
}

void led_rgb_pwm_init(void)
{
    gpio_set_function(LED_RGB_PIN_R, GPIO_FUNC_PWM);
    gpio_set_function(LED_RGB_PIN_G, GPIO_FUNC_PWM);
    gpio_set_function(LED_RGB_PIN_B, GPIO_FUNC_PWM);

    s_slice_r = pwm_gpio_to_slice_num(LED_RGB_PIN_R);
    s_slice_g = pwm_gpio_to_slice_num(LED_RGB_PIN_G);
    s_slice_b = pwm_gpio_to_slice_num(LED_RGB_PIN_B);
    s_chan_r = pwm_gpio_to_channel(LED_RGB_PIN_R);
    s_chan_g = pwm_gpio_to_channel(LED_RGB_PIN_G);
    s_chan_b = pwm_gpio_to_channel(LED_RGB_PIN_B);

    for (unsigned i = 0; i < 3u; i++) {
        uint8_t sl = (i == 0) ? s_slice_r : (i == 1) ? s_slice_g : s_slice_b;
        pwm_set_wrap(sl, LED_RGB_WRAP);
        pwm_set_enabled(sl, true);
    }

    led_rgb_set(0, 0, 0);
}

void led_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    set_pwm_duty(s_slice_r, s_chan_r, r);
    set_pwm_duty(s_slice_g, s_chan_g, g);
    set_pwm_duty(s_slice_b, s_chan_b, b);
}
