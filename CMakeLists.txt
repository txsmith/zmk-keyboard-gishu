target_sources_ifdef(CONFIG_ZMK_INDICATOR_LED_STRIP app PRIVATE src/indicator_led_strip.c)
target_sources_ifdef(CONFIG_ZMK_INDICATOR_LED_STRIP app PRIVATE src/rgb.c)

zephyr_include_directories(include)

add_subdirectory(drivers)
