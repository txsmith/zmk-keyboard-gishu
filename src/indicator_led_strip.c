#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <math.h>
#include <stdlib.h>

#include <zephyr/drivers/led_strip.h>
#include <drivers/ext_power.h>

#include <zmk/workqueue.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>


#include "rgb.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_indicator_strip)
#error "A zmk,indicator_strip chosen node must be declared"
#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_indicator_strip)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];

static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));

static const int LED_BREATHE_DURATION_MS = 1000;
static const int LED_BREATHE_TICK_DURATION_MS = 15;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void led_power_enable() {
    if (ext_power != NULL) {
        int rc = ext_power_enable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to enable EXT_POWER: %d", rc);
        }
    }
}

static void led_power_disable() {
    if (ext_power != NULL) {
        int rc = ext_power_disable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to disable EXT_POWER: %d", rc);
        }
    }
}

static void led_breathe_once(struct led_hsb color) {
    int num_ticks = LED_BREATHE_DURATION_MS / LED_BREATHE_TICK_DURATION_MS;
    uint8_t max_brightness = color.b;

    led_power_enable();
    for (int t = 0; t < num_ticks; t++) {
        float angle = (float)t / num_ticks * M_PI;
        
        // Use sine function to create smooth brightness curve
        // sin(0) = 0, sin(π/2) = 1, sin(π) = 0
        color.b = (uint8_t)(max_brightness * sin(angle));

        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            pixels[i] = hsb_to_rgb(color);
        }
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
        k_sleep(K_MSEC(LED_BREATHE_TICK_DURATION_MS));
    }

    led_power_disable();
}

// Returns true if the state changed from disconnected to connected
static bool indicate_while_ble_unconnected() {
    uint8_t profile_index = zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_connected()) {
        LOG_INF("Profile %d %s", profile_index, "connected");
        return false;
    } else if (zmk_ble_active_profile_is_open()) {
        LOG_INF("Profile %d %s", profile_index, "open");
    } else {
        LOG_INF("Profile %d %s", profile_index, "not connected");
    }

    const int num_colors = sizeof(COLORS) / sizeof(COLORS[0]);
    int color_index = 0;
    while(zmk_ble_active_profile_is_open()) {
        led_breathe_once(COLORS[color_index]);
        color_index = (color_index + 1) % num_colors;
        k_sleep(K_MSEC(300));
    }

    while(!zmk_ble_active_profile_is_connected() && !zmk_ble_active_profile_is_open()) {
        led_breathe_once(WHITE);
        k_sleep(K_MSEC(300));
    }
    return true;
}

static uint8_t get_battery_state_of_charge() {
    uint8_t battery_level = zmk_battery_state_of_charge();
    uint8_t retry = 0;

    // Fetch the charge percetage (0-100)
    while (battery_level == 0 && retry++ < 10) {
        k_sleep(K_MSEC(100));
        battery_level = zmk_battery_state_of_charge();
    };
    return battery_level;
}

struct battery_color {
    struct led_hsb color;
    uint8_t pixels_to_light;
};

static struct battery_color battery_state_of_charge_color(uint8_t battery_level) {
    uint8_t max_brightness = CONFIG_ZMK_INDICATOR_LED_BRIGHTNESS;

    // Determine LED color (between red and green)
    struct led_hsb battery_color = { .h = (int)round((float)battery_level/100*120), .s = 100, .b = max_brightness };
    uint8_t pixels_to_light = (uint8_t)round((float)STRIP_NUM_PIXELS * ((float)battery_level / 100));

    return (struct battery_color){ .color = battery_color, .pixels_to_light = pixels_to_light };
}

static void update_strip(struct battery_color battery_color, bool last_pixel_on) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        if (i < battery_color.pixels_to_light - 1) {
            pixels[i] = hsb_to_rgb(battery_color.color);
        } else if (i == battery_color.pixels_to_light - 1 && last_pixel_on) {
            pixels[i] = hsb_to_rgb(battery_color.color);
        } else {
            pixels[i] = hsb_to_rgb(OFF);
        }
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

static int get_battery_current_ma() {
    const struct device *battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
    struct sensor_value current_val;

    if (!device_is_ready(battery)) {
        return 0;
    }

    if (sensor_sample_fetch_chan(battery, SENSOR_CHAN_CURRENT) < 0) {
        return 0;
    }

    if (sensor_channel_get(battery, SENSOR_CHAN_CURRENT, &current_val) < 0) {
        return 0;
    }

    return current_val.val1;
}

static void indicate_while_usb_connected() {

    uint8_t battery_soc = get_battery_state_of_charge();

    struct battery_color battery_color = battery_state_of_charge_color(battery_soc);

    uint8_t pixels_to_light = 0;
    struct led_hsb color = battery_color.color;

    led_power_enable();

    float pct_per_pixel = 100.0 / STRIP_NUM_PIXELS;

    // Initial animation, fade in from red to green
    for (int p = 0; p <= battery_color.pixels_to_light; p++) {
        struct battery_color current_color = battery_state_of_charge_color(round(pct_per_pixel*p));
        pixels_to_light = current_color.pixels_to_light;

        update_strip(current_color, true);
        k_sleep(K_MSEC(500/battery_color.pixels_to_light));
    }

    k_sleep(K_MSEC(500));

    // Blink while charging (USB connected and actively charging)
    // Stop when: SOC >= 99% OR current near 0 (charge complete) OR connections lost
    while (zmk_usb_is_powered() && zmk_ble_active_profile_is_connected()) {
        battery_soc = get_battery_state_of_charge();
        int current_ma = get_battery_current_ma();

        battery_color = battery_state_of_charge_color(battery_soc);
        pixels_to_light = battery_color.pixels_to_light;
        color = battery_color.color;

        update_strip(battery_color, current_ma > -10);
        k_sleep(K_MSEC(500));
        update_strip(battery_color, true);
        k_sleep(K_MSEC(500));
    }

    k_sleep(K_MSEC(500));

    // When USB or BLE disconnects
    // Fade out
    int fade_time = 250;
    int num_ticks = fade_time / LED_BREATHE_TICK_DURATION_MS;
    float brightness_step = (float)color.b / num_ticks;
    float new_brightness = color.b;
    for (int i = 0; i < num_ticks; i++) {
        new_brightness -= brightness_step;
        color.b = (uint8_t)round(new_brightness);

        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            if (i < pixels_to_light) {
                pixels[i] = hsb_to_rgb(color);
            } else {
                pixels[i] = hsb_to_rgb(OFF);
            }
        }
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
        k_sleep(K_MSEC(LED_BREATHE_TICK_DURATION_MS));
    }
    
    led_power_disable();
}

extern void indicator_thread(void *d0, void *d1, void *d2) {
    LOG_INF("Indicator thread started");
    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);

    if (!device_is_ready(ext_power)) {
        LOG_ERR("External power device \"%s\" is not ready", ext_power->name);
        return;
    }
    led_power_disable();
    indicate_while_ble_unconnected();
    indicate_while_usb_connected();
    while (true) {
        bool did_just_connect = indicate_while_ble_unconnected();
        if (did_just_connect || zmk_usb_is_powered()) {
            indicate_while_usb_connected();
        }
        k_sleep(K_MSEC(100));
    }
}

K_THREAD_DEFINE(led_init_tid, 1024, indicator_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 200);
