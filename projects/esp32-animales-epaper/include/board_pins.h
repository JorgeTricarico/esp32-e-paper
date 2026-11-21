#ifndef BOARD_PINS_H
#define BOARD_PINS_H

// ==============================================================================
// Pinout Centralizado: Waveshare ESP32-S3-ePaper-1.54 (ESP32-S3-PICO-1-N8R8)
// Pines verificados contra hardware real en projects/copito y projects/_template
// ==============================================================================

// --- Control de Energia y Bateria ---
#define PIN_MAIN_PWR        17      // Rail principal / PWR_LATCH (Active HIGH)
#define PIN_EPD_PWR         6       // Gate de alimentacion e-Paper (Active LOW)
#define PIN_AUDIO_PWR       42      // Gate de alimentacion audio ES8311 (Active LOW)
#define PIN_AUDIO_PA        46      // Habilitacion amplificador NS4150B PA_EN (Active HIGH)
#define PIN_VBAT_ADC        4       // Lectura de voltaje bateria (ADC1_CHANNEL_3)

#define MAIN_PWR_ON         HIGH
#define MAIN_PWR_OFF        LOW
#define EPD_PWR_ON          LOW
#define EPD_PWR_OFF         HIGH
#define AUDIO_PWR_ON        LOW
#define AUDIO_PWR_OFF       HIGH
#define AUDIO_PA_ON         HIGH
#define AUDIO_PA_OFF        LOW

// --- Pantalla E-Paper 1.54" 200x200 (SPI, controlador SSD1681) ---
#define PIN_EPD_BUSY        8
#define PIN_EPD_RST         9
#define PIN_EPD_DC          10
#define PIN_EPD_CS          11
#define PIN_EPD_SCK         12
#define PIN_EPD_MOSI        13

#define EPD_WIDTH           200
#define EPD_HEIGHT          200
#define EPD_BUFFER_SIZE     (EPD_WIDTH * EPD_HEIGHT / 8) // 5000 bytes

// --- Bus I2C (Codec de audio ES8311 @ 0x18) ---
#define PIN_I2C_SDA         47
#define PIN_I2C_SCL         48
#define I2C_FREQ_HZ         400000
#define ES8311_I2C_ADDR     0x18

// --- Bus I2S (Audio ES8311) ---
#define PIN_I2S_MCLK        14
#define PIN_I2S_BCLK        15
#define PIN_I2S_WS          38      // LRCK / WS
#define PIN_I2S_DOUT        45      // ESP32 -> ES8311 DSDIN (Parlante)
#define PIN_I2S_DIN         16      // ESP32 <- ES8311 ASDOUT (Mic, no usado)

// --- Botones Fisicos y LED ---
#define PIN_BTN_BOOT        0       // Active LOW (pull-up interno)
#define PIN_BTN_PWR         18      // Active LOW (pull-up interno)
#define PIN_USER_LED        3       // Active LOW (requiere gpio_reset_pin)

// --- Fallback Opcional: Tarjeta TF / MicroSD (deshabilitado por defecto) ---
#ifdef ENABLE_SD_FALLBACK
#define PIN_SD_CS           41      // TODO: VERIFY WAVESHARE PIN (segun rev PCB)
#define PIN_SD_MOSI         40      // TODO: VERIFY WAVESHARE PIN
#define PIN_SD_MISO         39      // TODO: VERIFY WAVESHARE PIN
#define PIN_SD_SCK          37      // TODO: VERIFY WAVESHARE PIN
#endif

#endif // BOARD_PINS_H
