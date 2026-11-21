#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#include <Arduino.h>
#include "board_pins.h"

/**
 * Driver SPI de bajo nivel para pantalla e-Paper 1.54" 200x200 (SSD1681).
 * Soporta refresco completo (anti-ghosting) y refresco parcial por ventana
 * alineada a multiplos de 8 en X (0x44/0x45).
 */
class EpdDriver {
public:
    EpdDriver();
    ~EpdDriver();

    bool begin();
    void powerOn();
    void powerOff();

    void initFull();
    void initPartial();
    void sleep();

    void clear(uint8_t color = 0xFF);
    void displayFull();
    void displayPartial();
    void displayPartialWindow(int x, int y, int w, int h);

    // Primitivas de renderizado sobre el framebuffer de 5000 bytes (1=Blanco, 0=Negro)
    void setPixel(int x, int y, bool black);
    void drawHLine(int x0, int x1, int y, bool black = true);
    void drawCircle(int cx, int cy, int r, bool filled, bool black = true);
    void drawBitmap1Bit(int x, int y, int w, int h, const uint8_t *bitmap);

    // Composicion de pantallas del Libro de Animales
    void setBatteryLevel(uint8_t level0to3);
    void setSpellMode(bool enabled);
    void drawPageIndicator(uint8_t currentIndex, uint8_t totalCount);
    void drawBatteryIcon(uint8_t level0to3);
    void drawSpellModeIcon(bool enabled);
    void drawAnimalScreen(uint8_t animalIndex, bool frameB);
    void drawAnimalSpriteOnly(uint8_t animalIndex, bool frameB);
    void drawLetterHighlight(uint8_t animalIndex, int8_t letterPos);
    void drawSleepScreen();
    void drawCoverScreen();

    uint8_t *getBuffer() { return buffer; }

private:
    uint8_t *buffer;
    bool isPartialReady;
    uint8_t batteryLevel;
    bool spellModeEnabled;

    void spiWriteByte(uint8_t data);
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void sendDataBytes(const uint8_t *data, size_t len);
    void waitBusy();
    void setLut(const uint8_t *lut);
    void setWindows(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
    void setCursor(uint16_t x, uint16_t y);
};

extern EpdDriver epd;

#endif // EPD_DRIVER_H
