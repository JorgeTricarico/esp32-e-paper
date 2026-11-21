# Verificación de Pinout — Waveshare ESP32-S3-ePaper-1.54

Los pines principales en [`include/board_pins.h`](include/board_pins.h) fueron **verificados directamente** contra los proyectos hermanos del monorepo ([`projects/copito/include/config.h`](../copito/include/config.h) y [`projects/_template/include/board_pins.h`](../_template/include/board_pins.h)), probados en hardware real Waveshare ESP32-S3-ePaper-1.54.

## Pines Verificados (Hardware Real)
| Función | GPIO | Polaridad / Notas |
| :--- | :---: | :--- |
| `PIN_MAIN_PWR` | 17 | Active HIGH — Rail principal de periféricos / retención de energía |
| `PIN_EPD_PWR` | 6 | Active LOW — Alimentación de pantalla e-Paper |
| `PIN_AUDIO_PWR` | 42 | Active LOW — Alimentación del circuito de audio ES8311 |
| `PIN_AUDIO_PA` | 46 | Active HIGH — Habilitación de amplificador NS4150B |
| `PIN_VBAT_ADC` | 4 | ADC1_CHANNEL_3 — Lectura de batería |
| `PIN_EPD_BUSY` | 8 | Entrada estado ocupado SSD1681 |
| `PIN_EPD_RST` | 9 | Reset hardware e-Paper |
| `PIN_EPD_DC` | 10 | Data / Command e-Paper |
| `PIN_EPD_CS` | 11 | Chip Select SPI e-Paper |
| `PIN_EPD_SCK` | 12 | Clock SPI e-Paper |
| `PIN_EPD_MOSI` | 13 | MOSI SPI e-Paper |
| `PIN_I2C_SDA` | 47 | Bus I2C compartido (ES8311 @ `0x18`) |
| `PIN_I2C_SCL` | 48 | Bus I2C compartido (400 kHz) |
| `PIN_I2S_MCLK` | 14 | Master Clock ES8311 |
| `PIN_I2S_BCLK` | 15 | Bit Clock ES8311 |
| `PIN_I2S_WS` | 38 | Word Select / LRCK ES8311 |
| `PIN_I2S_DOUT` | 45 | ESP32 -> ES8311 DSDIN (Parlante) |
| `PIN_I2S_DIN` | 16 | ESP32 <- ES8311 ASDOUT (Micrófono) |
| `PIN_BTN_BOOT` | 0 | Active LOW (pull-up interno) |
| `PIN_BTN_PWR` | 18 | Active LOW (pull-up interno) |
| `PIN_USER_LED` | 3 | Active LOW (requiere `gpio_reset_pin` previo) |

## Pines Opcionales (Fallback Tarjeta TF/SD)
El proyecto utiliza **LittleFS** en Flash interna por defecto. Si se habilita `-DENABLE_SD_FALLBACK` en `platformio.ini`, los pines de tarjeta TF (`PIN_SD_CS`, `PIN_SD_MOSI`, `PIN_SD_MISO`, `PIN_SD_SCK`) están marcados con `// TODO: VERIFY WAVESHARE PIN` en `include/board_pins.h` y deben validarse según la revisión de PCB (V1/V2) antes de habilitarlos.
