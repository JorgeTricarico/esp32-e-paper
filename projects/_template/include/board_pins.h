#ifndef BOARD_PINS_H
#define BOARD_PINS_H

// ==============================================================================
// Hardware Pinout: Waveshare ESP32-S3-ePaper-1.54
// ==============================================================================

// Power Control Gates
#define PIN_MAIN_PWR        17      // Main peripheral power rail (Active HIGH)
#define PIN_EPD_PWR         6       // Display power gate (Active LOW)
#define PIN_AUDIO_PWR       42      // Audio circuit power gate (Active LOW)
#define PIN_AUDIO_PA        46      // NS4150B Power Amplifier Enable (Active HIGH)
#define PIN_VBAT_ADC        4       // Battery ADC sensing (ADC1_CHANNEL_3)

// E-Paper Display (SPI 200x200 1.54")
#define PIN_EPD_BUSY        8
#define PIN_EPD_RST         9
#define PIN_EPD_DC          10
#define PIN_EPD_CS          11
#define PIN_EPD_SCK         12
#define PIN_EPD_MOSI        13

// I2C Bus (ES8311 @ 0x18, PCF85063 RTC @ 0x51, SHTC3 @ 0x70)
#define PIN_I2C_SDA         47
#define PIN_I2C_SCL         48

// I2S Audio Bus (ES8311)
#define PIN_I2S_MCLK        14
#define PIN_I2S_BCLK        15
#define PIN_I2S_WS          38
#define PIN_I2S_DOUT        45
#define PIN_I2S_DIN         16

// Buttons & LED
#define PIN_BOOT_BUTTON     0       // Active LOW
#define PIN_PWR_BUTTON      18      // Active LOW
#define PIN_USER_LED        3       // Active LOW (call gpio_reset_pin first)

#endif // BOARD_PINS_H
