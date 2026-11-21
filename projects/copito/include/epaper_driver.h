#ifndef EPAPER_DRIVER_H
#define EPAPER_DRIVER_H

#include <Arduino.h>
#include "config.h"
#include "faces_bitmaps.h"

class EPaperDriver {
public:
    EPaperDriver();
    ~EPaperDriver();

    bool begin();
    void powerOn();
    void powerOff();
    void clear(uint8_t color = 0xFF);
    
    // Refresh routines
    void initFull();
    void initPartial();
    void displayFull();
    void displayPartial();
    // Parcial por ventana (SSD1681 0x44/0x45): solo transfiere y excita el
    // rectángulo (x,y,w,h) en coords de framebuffer (0,0 = arriba-izq).
    // X se alinea a bytes. Ahorra SPI + corriente de panel frente al partial
    // completo (5000 B siempre). Y se mapea al orden Y-decremento del driver.
    void displayPartialWindow(int x, int y, int w, int h);
    void sleep();

    // High level face display (Asynchronous & Non-blocking via FreeRTOS on Core 0)
    void showFace(FaceState state, int frame = 0, bool forceFullRefresh = false);
    void showFaceDirect(FaceState state, int frame = 0, bool forceFullRefresh = false);
    void showFallbackText(int drawType, const char *text, bool forceFullRefresh = true);
    void renderPaginatorPage(class TextPaginator &p, const char *title, bool forceFull = false);
    void showClockFace(int hour, int min, float tempC, float humPct, int battPct, const char *dateStr, bool forceFullRefresh = false);
    // Actualización diferencial del reloj: redibuja en RAM solo los dígitos
    // que cambian entre (prevHour:prevMin) -> (hour:min) y excita por ventana
    // solo su bounding-box unido. Devuelve false si no pudo (pide full).
    // El llamador debe haber hecho el FULL inicial (changeState) y decidir el
    // FULL periódico anti-ghosting; esto cubre el caso común 1 dígito/min.
    bool showClockMinutesWindowed(int prevHour, int prevMin, int hour, int min);
    void showWeatherScreen(int hour, int min, float tempC, float humPct, int battPct, const char *conditionText, bool forceFullRefresh = false);
    void showVectorSketch(const char *commands, const char *caption = nullptr, bool forceFullRefresh = true);
    void showMusicScreen(const char *title, const char *status = "SONANDO", bool forceFullRefresh = true);
    void showMusicLogo(const char *title, const char *status, const uint8_t *logo64);
    void showSpeechProgress(int done, int total, bool forceFullRefresh = false);
    void beginStoryLoading();
    void updateStoryLoading(uint16_t step);
    void showBitmap(const uint8_t *bitmap, bool forceFullRefresh = true);
    // Foto del framebuffer actual (5000 B 1-bit) para /api/screen: emulación
    // de pantallas en la PC (agente UI). Con mutex, sin tocar el display.
    bool snapshot(uint8_t *dst);

    uint8_t* getBuffer() { return buffer; }

private:
    uint8_t *buffer;
    bool isPartialReady;
    FaceState currentFace;

    void spiWriteByte(uint8_t data);
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void sendDataBytes(const uint8_t *data, size_t len);
    void waitBusy();
    void setLut(const uint8_t *lut);
    void setWindows(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd);
    void setCursor(uint16_t x, uint16_t y);
};

extern EPaperDriver epd;

#endif // EPAPER_DRIVER_H
