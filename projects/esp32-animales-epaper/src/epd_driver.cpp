#include "epd_driver.h"
#include "assets/animals_gfx.h"
#include "assets/cover_gfx.h"
#include <SPI.h>

// ==============================================================================
// LUTs OFICIALES WAVESHARE 1.54" (SSD1681)
// ==============================================================================
static const uint8_t WF_Full_1IN54[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x00, 0x08, 0x01, 0x00, 0x02,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x20
};

static const uint8_t WF_PARTIAL_1IN54_0[159] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28
};

EpdDriver epd;

EpdDriver::EpdDriver() : buffer(nullptr), isPartialReady(false), batteryLevel(3), spellModeEnabled(true) {}

EpdDriver::~EpdDriver() {
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }
}

bool EpdDriver::begin() {
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }

    if (psramFound()) {
        buffer = (uint8_t *)ps_malloc(EPD_BUFFER_SIZE);
    }
    if (!buffer) {
        buffer = (uint8_t *)malloc(EPD_BUFFER_SIZE);
    }
    if (!buffer) {
        log_e("No se pudo reservar el framebuffer EPD de %d bytes", EPD_BUFFER_SIZE);
        return false;
    }
    memset(buffer, 0xFF, EPD_BUFFER_SIZE);

    pinMode(PIN_EPD_PWR, OUTPUT);
    pinMode(PIN_EPD_CS, OUTPUT);
    pinMode(PIN_EPD_DC, OUTPUT);
    pinMode(PIN_EPD_RST, OUTPUT);
    pinMode(PIN_EPD_BUSY, INPUT);

    digitalWrite(PIN_EPD_CS, HIGH);
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_RST, HIGH);

    powerOn();
    delay(50);

    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);

    initFull();
    return true;
}

void EpdDriver::powerOn() {
    digitalWrite(PIN_EPD_PWR, EPD_PWR_ON);
    delay(20);
}

void EpdDriver::powerOff() {
    digitalWrite(PIN_EPD_PWR, EPD_PWR_OFF);
}

void EpdDriver::waitBusy() {
    unsigned long start = millis();
    while (digitalRead(PIN_EPD_BUSY) == HIGH) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (millis() - start > 6000) {
            log_w("EPD Busy timeout!");
            break;
        }
    }
}

void EpdDriver::spiWriteByte(uint8_t data) {
    SPI.transfer(data);
}

void EpdDriver::sendCommand(uint8_t cmd) {
    SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, LOW);
    digitalWrite(PIN_EPD_CS, LOW);
    spiWriteByte(cmd);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void EpdDriver::sendData(uint8_t data) {
    SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    spiWriteByte(data);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void EpdDriver::sendDataBytes(const uint8_t *data, size_t len) {
    SPI.beginTransaction(SPISettings(16000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.writeBytes(data, len);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.endTransaction();
}

void EpdDriver::setWindows(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
    sendCommand(0x44);
    sendData((xStart >> 3) & 0xFF);
    sendData((xEnd >> 3) & 0xFF);

    sendCommand(0x45);
    sendData(yStart & 0xFF);
    sendData((yStart >> 8) & 0xFF);
    sendData(yEnd & 0xFF);
    sendData((yEnd >> 8) & 0xFF);
}

void EpdDriver::setCursor(uint16_t x, uint16_t y) {
    sendCommand(0x4E);
    sendData((x >> 3) & 0xFF);

    sendCommand(0x4F);
    sendData(y & 0xFF);
    sendData((y >> 8) & 0xFF);
}

void EpdDriver::setLut(const uint8_t *lut) {
    sendCommand(0x32);
    sendDataBytes(lut, 153);
    waitBusy();

    sendCommand(0x3F);
    sendData(lut[153]);

    sendCommand(0x03);
    sendData(lut[154]);

    sendCommand(0x04);
    sendData(lut[155]);
    sendData(lut[156]);
    sendData(lut[157]);

    sendCommand(0x2C);
    sendData(lut[158]);
}

void EpdDriver::initFull() {
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(50);
    digitalWrite(PIN_EPD_RST, LOW);
    delay(20);
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(50);
    waitBusy();

    sendCommand(0x12); // Software Reset
    waitBusy();

    sendCommand(0x01); // Driver output control
    sendData(0xC7);
    sendData(0x00);
    sendData(0x01);

    sendCommand(0x11); // Data entry mode (Y decrement, X increment)
    sendData(0x01);

    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);

    sendCommand(0x3C); // Border Waveform
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);

    sendCommand(0x22);
    sendData(0xB1);
    sendCommand(0x20);

    setCursor(0, EPD_HEIGHT - 1);
    waitBusy();

    setLut(WF_Full_1IN54);
    isPartialReady = false;
}

void EpdDriver::initPartial() {
    setLut(WF_PARTIAL_1IN54_0);

    sendCommand(0x37);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x40);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);

    sendCommand(0x3C);
    sendData(0x80);

    // No disparar 0x22/0xC0/0x20 aqui: 0xCF en displayPartial() ya habilita
    // el driver y evita el doble barrido / parpadeo en la primera animacion.
    isPartialReady = true;
}

void EpdDriver::clear(uint8_t color) {
    if (buffer) {
        memset(buffer, color, EPD_BUFFER_SIZE);
    }
}

void EpdDriver::displayFull() {
    if (!buffer) return;
    if (isPartialReady) {
        initFull();
    }

    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x24);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);

    // Sincronizar RAM previa (0x26) para evitar ghosting en futuros parciales
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x26);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);

    sendCommand(0x22);
    sendData(0xC7);
    sendCommand(0x20);
    waitBusy();
}

void EpdDriver::displayPartial() {
    if (!buffer) return;
    if (!isPartialReady) {
        initPartial();
    }
    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x24);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);

    sendCommand(0x22);
    sendData(0xCF);
    sendCommand(0x20);
    waitBusy();

    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x26);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);
}

void EpdDriver::displayPartialWindow(int x, int y, int w, int h) {
    // Refresca usando el framebuffer completo en modo parcial:
    // Como Zona 1 y Zona 3 son identicas entre 0x24 y 0x26, el SSD1681
    // solo excita los pixeles de Zona 2 que cambiaron, evitando cualquier
    // desincronizacion de ventana 0x44/0x45 en 0x26.
    (void)x; (void)y; (void)w; (void)h;
    displayPartial();
}

void EpdDriver::sleep() {
    sendCommand(0x10);
    sendData(0x01);
    delay(100);
}

void EpdDriver::setPixel(int x, int y, bool black) {
    if (!buffer || x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    int byteIdx = y * (EPD_WIDTH / 8) + (x >> 3);
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        buffer[byteIdx] &= (uint8_t)(~mask);
    } else {
        buffer[byteIdx] |= mask;
    }
}

void EpdDriver::drawHLine(int x0, int x1, int y, bool black) {
    for (int x = x0; x <= x1; x++) {
        setPixel(x, y, black);
    }
}

void EpdDriver::drawCircle(int cx, int cy, int r, bool filled, bool black) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (filled) {
                if (d2 <= r * r) setPixel(cx + dx, cy + dy, black);
            } else {
                if (d2 <= r * r && d2 >= (r - 1) * (r - 1)) {
                    setPixel(cx + dx, cy + dy, black);
                }
            }
        }
    }
}

void EpdDriver::drawBitmap1Bit(int x, int y, int w, int h, const uint8_t *bitmap) {
    if (!buffer || !bitmap) return;
    int srcStride = (w + 7) / 8;
    int dstStride = EPD_WIDTH / 8;

    // Copia rapida por bytes cuando X y W estan alineados a 8
    if ((x & 7) == 0 && (w & 7) == 0 && x >= 0 && x + w <= EPD_WIDTH) {
        int xByte = x >> 3;
        for (int row = 0; row < h; row++) {
            int dy = y + row;
            if (dy < 0 || dy >= EPD_HEIGHT) continue;
            memcpy_P(buffer + dy * dstStride + xByte, bitmap + row * srcStride, srcStride);
        }
        return;
    }

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint8_t b = pgm_read_byte(bitmap + row * srcStride + (col >> 3));
            bool isWhite = (b & (0x80 >> (col & 7))) != 0;
            setPixel(x + col, y + row, !isWhite);
        }
    }
}

void EpdDriver::setBatteryLevel(uint8_t level0to3) {
    batteryLevel = (level0to3 > 3) ? 3 : level0to3;
}

void EpdDriver::setSpellMode(bool enabled) {
    spellModeEnabled = enabled;
}

void EpdDriver::drawSpellModeIcon(bool enabled) {
    // Icono minimalista "A" (7x7 px) arriba a la izquierda (x=4..10, y=7..13)
    const int x0 = 4;
    const int y0 = 7;
    // Barra superior de la A
    drawHLine(x0 + 1, x0 + 5, y0, true);
    // Patas verticales
    for (int y = y0 + 1; y <= y0 + 6; y++) {
        setPixel(x0, y, true);
        setPixel(x0 + 6, y, true);
    }
    // Travesano medio
    drawHLine(x0 + 1, x0 + 5, y0 + 3, true);

    // Si esta desactivado, tachar con una linea diagonal
    if (!enabled) {
        for (int i = 0; i <= 8; i++) {
            int px = x0 - 1 + i;
            int py = y0 + 7 - i;
            setPixel(px, py, true);
            setPixel(px, py - 1, true);
        }
    }
}

void EpdDriver::drawBatteryIcon(uint8_t level0to3) {
    // Pilita minimalista 14x7 px arriba a la derecha (x=183..197, y=7..13)
    const int x0 = 183;
    const int y0 = 7;
    const int w = 12;
    const int h = 7;

    // Contorno exterior
    drawHLine(x0, x0 + w - 1, y0, true);
    drawHLine(x0, x0 + w - 1, y0 + h - 1, true);
    for (int y = y0; y < y0 + h; y++) {
        setPixel(x0, y, true);
        setPixel(x0 + w - 1, y, true);
    }
    // Borne positivo derecho
    for (int y = y0 + 2; y <= y0 + 4; y++) {
        setPixel(x0 + w, y, true);
        setPixel(x0 + w + 1, y, true);
    }

    // 1 a 3 barritas internas (0 = vacia -> aviso de carga)
    for (uint8_t b = 0; b < level0to3 && b < 3; b++) {
        int bx = x0 + 2 + b * 3;
        for (int y = y0 + 2; y <= y0 + h - 3; y++) {
            setPixel(bx, y, true);
            setPixel(bx + 1, y, true);
        }
    }
}

void EpdDriver::drawPageIndicator(uint8_t currentIndex, uint8_t totalCount) {
    const int spacing = (totalCount > 13) ? 10 : ((totalCount > 10) ? 13 : 16);
    const int startX = (EPD_WIDTH - (int)(totalCount - 1) * spacing) / 2;
    const int py = 10;

    for (uint8_t i = 0; i < totalCount; i++) {
        int px = startX + (int)i * spacing;
        if (i == currentIndex) {
            drawCircle(px, py, 4, true, true);
        } else {
            drawCircle(px, py, 2, false, true);
        }
    }
    drawSpellModeIcon(spellModeEnabled);
    drawBatteryIcon(batteryLevel);
    drawHLine(16, 183, 20, true);
}

void EpdDriver::drawLetterHighlight(uint8_t animalIndex, int8_t letterPos) {
    if (animalIndex >= ANIMAL_COUNT) return;
    const AnimalAsset &asset = ANIMALS[animalIndex];
    drawBitmap1Bit(0, GFX_ZONE3_Y, GFX_ZONE3_W, GFX_ZONE3_H, asset.name_bitmap);

    if (letterPos >= 0 && (uint8_t)letterPos < asset.letter_count) {
        int x0 = asset.letter_x[letterPos];
        int x1 = x0 + asset.letter_w[letterPos] - 1;
        if (x0 < 2) x0 = 2;
        if (x1 > EPD_WIDTH - 3) x1 = EPD_WIDTH - 3;
        // Barra gruesa debajo de la letra activa (y=194..196)
        drawHLine(x0, x1, 194, true);
        drawHLine(x0, x1, 195, true);
        drawHLine(x0, x1, 196, true);
    } else if (letterPos == -2 && asset.letter_count > 0) {
        // Subrayar toda la palabra completa y dibujar puntas de flecha ► / ◄ a los costados
        int x0 = asset.letter_x[0] - 2;
        int last = asset.letter_count - 1;
        int x1 = asset.letter_x[last] + asset.letter_w[last] + 1;
        if (x0 < 2) x0 = 2;
        if (x1 > EPD_WIDTH - 3) x1 = EPD_WIDTH - 3;

        // Subrayado grueso continuo debajo de toda la palabra
        drawHLine(x0, x1, 194, true);
        drawHLine(x0, x1, 195, true);
        drawHLine(x0, x1, 196, true);

        // Flechitas a los costados apuntando a la palabra (y=170..180, centro y=175)
        const int cy = 175;
        for (int d = 0; d <= 4; d++) {
            int half = 4 - d;
            int lx = x0 - 6 + d;
            int rx = x1 + 6 - d;
            for (int dy = -half; dy <= half; dy++) {
                if (lx >= 2) setPixel(lx, cy + dy, true);
                if (rx <= EPD_WIDTH - 3) setPixel(rx, cy + dy, true);
            }
        }
    }
}

void EpdDriver::drawAnimalSpriteOnly(uint8_t animalIndex, bool frameB) {
    if (animalIndex >= ANIMAL_COUNT) return;
    const AnimalAsset &asset = ANIMALS[animalIndex];
    const uint8_t *sprite = frameB ? asset.frame_b : asset.frame_a;
    drawBitmap1Bit(GFX_ZONE2_X, GFX_ZONE2_Y, GFX_ZONE2_W, GFX_ZONE2_H, sprite);
}

void EpdDriver::drawAnimalScreen(uint8_t animalIndex, bool frameB) {
    if (animalIndex >= ANIMAL_COUNT) return;
    clear(0xFF);
    drawPageIndicator(animalIndex, ANIMAL_COUNT);
    drawAnimalSpriteOnly(animalIndex, frameB);
    drawHLine(16, 183, 154, true);
    drawBitmap1Bit(0, GFX_ZONE3_Y, GFX_ZONE3_W, GFX_ZONE3_H, ANIMALS[animalIndex].name_bitmap);
}

void EpdDriver::drawCoverScreen() {
    drawBitmap1Bit(0, 0, EPD_WIDTH, EPD_HEIGHT, gfx_cover);
}

void EpdDriver::drawSleepScreen() {
    clear(0xFF);
    drawBitmap1Bit(0, 0, EPD_WIDTH, EPD_HEIGHT, gfx_sleep_screen);
}
