#pragma once

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

struct led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

static const struct led_hsb COLORS[] = {
    {.h = 0,   .s = 90, .b = 20},  // Red
    {.h = 120, .s = 90, .b = 20},  // Green
    {.h = 240, .s = 90, .b = 20},  // Blue
    {.h = 60,  .s = 90, .b = 15},  // Yellow
    {.h = 300, .s = 90, .b = 15}   // Purple
};
static const struct led_hsb WHITE = {.h = 0, .s = 0, .b = 10};
static const struct led_hsb OFF = {.h = 0, .s = 0, .b = 0};

struct led_rgb hsb_to_rgb(struct led_hsb hsb);
