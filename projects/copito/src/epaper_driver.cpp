#include "epaper_driver.h"
#include "text_paginator.h"
#include "copito_log.h"
#include <SPI.h>

static const char *TAG = "EPD";

// ==============================================================================
// OFFICIAL WAVESHARE 1.54" E-PAPER LOOKUP TABLES (LUTs)
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

struct EpdTaskMsg {
    FaceState state;
    int frame;
    bool forceFull;
};

static QueueHandle_t s_epdQueue = NULL;
static TaskHandle_t s_epdTaskHandle = NULL;
static SemaphoreHandle_t s_epdMutex = NULL;

static void epdBackgroundTask(void *pvParameters) {
    EPaperDriver *driver = (EPaperDriver *)pvParameters;
    EpdTaskMsg msg;

    while (true) {
        if (xQueueReceive(s_epdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            // Drain queue so only the latest animation frame is processed if backlog exists
            while (xQueueReceive(s_epdQueue, &msg, 0) == pdTRUE) {}
            driver->showFaceDirect(msg.state, msg.frame, msg.forceFull);
        }
    }
}

uint8_t g_currentFacePack = DEFAULT_FACE_PACK;
EPaperDriver epd;

EPaperDriver::EPaperDriver() : buffer(nullptr), isPartialReady(false), currentFace(FACE_IDLE) {}

EPaperDriver::~EPaperDriver() {
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }
}

bool EPaperDriver::begin() {
    // Prevent memory leaks if begin() is re-called
    if (buffer) {
        free(buffer);
        buffer = nullptr;
    }

    // Allocate 5000 bytes in PSRAM or internal SRAM
    if (psramFound()) {
        buffer = (uint8_t *)ps_malloc(EPD_BUFFER_SIZE);
    }
    if (!buffer) {
        buffer = (uint8_t *)malloc(EPD_BUFFER_SIZE);
    }
    if (!buffer) {
        log_e("Failed to allocate EPD buffer!");
        return false;
    }
    memset(buffer, 0xFF, EPD_BUFFER_SIZE);

    // Initialize GPIOs
    pinMode(PIN_EPD_PWR, OUTPUT);
    pinMode(PIN_EPD_CS, OUTPUT);
    pinMode(PIN_EPD_DC, OUTPUT);
    pinMode(PIN_EPD_RST, OUTPUT);
    pinMode(PIN_EPD_BUSY, INPUT);

    digitalWrite(PIN_EPD_CS, HIGH);
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_RST, HIGH);

    // Turn ON display power gate using config macro
    powerOn();
    delay(50);

    // Initialize SPI bus (SCK=12, MOSI=13, manual CS on GPIO 11)
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    // Full initialization first
    initFull();
    FaceRenderer::renderIdleFrame(buffer, 0);
    displayFull();

    // Initialize partial refresh mode without hardware reset
    initPartial();
    isPartialReady = true;

    // Create FreeRTOS mutex to synchronize Core 0 background task with Core 1 direct calls
    if (!s_epdMutex) {
        s_epdMutex = xSemaphoreCreateMutex();
    }

    // Create FreeRTOS queue and background task pinned to Core 0 (Audio/Loop run on Core 1)
    if (!s_epdQueue) {
        s_epdQueue = xQueueCreate(4, sizeof(EpdTaskMsg));
        xTaskCreatePinnedToCore(
            epdBackgroundTask,
            "epd_task",
            4096,
            this,
            1, // Low priority background task
            &s_epdTaskHandle,
            0  // Pinned to Core 0 (Zero audio stuttering!)
        );
    }

    log_i("EPaperDriver initialized successfully (200x200 Partial Refresh Ready, Core 0 decoupled)");
    return true;
}

void EPaperDriver::powerOn() {
    digitalWrite(PIN_EPD_PWR, EPD_PWR_ON);
    delay(20);
}

void EPaperDriver::powerOff() {
    digitalWrite(PIN_EPD_PWR, EPD_PWR_OFF);
}

void EPaperDriver::waitBusy() {
    unsigned long start = millis();
    while (digitalRead(PIN_EPD_BUSY) == HIGH) {
        delay(5);
        if (millis() - start > 6000) {
            log_w("EPD Busy timeout!");
            break;
        }
    }
}

void EPaperDriver::spiWriteByte(uint8_t data) {
    SPI.transfer(data);
}

void EPaperDriver::sendCommand(uint8_t cmd) {
    digitalWrite(PIN_EPD_DC, LOW);
    digitalWrite(PIN_EPD_CS, LOW);
    spiWriteByte(cmd);
    digitalWrite(PIN_EPD_CS, HIGH);
}

void EPaperDriver::sendData(uint8_t data) {
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    spiWriteByte(data);
    digitalWrite(PIN_EPD_CS, HIGH);
}

void EPaperDriver::sendDataBytes(const uint8_t *data, size_t len) {
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.writeBytes(data, len);
    digitalWrite(PIN_EPD_CS, HIGH);
}

void EPaperDriver::setWindows(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd) {
    sendCommand(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
    sendData((xStart >> 3) & 0xFF);
    sendData((xEnd >> 3) & 0xFF);

    sendCommand(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
    sendData(yStart & 0xFF);
    sendData((yStart >> 8) & 0xFF);
    sendData(yEnd & 0xFF);
    sendData((yEnd >> 8) & 0xFF);
}

void EPaperDriver::setCursor(uint16_t x, uint16_t y) {
    sendCommand(0x4E); // SET_RAM_X_ADDRESS_COUNTER
    sendData((x >> 3) & 0xFF);

    sendCommand(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
    sendData(y & 0xFF);
    sendData((y >> 8) & 0xFF);
}

void EPaperDriver::setLut(const uint8_t *lut) {
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

void EPaperDriver::initFull() {
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

    sendCommand(0x11); // Data entry mode
    sendData(0x01);

    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);

    sendCommand(0x3C); // Border Waveform
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);

    sendCommand(0x22); // Load Temperature and waveform setting
    sendData(0xB1);
    sendCommand(0x20);

    setCursor(0, EPD_HEIGHT - 1);
    waitBusy();

    setLut(WF_Full_1IN54);
    isPartialReady = false;
}

void EPaperDriver::initPartial() {
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

    sendCommand(0x3C); // BorderWaveform
    sendData(0x80);

    sendCommand(0x22);
    sendData(0xC0);
    sendCommand(0x20);
    waitBusy();

    isPartialReady = true;
}

void EPaperDriver::clear(uint8_t color) {
    if (buffer) {
        memset(buffer, color, EPD_BUFFER_SIZE);
    }
}

void EPaperDriver::displayFull() {
    if (!buffer) return;
    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x24);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);
    
    // Also write to red/secondary SRAM to prevent ghosting
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x26);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);

    sendCommand(0x22);
    sendData(0xC7);
    sendCommand(0x20);
    waitBusy();

    // NO re-inicializar modo parcial aca: el trailing 0x22/0xC0/0x20 volvia a
    // excitar el panel con la LUT parcial sobre la imagen recien fijada y la
    // "aclaraba" un segundo despues. displayPartial() re-inicializa solo
    // cuando hace falta (isPartialReady == false).
}

void EPaperDriver::displayPartial() {
    if (!buffer) return;
    if (!isPartialReady) {
        initPartial();
    }
    setWindows(0, EPD_WIDTH - 1, EPD_HEIGHT - 1, 0);
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x24); // Write RAM
    sendDataBytes(buffer, EPD_BUFFER_SIZE);

    sendCommand(0x22); // Display update control: TurnOnDisplayPart (factory value: 0xCF)
    sendData(0xCF);
    sendCommand(0x20); // Activate display update
    waitBusy();

    // Sincronizar memoria 0x26 (Previous RAM) con el buffer actual para que el siguiente
    // ciclo diferencial sea exacto y no relaje los pigmentos negros a blanco
    setCursor(0, EPD_HEIGHT - 1);
    sendCommand(0x26);
    sendDataBytes(buffer, EPD_BUFFER_SIZE);
}

void EPaperDriver::displayPartialWindow(int x, int y, int w, int h) {
    if (!buffer) return;
    if (w <= 0 || h <= 0) return;
    // Clamp al panel
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EPD_WIDTH) w = EPD_WIDTH - x;
    if (y + h > EPD_HEIGHT) h = EPD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    if (!isPartialReady) {
        initPartial();
    }
    // Alinear X a bytes (el controlador direcciona X en pasos de 8 px)
    int x0a = (x >> 3) << 3;
    int x1 = ((x + w + 7) >> 3) << 3;
    if (x1 > EPD_WIDTH) x1 = EPD_WIDTH;
    x1 -= 1; // inclusivo
    if (x1 < x0a) return;
    int y0fb = y;
    int y1fb = y + h - 1;
    // Mapeo a coords del controlador (entry mode Y-decremento: el driver
    // usa yStart=199,yEnd=0 y cursor 199 para pantalla completa).
    int yStartCtrl = (EPD_HEIGHT - 1) - y0fb;
    int yEndCtrl = (EPD_HEIGHT - 1) - y1fb;
    int wBytes = (x1 >> 3) - (x0a >> 3) + 1;
    int rows = y1fb - y0fb + 1;
    static uint8_t winBuf[((200 + 7) / 8) * 200]; // 25*200 = 5000 máx
    if ((size_t)(wBytes * rows) > sizeof(winBuf)) return; // no debería pasar
    int stride = EPD_WIDTH / 8; // 25
    for (int r = 0; r < rows; r++) {
        const uint8_t *src = buffer + (y0fb + r) * stride + (x0a >> 3);
        uint8_t *dst = winBuf + r * wBytes;
        memcpy(dst, src, wBytes);
    }
    auto setWin = [&]() {
        setWindows((uint16_t)x0a, (uint16_t)yStartCtrl, (uint16_t)x1, (uint16_t)yEndCtrl);
        setCursor((uint16_t)x0a, (uint16_t)yStartCtrl);
    };
    setWin();
    sendCommand(0x24);
    sendDataBytes(winBuf, (size_t)(wBytes * rows));
    sendCommand(0x22);
    sendData(0xCF);
    sendCommand(0x20);
    waitBusy();
    // Sincronizar RAM previa (0x26) solo en la ventana para que el próximo
    // diferencial sea exacto sin reenviar los 5000 B.
    setWin();
    sendCommand(0x26);
    sendDataBytes(winBuf, (size_t)(wBytes * rows));
}

// Geometría del reloj gigante (debe coincidir con renderClockFace en
// faces_bitmaps.h): hora 60px alto, 30px ancho, trazo 8px.
static const int CLK_X0 = 22, CLK_Y0 = 45, CLK_W = 30, CLK_H = 60;
static const int CLK_GAP = 7, CLK_S = 8;
static const int CLK_CX = CLK_X0 + (CLK_W + CLK_GAP) * 2 + 2; // 98
static const int CLK_X3 = CLK_CX + 15;                        // 113

bool EPaperDriver::showClockMinutesWindowed(int prevHour, int prevMin, int hour, int min) {
    if (!buffer) return false;
    if (!isPartialReady) return false;
    if (prevHour < 0 || prevMin < 0) return false;
    char prev[6], cur[6];
    snprintf(prev, sizeof(prev), "%02d:%02d", prevHour, prevMin);
    snprintf(cur, sizeof(cur), "%02d:%02d", hour, min);
    if (strcmp(prev, cur) == 0) return true; // nada que hacer
    // Drenar animaciones residuales de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);
    currentFace = FACE_CLOCK;
    // Posiciones X de los 4 dígitos (índices de char 0,1,3,4)
    const int xs[4] = { CLK_X0, CLK_X0 + CLK_W + CLK_GAP, CLK_X3, CLK_X3 + CLK_W + CLK_GAP };
    const int ci[4] = { 0, 1, 3, 4 };
    int minX = EPD_WIDTH, maxX = -1;
    for (int i = 0; i < 4; i++) {
        if (prev[ci[i]] != cur[ci[i]]) {
            if (xs[i] < minX) minX = xs[i];
            if (xs[i] + CLK_W > maxX) maxX = xs[i] + CLK_W;
        }
    }
    if (maxX < 0) { // solo cambió ':' (imposible) o nada
        if (s_epdMutex) xSemaphoreGive(s_epdMutex);
        return true;
    }
    // Margen de 2 px para limpiar bordes del trazo anterior
    const int M = 2;
    int bx = minX - M, by = CLK_Y0 - M;
    int bw = (maxX - minX) + 2 * M, bh = CLK_H + 2 * M;
    // 1) Blanquear la unión en RAM
    FaceRenderer::drawRect(buffer, bx, by, bw, bh, 1, true);
    // 2) Redibujar los dígitos nuevos que caen dentro (cambiados o no, para
    // no dejar huecos si la unión cubre un dígito intacto intermedio)
    for (int i = 0; i < 4; i++) {
        if (xs[i] + CLK_W > bx && xs[i] < bx + bw) {
            FaceRenderer::drawBigDigit(buffer, xs[i], CLK_Y0, CLK_W, CLK_H, CLK_S, cur[ci[i]]);
        }
    }
    // 3) Excitar solo la ventana manteniendo s_epdMutex tomado (sin ventana sin mutex):
    // Reimplementación inline de displayPartialWindow sin re-tomar mutex:
    int x = bx, yy = by, w = bw, h = bh;
    if (x < 0) { w += x; x = 0; }
    if (yy < 0) { h += yy; yy = 0; }
    if (x + w > EPD_WIDTH) w = EPD_WIDTH - x;
    if (yy + h > EPD_HEIGHT) h = EPD_HEIGHT - yy;
    bool ok = (w > 0 && h > 0);
    if (ok) {
        int x0a = (x >> 3) << 3;
        int x1 = ((x + w + 7) >> 3) << 3;
        if (x1 > EPD_WIDTH) x1 = EPD_WIDTH;
        x1 -= 1;
        int y0fb = yy, y1fb = yy + h - 1;
        int yStartCtrl = (EPD_HEIGHT - 1) - y0fb;
        int yEndCtrl = (EPD_HEIGHT - 1) - y1fb;
        int wBytes = (x1 >> 3) - (x0a >> 3) + 1;
        int rows = y1fb - y0fb + 1;
        static uint8_t winBuf2[((200 + 7) / 8) * 200];
        if ((size_t)(wBytes * rows) <= sizeof(winBuf2)) {
            int stride = EPD_WIDTH / 8;
            for (int r = 0; r < rows; r++) {
                memcpy(winBuf2 + r * wBytes,
                       buffer + (y0fb + r) * stride + (x0a >> 3), wBytes);
            }
            setWindows((uint16_t)x0a, (uint16_t)yStartCtrl, (uint16_t)x1, (uint16_t)yEndCtrl);
            setCursor((uint16_t)x0a, (uint16_t)yStartCtrl);
            sendCommand(0x24);
            sendDataBytes(winBuf2, (size_t)(wBytes * rows));
            sendCommand(0x22);
            sendData(0xCF);
            sendCommand(0x20);
            waitBusy();
            setWindows((uint16_t)x0a, (uint16_t)yStartCtrl, (uint16_t)x1, (uint16_t)yEndCtrl);
            setCursor((uint16_t)x0a, (uint16_t)yStartCtrl);
            sendCommand(0x26);
            sendDataBytes(winBuf2, (size_t)(wBytes * rows));
            CLOGI(TAG, "reloj windowed %02d:%02d->%02d:%02d win x=%d w=%d (%d B, %d ciclo)",
                  prevHour, prevMin, hour, min, x0a, (x1 - x0a + 1),
                  wBytes * rows, 1);
        } else {
            ok = false;
        }
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
    return ok;
}

void EPaperDriver::showFace(FaceState state, int frame, bool forceFullRefresh) {
    if (s_epdQueue) {
        EpdTaskMsg msg = { state, frame, forceFullRefresh };
        if (forceFullRefresh) {
            xQueueSend(s_epdQueue, &msg, pdMS_TO_TICKS(100));
        } else {
            // Non-blocking post: drops frame if queue is full, keeping audio perfectly smooth!
            xQueueSend(s_epdQueue, &msg, 0);
        }
    } else {
        showFaceDirect(state, frame, forceFullRefresh);
    }
}

void EPaperDriver::showFaceDirect(FaceState state, int frame, bool forceFullRefresh) {
    if (!buffer) return;
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = state;
    switch (state) {
        case FACE_IDLE:
            FaceRenderer::renderIdleFrame(buffer, frame);
            break;
        case FACE_LISTENING:
            FaceRenderer::renderListeningFrame(buffer, frame);
            break;
        case FACE_THINKING:
            FaceRenderer::renderThinkingFrame(buffer, frame);
            break;
        case FACE_SPEAKING:
            FaceRenderer::renderSpeakingFrame(buffer, frame);
            break;
        case FACE_ERROR:
            FaceRenderer::renderErrorFrame(buffer, "ERROR DE IA", "Intente de nuevo");
            break;
        case FACE_PORTAL:
            FaceRenderer::renderPortalFrame(buffer, frame);
            break;
        case FACE_WIFI_CONNECTING:
            FaceRenderer::renderConnectingFrame(buffer, frame);
            break;
        case FACE_SLEEP:
            FaceRenderer::renderSleepFrame(buffer);
            break;
        case FACE_USB_PC:
            FaceRenderer::renderUsbPcFrame(buffer);
            break;
        case FACE_CHARGING:
            FaceRenderer::renderChargingFrame(buffer);
            break;
        case FACE_HAPPY:
            FaceRenderer::renderHappyFrame(buffer);
            break;
        case FACE_FALLBACK_TEXT:
            break;
    }

    if (forceFullRefresh) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showFallbackText(int drawType, const char *text, bool forceFullRefresh) {
    if (!buffer) return;
    // Drenar cualquier animacion residual pendiente en la cola de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::renderFallbackDrawFrame(buffer, drawType, text);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::renderPaginatorPage(TextPaginator &p, const char *title, bool forceFull) {
    if (!buffer) return;
    // Drenar cualquier animacion residual pendiente en la cola de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    p.render(buffer, title);
    if (forceFull || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    p.notifyRefreshExecuted(forceFull);
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showClockFace(int hour, int min, float tempC, float humPct, int battPct, const char *dateStr, bool forceFullRefresh) {
    if (!buffer) return;
    // Drenar cualquier animación de cara residual pendiente en la cola de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_CLOCK;
    FaceRenderer::renderClockFace(buffer, hour, min, tempC, humPct, battPct, dateStr);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showWeatherScreen(int hour, int min, float tempC, float humPct, int battPct, const char *conditionText, bool forceFullRefresh) {
    if (!buffer) return;
    // Drenar cualquier animación residual pendiente en la cola de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_WEATHER;
    FaceRenderer::renderWeatherScreen(buffer, hour, min, tempC, humPct, battPct, conditionText);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showVectorSketch(const char *commands, const char *caption, bool forceFullRefresh) {
    if (!buffer) return;
    CLOGI(TAG, "showVectorSketch: renderizando %u bytes (cap='%s', refresh=%s)", 
          commands ? strlen(commands) : 0, caption ? caption : "", forceFullRefresh ? "FULL" : "PARTIAL");
    // Drenar cualquier animación residual pendiente en la cola de Core 0
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::renderVectorSketch(buffer, commands, caption);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
        // Doble barrido FULL: el primer ciclo deja los negros a medio tono en
        // algunas partidas del panel; el segundo los fija profundos y sin fantasmas.
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showMusicScreen(const char *title, const char *status, bool forceFullRefresh) {
    if (!buffer) return;
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::renderMusicFrame(buffer, title, status);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showMusicLogo(const char *title, const char *status, const uint8_t *logo64) {
    if (!buffer || !logo64) return;
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::renderMusicFrameWithLogo(buffer, title, status, logo64);
    if (!isPartialReady) {
        initFull();
    }
    displayFull();
    displayFull(); // doble barrido: fija negros (ver showVectorSketch)
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showSpeechProgress(int done, int total, bool forceFullRefresh) {
    if (!buffer) return;
    if (total < 1) total = 1;
    if (done < 0) done = 0;
    if (done > total) done = total;
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::clearBuffer(buffer, 0xFF);
    FaceRenderer::drawTopBar(buffer);
    // Titulo centrado 2X
    {
        const char *t = "HABLANDO";
        int x = (200 - 8 * 12) / 2;
        FaceRenderer::drawString(buffer, x, 48, t, 0, 2);
    }
    // Barra: marco x20..180, relleno proporcional
    FaceRenderer::drawRect(buffer, 20, 100, 160, 16, 0, false);
    int fill = (160 * done) / total;
    if (fill > 0) FaceRenderer::drawRect(buffer, 22, 102, fill - 4 > 0 ? fill - 4 : fill, 12, 0, true);
    // Contador "Voz 2/8" centrado 1X
    {
        char c[16];
        snprintf(c, sizeof c, "Voz %d/%d", done, total);
        int l = strlen(c);
        int x = (200 - l * 6) / 2;
        if (x < 4) x = 4;
        FaceRenderer::drawString(buffer, x, 128, c, 0, 1);
    }
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

// Pantalla de carga del cuento en cascada: texto fijo con rima (FULL una sola
// vez) + timer que avanza pixel a pixel con PARTIAL (no barre toda la pantalla).
// Columnas de 1px dentro de la pista x22..177 (156 pasos); al llenarse se
// reinicia con un FULL. El loop() anima con epd.updateStoryLoading(n++).
static uint16_t s_storyLoadDrawn = 0;
static const uint16_t STORY_LOAD_TRACK_X = 22;
static const uint16_t STORY_LOAD_TRACK_N = 156;
static const uint16_t STORY_LOAD_Y0 = 142;
static const uint16_t STORY_LOAD_Y1 = 149;

void EPaperDriver::beginStoryLoading() {
    if (!buffer) return;
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    FaceRenderer::clearBuffer(buffer, 0xFF);
    FaceRenderer::drawTopBar(buffer);
    // Titulo 2X fijo
    {
        const char *t = "CUENTO";
        int x = (200 - 6 * 12) / 2;
        FaceRenderer::drawString(buffer, x, 40, t, 0, 2);
    }
    // Rima fija 1X centrada (no cambia nunca: cero refrescos de texto)
    {
        const char *l1 = "Un cuentito estoy pensando,";
        const char *l2 = "los dibujos van llegando.";
        int x1 = (200 - (int)strlen(l1) * 6) / 2;
        int x2 = (200 - (int)strlen(l2) * 6) / 2;
        if (x1 < 2) x1 = 2;
        if (x2 < 2) x2 = 2;
        FaceRenderer::drawString(buffer, x1, 88, l1, 0, 1);
        FaceRenderer::drawString(buffer, x2, 102, l2, 0, 1);
    }
    // Pista del timer vacia
    FaceRenderer::drawRect(buffer, 20, 140, 160, 12, 0, false);
    s_storyLoadDrawn = 0;
    if (!isPartialReady) {
        initFull();
    }
    displayFull();
    displayFull(); // doble barrido: fija negros del texto
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::updateStoryLoading(uint16_t step) {
    if (!buffer) return;
    uint16_t pos = step % STORY_LOAD_TRACK_N;
    if (pos < s_storyLoadDrawn) {
        // Pista llena: reinicio con FULL (pasa cada ~40s a 260ms/paso)
        beginStoryLoading();
        return;
    }
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    // Solo se AGREGAN pixeles negros (operacion limpia en PARTIAL, sin fantasmas)
    for (uint16_t s = s_storyLoadDrawn; s <= pos; s++) {
        FaceRenderer::drawLine(buffer, STORY_LOAD_TRACK_X + s, STORY_LOAD_Y0,
                               STORY_LOAD_TRACK_X + s, STORY_LOAD_Y1, 0, 1);
    }
    s_storyLoadDrawn = pos + 1;
    if (!isPartialReady) {
        initFull();
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::showBitmap(const uint8_t *bitmap, bool forceFullRefresh) {
    if (!buffer || !bitmap) return;
    CLOGI(TAG, "showBitmap: renderizando 5000 bytes bitmap (refresh=%s)", forceFullRefresh ? "FULL" : "PARTIAL");
    if (s_epdQueue) {
        EpdTaskMsg dummy;
        while (xQueueReceive(s_epdQueue, &dummy, 0) == pdTRUE) {}
    }
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);

    currentFace = FACE_FALLBACK_TEXT;
    memcpy(buffer, bitmap, EPD_BUFFER_SIZE);
    if (forceFullRefresh || !isPartialReady) {
        initFull();
        displayFull();
        // Doble barrido FULL: fija negros profundos (ver showVectorSketch).
        displayFull();
    } else {
        displayPartial();
    }
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
}

void EPaperDriver::sleep() {
    sendCommand(0x10); // SSD1681 / Waveshare Deep Sleep Mode
    sendData(0x01);
    delay(100);
}

bool EPaperDriver::snapshot(uint8_t *dst) {
    if (!buffer || !dst) return false;
    if (s_epdMutex) xSemaphoreTake(s_epdMutex, portMAX_DELAY);
    memcpy(dst, buffer, EPD_BUFFER_SIZE);
    if (s_epdMutex) xSemaphoreGive(s_epdMutex);
    return true;
}
