## Flashing the bootloader

Following this guide: https://github.com/joric/nrfmicro/wiki/Bootloader#flashing-the-bootloader-using-openocd

We're flashing the [pca10056 bootloader from Adafruit](https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases/download/0.2.11/pca10056_bootloader-0.2.11_s140_6.1.1.hex).

- Step 1: Install openocd on a raspberry pi
- Step 2: Configure openocd and wire the 5 SWD from the pi to the board. This should include GND, SWDCLK, SWDIO, RST and VDD. Wire VDD to a 3v3 line on the pi.
- Step 3: `sudo openocd`, then open another terminal and connect to the openocd process using `telnet localhost 4444`
- Step 4: From the new terminal, verify you can see the board with `targets`. The nRF should be 'halted', if not hit reset or power cycle.
- Step 5: `nrf5 mass_erase`
- Step 6: `flash write_image pca10056_bootloader-0.2.11_s140_6.1.1.hex`

The bootloader should now be flashed. Disconnect from the pi, connect to USB and try it!




## TODO
Change LED_LV pin
Change battery driver
Fix diode on y
