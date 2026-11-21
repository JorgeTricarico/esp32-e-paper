#ifndef FACES_BITMAPS_H
#define FACES_BITMAPS_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "battery_monitor.h"

// Global ambient sensor data from SHTC3
extern float g_ambientTempC;
extern float g_ambientHum;
extern bool  g_ambientSensorValid;
extern uint8_t g_currentFacePack;

enum FaceState {
    FACE_IDLE = 0,
    FACE_LISTENING,
    FACE_THINKING,
    FACE_SPEAKING,
    FACE_ERROR,
    FACE_PORTAL,
    FACE_WIFI_CONNECTING,
    FACE_SLEEP,
    FACE_USB_PC,
    FACE_CHARGING,
    FACE_HAPPY,
    FACE_FALLBACK_TEXT,
    FACE_CLOCK,
    FACE_WEATHER
};

// Sub-animation states for micro-movement
enum EyeBlinkState {
    BLINK_OPEN = 0,
    BLINK_HALF = 1,
    BLINK_ALMOST = 2,
    BLINK_CLOSED = 3
};

enum MouthPhoneme {
    MOUTH_REST = 0,
    MOUTH_E = 1,
    MOUTH_O = 2,
    MOUTH_A = 3,
    MOUTH_THINKING = 4
};

// ==============================================================================
// ADVANCED 200x200 MONOCHROME ARTWORK & MICRO-ANIMATION ENGINE
// Features: Ordered Bayer Dithering, High-Fidelity Cyber Companion Eyes,
//           Smooth Eyelid Glides, Pupil Gaze Tracking, and Viseme Mouth Lip-Sync.
// ==============================================================================

class FaceRenderer {
public:
    static void clearBuffer(uint8_t *buf, uint8_t fillVal = 0xFF) {
        memset(buf, fillVal, EPD_BUFFER_SIZE);
    }

    static inline uint8_t getFacePack() { return g_currentFacePack; }
    static inline void setFacePack(uint8_t pack) { g_currentFacePack = (pack <= FACE_PACK_KRAKEN) ? pack : DEFAULT_FACE_PACK; }

    static inline void drawPixel(uint8_t *buf, int x, int y, uint8_t color) {
        if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
        uint16_t index = y * 25 + (x >> 3);
        uint8_t bit = 7 - (x & 0x07);
        if (color) {
            buf[index] |= (1 << bit);   // 1 = White
        } else {
            buf[index] &= ~(1 << bit);  // 0 = Black
        }
    }

    // Dithered pixel (gray level 0..16: 0 = pure black, 16 = pure white)
    static inline void drawDitherPixel(uint8_t *buf, int x, int y, uint8_t grayLevel16) {
        if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
        static const uint8_t bayer[4][4] = {
            {  0,  8,  2, 10 },
            { 12,  4, 14,  6 },
            {  3, 11,  1,  9 },
            { 15,  7, 13,  5 }
        };
        uint8_t threshold = bayer[y & 3][x & 3];
        drawPixel(buf, x, y, (grayLevel16 > threshold) ? 1 : 0);
    }

    static void drawLine(uint8_t *buf, int x0, int y0, int x1, int y1, uint8_t color, int thickness = 1) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;

        while (true) {
            if (thickness <= 1) {
                drawPixel(buf, x0, y0, color);
            } else {
                for (int ty = -thickness / 2; ty <= thickness / 2; ty++) {
                    for (int tx = -thickness / 2; tx <= thickness / 2; tx++) {
                        drawPixel(buf, x0 + tx, y0 + ty, color);
                    }
                }
            }
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    static void drawFilledCircleDither(uint8_t *buf, int xm, int ym, int r, uint8_t grayLevel) {
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x * x + y * y <= r * r) {
                    drawDitherPixel(buf, xm + x, ym + y, grayLevel);
                }
            }
        }
    }

    static void drawCircle(uint8_t *buf, int xm, int ym, int r, uint8_t color, bool fill = false, int thick = 2) {
        // Grosor >=2: anillos concentricos (r, r-1, ...) para trazo visible en e-paper.
        // Sin el anillo extra los contornos de 1px se ven palidos ("muy claros").
        int passes = (fill || thick < 2) ? 1 : thick;
        for (int ring = 0; ring < passes; ring++) {
            int rr = r - ring;
            if (rr < 1) break;
            int x = -rr, y = 0, err = 2 - 2 * rr;
            do {
                if (fill) {
                    for (int i = xm + x; i <= xm - x; i++) {
                        drawPixel(buf, i, ym + y, color);
                        drawPixel(buf, i, ym - y, color);
                    }
                } else {
                    drawPixel(buf, xm - x, ym + y, color);
                    drawPixel(buf, xm - y, ym - x, color);
                    drawPixel(buf, xm + x, ym - y, color);
                    drawPixel(buf, xm + y, ym + x, color);
                }
                rr = err;
                if (rr <= y) err += ++y * 2 + 1;
                if (rr > x || err > y) err += ++x * 2 + 1;
            } while (x < 0);
        }
    }

    static void drawRect(uint8_t *buf, int x, int y, int w, int h, uint8_t color, bool fill = false) {
        if (fill) {
            for (int cy = y; cy < y + h; cy++) {
                drawLine(buf, x, cy, x + w - 1, cy, color, 1);
            }
        } else {
            // Contorno de 2px: en e-paper 200x200 el borde de 1px se ve palido.
            drawLine(buf, x, y, x + w - 1, y, color, 2);
            drawLine(buf, x, y + h - 1, x + w - 1, y + h - 1, color, 2);
            drawLine(buf, x, y, x, y + h - 1, color, 2);
            drawLine(buf, x + w - 1, y, x + w - 1, y + h - 1, color, 2);
        }
    }

    // Glifo monocromático HD de batería con 5 niveles escalonados y rayo de carga
    static void drawBatteryIcon(uint8_t *buf, int bx, int by, int battPct, bool isCharging) {
        // Cuerpo: 12px de ancho x 7px de alto (borde externo)
        drawRect(buf, bx, by, 12, 7, 0, false);
        // Borne positivo a la derecha (2px ancho x 3px alto)
        drawRect(buf, bx + 12, by + 2, 2, 3, 0, true);

        if (isCharging) {
            // Rayo de carga nítido
            drawLine(buf, bx + 6, by + 1, bx + 4, by + 3, 0, 1);
            drawLine(buf, bx + 4, by + 3, bx + 7, by + 3, 0, 1);
            drawLine(buf, bx + 7, by + 3, bx + 5, by + 5, 0, 1);
        } else {
            // Niveles escalonados: interior de bx+2 a bx+9 (8px de ancho total, y=by+2..by+4)
            int p = constrain(battPct, 0, 100);
            if (p <= 5) {
                // Nivel crítico: vacío
            } else if (p <= 25) {
                drawRect(buf, bx + 2, by + 2, 2, 3, 0, true);
            } else if (p <= 50) {
                drawRect(buf, bx + 2, by + 2, 2, 3, 0, true);
                drawRect(buf, bx + 5, by + 2, 2, 3, 0, true);
            } else if (p <= 75) {
                drawRect(buf, bx + 2, by + 2, 2, 3, 0, true);
                drawRect(buf, bx + 5, by + 2, 2, 3, 0, true);
                drawRect(buf, bx + 8, by + 2, 2, 3, 0, true);
            } else {
                drawRect(buf, bx + 2, by + 2, 8, 3, 0, true);
            }
        }
    }

    // ==========================================================================
    // HIGH-FIDELITY CHARACTER COMPONENTS (Cyber Robot Companion)
    // ==========================================================================

    // Rounded Chassis Screen Border
    static void drawChassisFrame(uint8_t *buf) {
        // Outer curved bezel
        drawLine(buf, 16, 8, 184, 8, 0, 2);
        drawLine(buf, 16, 192, 184, 192, 0, 2);
        drawLine(buf, 8, 16, 8, 184, 0, 2);
        drawLine(buf, 192, 16, 192, 184, 0, 2);

        // Corner accents
        drawPixel(buf, 9, 10, 0); drawPixel(buf, 10, 9, 0);
        drawPixel(buf, 190, 10, 0); drawPixel(buf, 189, 9, 0);
        drawPixel(buf, 9, 190, 0); drawPixel(buf, 10, 189, 0);
        drawPixel(buf, 190, 190, 0); drawPixel(buf, 189, 189, 0);

        // Cute mini robot ear widgets on top sides
        drawLine(buf, 2, 80, 7, 75, 0, 2);
        drawLine(buf, 2, 80, 2, 105, 0, 2);
        drawLine(buf, 2, 105, 7, 110, 0, 2);

        drawLine(buf, 197, 80, 192, 75, 0, 2);
        drawLine(buf, 197, 80, 197, 105, 0, 2);
        drawLine(buf, 197, 105, 192, 110, 0, 2);
    }

    // 4-point kawaii sparkle star (+ shape with diamond core)
    static void drawSparkle(uint8_t *buf, int cx, int cy, int r = 3) {
        drawLine(buf, cx - r, cy, cx + r, cy, 0, 1);
        drawLine(buf, cx, cy - r, cx, cy + r, 0, 1);
        if (r >= 3) {
            drawPixel(buf, cx - 1, cy - 1, 0);
            drawPixel(buf, cx + 1, cy - 1, 0);
            drawPixel(buf, cx - 1, cy + 1, 0);
            drawPixel(buf, cx + 1, cy + 1, 0);
        }
    }

    // Expressive eyebrows (0 = gentle happy, 1 = raised alert, 2 = inquisitive arch, 3 = focused slant)
    static void drawEyebrow(uint8_t *buf, int cx, int cy, int type) {
        if (type == 0) {
            // Gentle happy arched eyebrow
            for (int x = -13; x <= 13; x++) {
                float norm = (float)x / 13.0f;
                int y = cy - (int)((1.0f - norm * norm) * 4.0f);
                drawPixel(buf, cx + x, y, 0);
                drawPixel(buf, cx + x, y + 1, 0);
            }
        } else if (type == 1) {
            // Raised alert eyebrow (Listening)
            for (int x = -14; x <= 14; x++) {
                float norm = (float)x / 14.0f;
                int y = cy - 2 - (int)((1.0f - norm * norm) * 5.0f);
                drawPixel(buf, cx + x, y, 0);
                drawPixel(buf, cx + x, y + 1, 0);
            }
        } else if (type == 2) {
            // Inquisitive eyebrow (Thinking Left - high curious arch)
            for (int x = -14; x <= 14; x++) {
                float norm = (float)(x - 2) / 14.0f;
                int y = cy - 3 - (int)((1.0f - norm * norm) * 5.0f);
                drawPixel(buf, cx + x, y, 0);
                drawPixel(buf, cx + x, y + 1, 0);
            }
        } else if (type == 3) {
            // Concentrated slightly slanted eyebrow (Thinking Right)
            for (int x = -13; x <= 13; x++) {
                int y = cy + (x * 3) / 13;
                drawPixel(buf, cx + x, y, 0);
                drawPixel(buf, cx + x, y + 1, 0);
            }
        }
    }

    // Detailed Layered Glossy Eye with Dynamic Eyelid, Gaze & Iris Highlights
    static void drawDetailedEye(uint8_t *buf, int cx, int cy, int pupilOffsetX, int pupilOffsetY, EyeBlinkState blink) {
        if (blink == BLINK_CLOSED) {
            // Sweet curved happy smile line for closed eye with tapered ends
            for (int x = -22; x <= 22; x++) {
                float norm = (float)x / 22.0f;
                int y = cy + 4 - (int)((1.0f - norm * norm) * 9.0f);
                int thick = (abs(x) < 14) ? 3 : 2;
                for (int t = 0; t < thick; t++) {
                    drawPixel(buf, cx + x, y + t, 0);
                }
            }
            // Cute upturned outer eyelash flick
            drawPixel(buf, cx - 23, cy + 1, 0);
            drawPixel(buf, cx - 24, cy - 1, 0);
            drawPixel(buf, cx + 23, cy + 1, 0);
            drawPixel(buf, cx + 24, cy - 1, 0);
            return;
        }

        // 1. Outer Eye Contour: Beautiful oval (width 32, height 42)
        int rx = 16, ry = 21;
        if (blink == BLINK_HALF)   ry = 12;
        if (blink == BLINK_ALMOST) ry = 5;

        // Draw solid glossy dark eye socket
        for (int y = -ry; y <= ry; y++) {
            for (int x = -rx; x <= rx; x++) {
                float dist = (float)(x * x) / (rx * rx) + (float)(y * y) / (ry * ry);
                if (dist <= 1.0f) {
                    drawPixel(buf, cx + x, cy + y, 0);
                }
            }
        }

        if (blink >= BLINK_ALMOST) return; // Very thin slit, no inner highlights needed

        // 2. Catchlights (Reflejos de la pupila llenos de vida y brillo)
        // Primary Catchlight (Grande superior izquierda)
        drawCircle(buf, cx - 5 + pupilOffsetX, cy - 7 + pupilOffsetY, 5, 1, true);
        // Secondary Catchlight (Inferior derecha)
        drawCircle(buf, cx + 6 + pupilOffsetX, cy + 6 + pupilOffsetY, 3, 1, true);
        // Tertiary Catchlight (Destellito inferior izquierdo)
        drawCircle(buf, cx - 2 + pupilOffsetX, cy + 8 + pupilOffsetY, 2, 1, true);

        // 3. Iris crescent light reflection (sutil brillo en arco inferior)
        for (int ix = -7; ix <= 7; ix++) {
            int iy = cy + ry - 4 - (int)((7 * 7 - ix * ix) * 0.04f);
            if (((ix + iy) & 1) == 0) {
                drawPixel(buf, cx + ix + pupilOffsetX / 2, iy, 1);
            }
        }

        // 4. Eyelid crease line above the eye (parpado superior estilizado)
        for (int x = -14; x <= 14; x++) {
            int ey = cy - ry - 4 - (int)(cos(x * 0.12f) * 2.0f);
            drawPixel(buf, cx + x, ey, 0);
        }
    }

    // Japanese anime kawaii blush lines (///) - 100% crisp vector, perfectly proportioned
    static void drawCuteBlush(uint8_t *buf, int cx, int cy) {
        // Linea izquierda (largo 8)
        drawLine(buf, cx - 7, cy + 4, cx - 3, cy - 4, 0, 2);
        // Linea central (largo 12, mas prominente)
        drawLine(buf, cx - 2, cy + 6, cx + 3, cy - 6, 0, 2);
        // Linea derecha (largo 8)
        drawLine(buf, cx + 4, cy + 4, cx + 8, cy - 4, 0, 2);
    }

    // Expressive Viseme Mouth (Lip-Sync and Moods)
    static void drawMouth(uint8_t *buf, int cx, int cy, MouthPhoneme phoneme) {
        switch (phoneme) {
            case MOUTH_REST:
                // Gentle smiling curve (curves down at center, corners pulled up into a warm smile)
                for (int x = -20; x <= 20; x++) {
                    float norm = (float)x / 20.0f;
                    int y = cy + (int)((1.0f - (norm * norm)) * 7.0f);
                    int thick = (abs(x) < 8) ? 3 : 2;
                    for (int t = 0; t < thick; t++) {
                        drawPixel(buf, cx + x, y + t, 0);
                    }
                }
                // Smile dimple endpoints (curling up slightly)
                drawPixel(buf, cx - 21, cy + 1, 0);
                drawPixel(buf, cx - 22, cy, 0);
                drawPixel(buf, cx + 21, cy + 1, 0);
                drawPixel(buf, cx + 22, cy, 0);
                break;

            case MOUTH_E:
                // Wide speaking mouth (horizontal smile)
                for (int x = -22; x <= 22; x++) {
                    drawLine(buf, cx + x, cy - 2, cx + x, cy + 4, 0, 1);
                }
                // White teeth line
                drawLine(buf, cx - 18, cy + 1, cx + 18, cy + 1, 1, 2);
                break;

            case MOUTH_O:
                // Expressive round talking mouth ('O' phoneme) - crisp solid outline with white inner
                drawCircle(buf, cx, cy + 2, 12, 0, true);
                drawCircle(buf, cx, cy + 2, 8, 1, true);
                drawCircle(buf, cx + 2, cy - 2, 3, 1, true); // Catchlight
                break;

            case MOUTH_A:
                // Wide laughing/talking mouth ('A' phoneme - large open D-shape)
                for (int y = 0; y <= 24; y++) {
                    int w = (int)(sqrt(1.0f - pow(y / 24.0f, 2.0f)) * 24.0f);
                    drawLine(buf, cx - w, cy - 4 + y, cx + w, cy - 4 + y, 0, 1);
                }
                // Cute tongue highlight at the bottom
                drawCircle(buf, cx, cy + 14, 10, 1, true);
                drawCircle(buf, cx, cy + 14, 9, 0, false);
                break;

            case MOUTH_THINKING:
                // Cute wavy anime pursed mouth ('w' style)
                for (int x = -10; x <= 0; x++) {
                    float norm = (float)(x + 5) / 5.0f;
                    int y = cy + 3 - (int)((norm * norm) * 3.5f);
                    drawPixel(buf, cx + x, y, 0);
                    drawPixel(buf, cx + x, y + 1, 0);
                }
                for (int x = 0; x <= 10; x++) {
                    float norm = (float)(x - 5) / 5.0f;
                    int y = cy + 3 - (int)((norm * norm) * 3.5f);
                    drawPixel(buf, cx + x, y, 0);
                    drawPixel(buf, cx + x, y + 1, 0);
                }
                break;
        }
    }

    // --- FACE PACK GRAPHICS & ACCESSORIES ---

    // 1. Pack Rudo: Aviator / Terminator Badass Cyber Shades
    static void drawRudoShades(uint8_t *buf, int cy = 88, int tilt = 0) {
        int yTop = cy - 18 + tilt;

        // 1. Top heavy brow bar
        drawLine(buf, 30, yTop, 170, yTop, 0, 4);

        // 2. Center nose bridge
        drawLine(buf, 92, yTop + 6, 108, yTop + 6, 0, 3);
        drawLine(buf, 92, yTop + 7, 108, yTop + 7, 0, 2);

        // 3. Left lens: Aviator teardrop shape (x: 36..90, y: yTop+2..yTop+38)
        for (int y = 0; y <= 36; y++) {
            int xLeft = (y > 20) ? (36 + (y - 20) / 3) : 36;
            int xRight = (y > 16) ? (90 - (y - 16) / 4) : 90;
            drawLine(buf, xLeft, yTop + 2 + y, xRight, yTop + 2 + y, 0, 1);
        }

        // 4. Right lens: Aviator teardrop shape (x: 110..164, y: yTop+2..yTop+38)
        for (int y = 0; y <= 36; y++) {
            int xLeft = (y > 16) ? (110 + (y - 16) / 4) : 110;
            int xRight = (y > 20) ? (164 - (y - 20) / 3) : 164;
            drawLine(buf, xLeft, yTop + 2 + y, xRight, yTop + 2 + y, 0, 1);
        }

        // 5. White specular diagonal reflection shine across lenses
        drawLine(buf, 48, yTop + 4, 76, yTop + 34, 1, 3);
        drawLine(buf, 58, yTop + 4, 82, yTop + 30, 1, 2);

        drawLine(buf, 122, yTop + 4, 150, yTop + 34, 1, 3);
        drawLine(buf, 132, yTop + 4, 156, yTop + 30, 1, 2);

        // 6. Cyber temple hinge pins
        drawLine(buf, 24, yTop + 2, 32, yTop + 2, 0, 3);
        drawLine(buf, 168, yTop + 2, 176, yTop + 2, 0, 3);
    }

    // 1b. Pack Rudo: Canchero Smirk / Tough Grin
    static void drawRudoMouth(uint8_t *buf, int cx = 100, int cy = 136) {
        drawLine(buf, 74, cy - 2, 120, cy, 0, 3);
        drawLine(buf, 74, cy - 1, 120, cy + 1, 0, 2);
        drawLine(buf, 72, cy - 5, 75, cy - 1, 0, 3);
        drawLine(buf, 95, cy + 9, 105, cy + 9, 0, 2);
    }

    // 1c. Pack Kraken: cejas pesadas en V de enojo (alta afuera, baja adentro).
    // Grosor 4px: en e-paper los trazos de 1px se lavan y quedan tenues.
    static void drawKrakenBrow(uint8_t *buf, int cxL = 58, int cxR = 142, int cy = 62) {
        drawLine(buf, cxL - 16, cy - 4, cxL + 16, cy + 5, 0, 4);
        drawLine(buf, cxR - 16, cy + 5, cxR + 16, cy - 4, 0, 4);
    }

    // 1d. Pack Kraken: ojo fiero entrecerrado. Parpado superior angular (sigue la ceja),
    // ojo solido oscuro, destello de ira en rendija blanca que SE MUEVE con la mirada
    // (pupilOffsetX/Y) y parpadea (blink). Cerrado = linea severa recta, sin curva dulce.
    static void drawKrakenEye(uint8_t *buf, int cx, int cy, int pupilOffsetX, int pupilOffsetY,
                              EyeBlinkState blink, bool isLeft) {
        if (blink == BLINK_CLOSED) {
            drawLine(buf, cx - 20, cy, cx + 20, cy, 0, 3);
            return;
        }
        int rx = 16, ry = 10;
        if (blink == BLINK_HALF)   ry = 6;
        if (blink == BLINK_ALMOST) ry = 2;
        // Parpado superior angular: alto afuera, bajo adentro (enojo)
        int s = isLeft ? 1 : -1;
        drawLine(buf, cx - s * 19, cy - ry - 6, cx + s * 19, cy - ry + 2, 0, 3);
        // Ojo solido oscuro (fiero)
        for (int y = -ry; y <= ry; y++) {
            for (int x = -rx; x <= rx; x++) {
                float dist = (float)(x * x) / (rx * rx) + (float)(y * y) / (ry * ry);
                if (dist <= 1.0f) drawPixel(buf, cx + x, cy + y, 0);
            }
        }
        if (blink >= BLINK_ALMOST) return;
        // Destello de ira: rendija blanca que sigue la mirada
        int gx = constrain(pupilOffsetX, -6, 6);
        int gy = constrain(pupilOffsetY, -3, 3);
        drawLine(buf, cx - 9 + gx, cy - 2 + gy, cx + 1 + gx, cy - 2 + gy, 1, 3);
        drawLine(buf, cx + 5 + gx, cy + 3 + gy, cx + 9 + gx, cy + 3 + gy, 1, 2);
        // Parpado inferior recto (firme, sin curva)
        drawLine(buf, cx - 12, cy + ry + 3, cx + 12, cy + ry + 3, 0, 2);
    }

    // 1f. Pack Kraken: MANTO de pulpo (domo bulboso; los ojos cuelgan DEBAJO del
    // faldon como en los pulpos de dibujos). Apice y~56, faldon en y=112.
    static void drawKrakenMantle(uint8_t *buf) {
        drawBezierCurve(buf, 44, 112, 100, 0, 156, 112, 0, 4);
        drawBezierCurve(buf, 50, 112, 100, 12, 150, 112, 0, 2);
        // Manchas del manto (textura de pulpo, puntos macizos que no se lavan)
        drawCircle(buf, 100, 60, 2, 0, true);
        drawCircle(buf, 86, 62, 2, 0, true);
        drawCircle(buf, 114, 62, 2, 0, true);
        // Aletas del manto (separadas del domo y de las cejas, trazo grueso)
        drawLine(buf, 58, 74, 42, 60, 0, 3);
        drawLine(buf, 42, 60, 48, 84, 0, 3);
        drawLine(buf, 142, 74, 158, 60, 0, 3);
        drawLine(buf, 158, 60, 152, 84, 0, 3);
    }

    // 1g. Pack Kraken: TENTACULO con ventosas. Curva negra gruesa + puntos blancos
    // (ventosas) al centro. sway = balanceo de animacion (+/- px en el punto de control).
    static void drawKrakenTentacle(uint8_t *buf, int x0, int y0, int cx, int cy,
                                   int x1, int y1, int sway) {
        drawBezierCurve(buf, x0, y0, cx + sway, cy, x1, y1, 0, 6);
        // Ventosas: 3 puntos blancos sobre el lomo del tentaculo (t=0.3/0.55/0.8)
        const float ts[] = { 0.3f, 0.55f, 0.8f };
        for (int i = 0; i < 3; i++) {
            float t = ts[i], it = 1.0f - t;
            int px = (int)(it * it * x0 + 2.0f * it * t * (cx + sway) + t * t * x1);
            int py = (int)(it * it * y0 + 2.0f * it * t * cy + t * t * y1);
            drawCircle(buf, px, py, 2, 1, true);
        }
    }

    // 1h. Pack Kraken: 6 tentaculos curvos (3 por lado) colgando del manto.
    // frame = animacion (balanceo alternado). Negro macizo 6px: nada tenue.
    static void drawKrakenTentacles(uint8_t *buf, int frame) {
        int s = (frame % 2 == 0) ? 3 : -3;
        // Izquierda: exterior en C + medio + interior bajo el ojo
        drawKrakenTentacle(buf, 42, 100, 28, 122, 32, 150, s);
        drawKrakenTentacle(buf, 52, 108, 40, 130, 46, 152, -s);
        drawKrakenTentacle(buf, 62, 124, 56, 136, 60, 150, s);
        // Derecha espejado
        drawKrakenTentacle(buf, 158, 100, 172, 122, 168, 150, -s);
        drawKrakenTentacle(buf, 148, 108, 160, 130, 154, 152, s);
        drawKrakenTentacle(buf, 138, 124, 144, 136, 140, 150, -s);
    }

    // 1i. Pack Kraken: PICO en reposo (V severa hacia abajo, ni sonrisa ni mofa).
    // Para hablar se reutilizan los fonemas de drawMouth (lip-sync intacto).
    static void drawKrakenBeak(uint8_t *buf, int cx = 100, int cy = 136) {
        drawLine(buf, cx - 12, cy - 4, cx, cy + 3, 0, 3);
        drawLine(buf, cx + 12, cy - 4, cx, cy + 3, 0, 3);
        drawLine(buf, cx - 12, cy - 5, cx + 12, cy - 5, 0, 2);
    }

    // 1j. Pack Kraken: cara completa de PULPO (manto + cejas V + ojos fieros con
    // mirada y parpadeo + tentaculos + pico/fonema). Una sola llamada por frame.
    static void drawKrakenFace(uint8_t *buf, int eyeY, int pupilX, int pupilY,
                               EyeBlinkState blinkL, EyeBlinkState blinkR,
                               int mouthCy, bool sternMouth, MouthPhoneme phoneme,
                               int frame) {
        drawKrakenMantle(buf);
        drawKrakenBrow(buf, 66, 134, 82);
        drawKrakenEye(buf, 66, eyeY, pupilX, pupilY, blinkL, true);
        drawKrakenEye(buf, 134, eyeY, pupilX, pupilY, blinkR, false);
        drawKrakenTentacles(buf, frame);
        if (sternMouth) drawKrakenBeak(buf, 100, mouthCy);
        else drawMouth(buf, 100, mouthCy, phoneme);
    }

    // 2. Pack Rudo: Robust Cigar (Habano) & Floating Smoke Rings
    static void drawCigarAndSmoke(uint8_t *buf, int mouthX = 100, int mouthY = 136, int iterFrame = 0) {
        int x0 = mouthX + 16;
        int y0 = mouthY - 2;
        int x1 = mouthX + 62;
        int y1 = mouthY + 6;

        // 1. Thick cigar body: 9 pixels thick
        for (int off = -4; off <= 4; off++) {
            int ox = (off * 2) / 8;
            drawLine(buf, x0 + ox, y0 + off, x1 + ox, y1 + off, 0, 1);
        }

        // 2. White cigar band / label around x = mouthX + 38..mouthX + 44
        for (int off = -3; off <= 3; off++) {
            int ox = (off * 2) / 8;
            drawLine(buf, mouthX + 38 + ox, mouthY + 2 + off, mouthX + 44 + ox, mouthY + 3 + off, 1, 1);
        }
        // Band dark borders & insignia dot
        drawLine(buf, mouthX + 37, mouthY - 2, mouthX + 39, mouthY + 6, 0, 1);
        drawLine(buf, mouthX + 45, mouthY - 1, mouthX + 47, mouthY + 7, 0, 1);
        drawPixel(buf, mouthX + 41, mouthY + 2, 0);

        // 3. Flat burning ash tip at (x1, y1)
        drawLine(buf, x1 + 1, y1 - 4, x1 + 1, y1 + 5, 0, 2);
        // Bright glowing ember core
        drawPixel(buf, x1 - 1, y1, 1);
        drawPixel(buf, x1 - 2, y1, 1);
        drawPixel(buf, x1 - 2, y1 + 1, 1);

        // 4. Humo real: cintas serpenteantes que se expanden y se disipan al subir.
        // El humo no son aros: abajo cinta densa, en medio gris traslúcido, arriba se deshace.
        for (int col = 0; col < 2; col++) {
            int baseX = 176 + col * 8;
            int phase = iterFrame * 2 + col * 5;
            for (int y = 128; y >= 30; y--) {
                int rise = 128 - y;              // 0 abajo .. 98 arriba
                int amp = 1 + rise / 22;         // se abre al subir: 1..5
                int t = (y * 2 + phase * 3) % 24;
                if (t < 0) t += 24;
                int sway = (t < 12) ? (t - 6) * amp / 6 : (18 - t) * amp / 6;
                int x = baseX + sway;
                if (rise > 70) {
                    // Arriba: disipado (puntos salteados)
                    if (((x + y + col) & 1) == 0) drawPixel(buf, x, y, 0);
                    if (((x * 3 + y + phase) % 7) == 0) drawPixel(buf, x + 1, y, 0);
                } else if (rise > 40) {
                    // Medio: traslúcido con tramado Bayer
                    drawDitherPixel(buf, x, y, 5);
                    drawDitherPixel(buf, x + 1, y, 8);
                } else {
                    // Abajo: cinta densa pegada a la brasa
                    drawPixel(buf, x, y, 0);
                    if ((y & 1) == 0) drawPixel(buf, x + 1, y, 0);
                }
            }
        }
    }

    // 3. Pack Femenino: Stylized eyelashes & Cyber Bow (Moño)
    static void drawFeminineAccents(uint8_t *buf) {
        // Left eye eyelashes (outer upper flick)
        drawLine(buf, 44, 76, 32, 68, 0, 2);
        drawLine(buf, 32, 68, 26, 64, 0, 1);
        drawLine(buf, 42, 82, 32, 79, 0, 2);

        // Right eye eyelashes (outer upper flick)
        drawLine(buf, 156, 76, 168, 68, 0, 2);
        drawLine(buf, 168, 68, 174, 64, 0, 1);
        drawLine(buf, 158, 82, 168, 79, 0, 2);

        // Cyber Bow / Moño at upper left head (x = 36, y = 50)
        // Left wing
        for (int y = -8; y <= 8; y++) {
            int span = (8 - abs(y)) * 12 / 8;
            drawLine(buf, 36 - span, 50 + y, 36, 50 + y, 0, 1);
        }
        // Right wing
        for (int y = -8; y <= 8; y++) {
            int span = (8 - abs(y)) * 12 / 8;
            drawLine(buf, 36, 50 + y, 36 + span, 50 + y, 0, 1);
        }
        // Central knot
        drawCircle(buf, 36, 50, 4, 0, true);
        drawPixel(buf, 35, 49, 1);
        // Ribbon tails hanging down
        drawLine(buf, 34, 54, 28, 66, 0, 2);
        drawLine(buf, 38, 54, 44, 66, 0, 2);
    }

    // 4. Pack Geek: 8-Bit "Deal With It" Pixel Sunglasses
    static void drawGeekSunglasses(uint8_t *buf) {
        // Left lens (x: 40..86, y: 78..96)
        drawRect(buf, 40, 78, 46, 18, 0, true);
        // Right lens (x: 114..160, y: 78..96)
        drawRect(buf, 114, 78, 46, 18, 0, true);
        // Bridge across nose (x: 86..114, y: 80..85)
        drawRect(buf, 86, 80, 28, 6, 0, true);

        // Stepped pixel edges on outer sides
        drawRect(buf, 36, 80, 4, 14, 0, true);
        drawRect(buf, 160, 80, 4, 14, 0, true);

        // Iconic 8-bit diagonal white specular reflection glints
        for (int s = 0; s < 11; s++) {
            drawPixel(buf, 48 + s, 81 + s, 1);
            drawPixel(buf, 49 + s, 81 + s, 1);
            drawPixel(buf, 122 + s, 81 + s, 1);
            drawPixel(buf, 123 + s, 81 + s, 1);
        }
        for (int s = 0; s < 6; s++) {
            drawPixel(buf, 64 + s, 81 + s, 1);
            drawPixel(buf, 65 + s, 81 + s, 1);
            drawPixel(buf, 138 + s, 81 + s, 1);
            drawPixel(buf, 139 + s, 81 + s, 1);
        }
    }

    // Dynamic Equalizer Bars for Listening State
    static void drawEqualizer(uint8_t *buf, int baseCy, int iterFrame) {
        int heights[] = { 10, 22, 14, 30, 24, 12, 28, 16, 8 };
        int count = 9;
        int barW = 4;
        int spacing = 8;
        int startX = 100 - (count * spacing) / 2;

        for (int i = 0; i < count; i++) {
            int h = heights[(i + iterFrame) % count];
            int bx = startX + i * spacing;
            for (int y = baseCy - h / 2; y <= baseCy + h / 2; y++) {
                drawLine(buf, bx, y, bx + barW, y, 0, 1);
            }
        }
    }

    static void drawChar5x7(uint8_t *buf, int x, int y, char c, uint8_t color, int scale = 1) {
        static const uint8_t font5x7[][5] = {
            /* ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00},
            /* '!' */ {0x00, 0x00, 0x5F, 0x00, 0x00},
            /* '"' */ {0x00, 0x07, 0x00, 0x07, 0x00},
            /* '#' */ {0x14, 0x7F, 0x14, 0x7F, 0x14},
            /* '$' */ {0x24, 0x2A, 0x7F, 0x2A, 0x12},
            /* '%' */ {0x23, 0x13, 0x08, 0x64, 0x62},
            /* '&' */ {0x36, 0x49, 0x55, 0x22, 0x50},
            /* '\''*/ {0x00, 0x05, 0x03, 0x00, 0x00},
            /* '(' */ {0x00, 0x1C, 0x22, 0x41, 0x00},
            /* ')' */ {0x00, 0x41, 0x22, 0x1C, 0x00},
            /* '*' */ {0x14, 0x08, 0x3E, 0x08, 0x14},
            /* '+' */ {0x08, 0x08, 0x3E, 0x08, 0x08},
            /* ',' */ {0x00, 0x50, 0x30, 0x00, 0x00},
            /* '-' */ {0x08, 0x08, 0x08, 0x08, 0x08},
            /* '.' */ {0x00, 0x60, 0x60, 0x00, 0x00},
            /* '/' */ {0x20, 0x10, 0x08, 0x04, 0x02},
            /* '0' */ {0x3E, 0x51, 0x49, 0x45, 0x3E},
            /* '1' */ {0x00, 0x42, 0x7F, 0x40, 0x00},
            /* '2' */ {0x42, 0x61, 0x51, 0x49, 0x46},
            /* '3' */ {0x21, 0x41, 0x45, 0x4B, 0x31},
            /* '4' */ {0x18, 0x14, 0x12, 0x7F, 0x10},
            /* '5' */ {0x27, 0x45, 0x45, 0x45, 0x39},
            /* '6' */ {0x3C, 0x4A, 0x49, 0x49, 0x30},
            /* '7' */ {0x01, 0x71, 0x09, 0x05, 0x03},
            /* '8' */ {0x36, 0x49, 0x49, 0x49, 0x36},
            /* '9' */ {0x06, 0x49, 0x49, 0x29, 0x1E},
            /* ':' */ {0x00, 0x36, 0x36, 0x00, 0x00},
            /* ';' */ {0x00, 0x56, 0x36, 0x00, 0x00},
            /* '<' */ {0x08, 0x14, 0x22, 0x41, 0x00},
            /* '=' */ {0x14, 0x14, 0x14, 0x14, 0x14},
            /* '>' */ {0x00, 0x41, 0x22, 0x14, 0x08},
            /* '?' */ {0x02, 0x01, 0x51, 0x09, 0x06},
            /* '@' */ {0x32, 0x49, 0x79, 0x41, 0x3E},
            /* 'A' */ {0x7E, 0x11, 0x11, 0x11, 0x7E},
            /* 'B' */ {0x7F, 0x49, 0x49, 0x49, 0x36},
            /* 'C' */ {0x3E, 0x41, 0x41, 0x41, 0x22},
            /* 'D' */ {0x7F, 0x41, 0x41, 0x22, 0x1C},
            /* 'E' */ {0x7F, 0x49, 0x49, 0x49, 0x41},
            /* 'F' */ {0x7F, 0x09, 0x09, 0x09, 0x01},
            /* 'G' */ {0x3E, 0x41, 0x49, 0x49, 0x7A},
            /* 'H' */ {0x7F, 0x08, 0x08, 0x08, 0x7F},
            /* 'I' */ {0x00, 0x41, 0x7F, 0x41, 0x00},
            /* 'J' */ {0x20, 0x40, 0x41, 0x3F, 0x01},
            /* 'K' */ {0x7F, 0x08, 0x14, 0x22, 0x41},
            /* 'L' */ {0x7F, 0x40, 0x40, 0x40, 0x40},
            /* 'M' */ {0x7F, 0x02, 0x04, 0x02, 0x7F},
            /* 'N' */ {0x7F, 0x04, 0x08, 0x10, 0x7F},
            /* 'O' */ {0x3E, 0x41, 0x41, 0x41, 0x3E},
            /* 'P' */ {0x7F, 0x09, 0x09, 0x09, 0x06},
            /* 'Q' */ {0x3E, 0x41, 0x51, 0x21, 0x5E},
            /* 'R' */ {0x7F, 0x09, 0x19, 0x29, 0x46},
            /* 'S' */ {0x46, 0x49, 0x49, 0x49, 0x31},
            /* 'T' */ {0x01, 0x01, 0x7F, 0x01, 0x01},
            /* 'U' */ {0x3F, 0x40, 0x40, 0x40, 0x3F},
            /* 'V' */ {0x1F, 0x20, 0x40, 0x20, 0x1F},
            /* 'W' */ {0x3F, 0x40, 0x38, 0x40, 0x3F},
            /* 'X' */ {0x63, 0x14, 0x08, 0x14, 0x63},
            /* 'Y' */ {0x07, 0x08, 0x70, 0x08, 0x07},
            /* 'Z' */ {0x61, 0x51, 0x49, 0x45, 0x43}
        };

        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c < ' ' || c > 'Z') return;
        const uint8_t *glyph = font5x7[c - ' '];
        for (int col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (line & (1 << row)) {
                    if (scale == 2) {
                        drawPixel(buf, x + col * 2,     y + row * 2,     color);
                        drawPixel(buf, x + col * 2 + 1, y + row * 2,     color);
                        drawPixel(buf, x + col * 2,     y + row * 2 + 1, color);
                        drawPixel(buf, x + col * 2 + 1, y + row * 2 + 1, color);
                        // Bold enhancement
                        drawPixel(buf, x + col * 2 + 2, y + row * 2,     color);
                        drawPixel(buf, x + col * 2 + 2, y + row * 2 + 1, color);
        } else {
                        // Bold 1X: doble trazo horizontal (2px de espesor) -> Negro azabache nítido
                        drawPixel(buf, x + col,     y + row, color);
                        drawPixel(buf, x + col + 1, y + row, color);
                    }
                }
            }
        }
    }

    static void drawString(uint8_t *buf, int x, int y, const char *str, uint8_t color = 0, int scale = 1) {
        while (*str) {
            uint8_t b1 = (uint8_t)*str++;
            char c = ' ';
            if (b1 < 0x80) {
                c = (char)b1;
            } else if (b1 == 0xC3 && *str) {
                uint8_t b2 = (uint8_t)*str++;
                switch (b2) {
                    case 0xA1: case 0x81: c = 'A'; break; // á, Á
                    case 0xA9: case 0x89: c = 'E'; break; // é, É
                    case 0xAD: case 0x8D: c = 'I'; break; // í, Í
                    case 0xB3: case 0x93: c = 'O'; break; // ó, Ó
                    case 0xBA: case 0x9A: c = 'U'; break; // ú, Ú
                    case 0xBC: case 0x9C: c = 'U'; break; // ü, Ü
                    case 0xB1: case 0x91: c = 'N'; break; // ñ, Ñ
                    default: c = ' '; break;
                }
            } else if (b1 == 0xC2 && *str) {
                uint8_t b2 = (uint8_t)*str++;
                if (b2 == 0xBF) c = '?';
                else if (b2 == 0xA1) c = '!';
                else c = ' ';
            } else {
                c = ' ';
            }
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            drawChar5x7(buf, x, y, c, color, scale);
            x += (scale == 2) ? 12 : 6;
        }
    }

    // --- SLEEK MINIMAL STATUS WIDGETS (TOP BAR) ---
    static void drawTopBar(uint8_t *buf, bool isCharging = false, bool isUsbPc = false) {
        // 1. WiFi Signal Indicator (Top Left: x = 10..24, y = 8..18)
        int wifiLevel = 0;
        if (WiFi.status() == WL_CONNECTED) {
            int rssi = WiFi.RSSI();
            if (rssi >= -60) wifiLevel = 4;
            else if (rssi >= -70) wifiLevel = 3;
            else if (rssi >= -80) wifiLevel = 2;
            else wifiLevel = 1;
        }

        int wx = 10, wy = 18;
        // 4 Sleek vertical signal bars
        drawLine(buf, wx, wy - 3, wx, wy, 0, 2);
        if (wifiLevel >= 2) drawLine(buf, wx + 4, wy - 6, wx + 4, wy, 0, 2);
        else drawPixel(buf, wx + 4, wy, 0);
        if (wifiLevel >= 3) drawLine(buf, wx + 8, wy - 9, wx + 8, wy, 0, 2);
        else drawPixel(buf, wx + 8, wy, 0);
        if (wifiLevel >= 4) drawLine(buf, wx + 12, wy - 12, wx + 12, wy, 0, 2);
        else drawPixel(buf, wx + 12, wy, 0);

        // WiFi offline "X" mark over bars when disconnected
        if (WiFi.status() != WL_CONNECTED) {
            drawLine(buf, wx + 16, wy - 14, wx + 22, wy - 8, 0, 1);
            drawLine(buf, wx + 22, wy - 14, wx + 16, wy - 8, 0, 1);
        }

        // USB datos a la PC: mini-tridente (x = 35..45, y = 8..18).
        // isUsbPc lo fuerza (pantalla USB dedicada); si no, autodetecta el
        // CDC de Serial. Hueco libre entre la X de WiFi (hasta x32) y el
        // texto central (desde ~x49). Con solo cargador no aparece (ver
        // copitoUsbDataConnected): así se sabe que el cable de DATOS va bien.
        bool usbLink = isUsbPc || copitoUsbDataConnected();
        if (usbLink) {
            int ux = 40;
            drawLine(buf, ux, 9, ux, 16, 0, 2);       // mástil
            drawLine(buf, ux, 8, ux - 2, 10, 0, 2);   // flecha sup izq
            drawLine(buf, ux, 8, ux + 2, 10, 0, 2);   // flecha sup der
            drawLine(buf, ux, 16, ux - 3, 13, 0, 2);  // rama inferior izq
            drawRect(buf, ux - 5, 11, 2, 2, 0, true); // punto izq
            drawLine(buf, ux, 16, ux + 3, 13, 0, 2);  // rama inferior der
            drawRect(buf, ux + 3, 11, 2, 2, 0, true); // punto der
        }

        // Clock & Ambient Sensor Widget (Center top: x = 32..145, y = 8..18)
        struct tm tm;
        bool hasTime = getLocalTime(&tm, 10);
        char topBuf[28];
        if (hasTime && g_ambientSensorValid) {
            int intTemp = (int)round(g_ambientTempC);
            snprintf(topBuf, sizeof(topBuf), "%02d:%02d  %dC %d%%", tm.tm_hour, tm.tm_min, intTemp, (int)round(g_ambientHum));
        } else if (hasTime) {
            snprintf(topBuf, sizeof(topBuf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        } else if (g_ambientSensorValid) {
            int intTemp = (int)round(g_ambientTempC);
            snprintf(topBuf, sizeof(topBuf), "%dC %d%%", intTemp, (int)round(g_ambientHum));
        } else if (isUsbPc) {
            snprintf(topBuf, sizeof(topBuf), "PC");
        } else {
            topBuf[0] = '\0';
        }

        if (topBuf[0] != '\0') {
            int envLen = strlen(topBuf);
            int envX = (EPD_WIDTH - envLen * 6) / 2;
            drawString(buf, envX, 9, topBuf, 0, 1);
        }

        // 2. Battery Indicator (Top Right, alineado al borde derecho)
        BatteryStatus bat = batteryMonitor.getStatus();
        int batteryPercent = bat.percentage;
        if (bat.isCharging) isCharging = true;

        // Layout (de derecha a izquierda, con margen de seguridad):
        //   borde derecho pantalla: x=199, margen 6px
        //   icono batería: 14px ancho (body 12px + pip 2px) → x=[178..192]
        //   separador: 3px
        //   texto "85%": txtLen*6 px → x=[178-txtLen*6-3 .. 178]
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), isCharging ? "+%d%%" : "%d%%", batteryPercent);
        int txtLen = strlen(pctBuf);
        int iconX = 178;
        int pctX  = iconX - 3 - txtLen * 6;

        // Texto porcentaje (negrita 1px)
        drawString(buf, pctX, 10, pctBuf, 0, 1);

        // Mini-icono de batería HD escalonado con rayo si carga
        drawBatteryIcon(buf, iconX, 9, batteryPercent, isCharging);
    }

    // ==========================================================================
    // COMPLETE HIGH-QUALITY MULTI-FRAME EXPRESSIONS (CLEAN & INDUSTRIAL)
    // ==========================================================================

    // 1. IDLE: Cyber Companion with breathing, glance & bold title
    static void renderIdleFrame(uint8_t *buf, int iterFrame = 0) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        uint8_t pack = getFacePack();

        int pupilX = (iterFrame % 4 == 1) ? -4 : ((iterFrame % 4 == 3) ? 4 : 0);
        int pupilY = 0;

        EyeBlinkState blinkL = BLINK_OPEN;
        EyeBlinkState blinkR = BLINK_OPEN;
        if (iterFrame == 5) {
            blinkL = BLINK_HALF;
            blinkR = BLINK_HALF;
        } else if (iterFrame == 6) {
            blinkL = BLINK_CLOSED;
            blinkR = BLINK_CLOSED;
        }

        if (pack == FACE_PACK_KRAKEN) {
            drawKrakenFace(buf, 102, pupilX, pupilY, blinkL, blinkR, 138, true, MOUTH_REST, iterFrame);

            drawLine(buf, 16, 162, 184, 162, 0, 1);
            drawString(buf, 46, 170, "KRAKEN AI", 0, 2);

        } else if (pack == FACE_PACK_RUDO) {
            drawRudoShades(buf, 88);
            drawRudoMouth(buf, 100, 138);
            drawCigarAndSmoke(buf, 100, 138, iterFrame);

            drawLine(buf, 16, 162, 184, 162, 0, 1);
            drawString(buf, 22, 170, "KRAKEN [RUDO]", 0, 2);

        } else if (pack == FACE_PACK_FEMENINO) {
            drawDetailedEye(buf, 58, 92, pupilX, pupilY, blinkL);
            drawDetailedEye(buf, 142, 92, pupilX, pupilY, blinkR);
            drawFeminineAccents(buf);
            drawCuteBlush(buf, 30, 120);
            drawCuteBlush(buf, 170, 120);
            drawMouth(buf, 100, 138, MOUTH_REST);

            drawLine(buf, 16, 162, 184, 162, 0, 1);
            drawString(buf, 10, 170, "KRAKEN [MAIDEN]", 0, 2);

        } else if (pack == FACE_PACK_GEEK) {
            drawDetailedEye(buf, 58, 92, pupilX, pupilY, blinkL);
            drawDetailedEye(buf, 142, 92, pupilX, pupilY, blinkR);
            drawGeekSunglasses(buf);
            drawMouth(buf, 100, 138, MOUTH_REST);

            drawLine(buf, 16, 162, 184, 162, 0, 1);
            drawString(buf, 22, 170, "KRAKEN [GEEK]", 0, 2);

        } else {
            // FACE_PACK_CLASICO (Default cute Copito mejorado)
            drawEyebrow(buf, 58, 66, 0);
            drawEyebrow(buf, 142, 66, 0);
            drawDetailedEye(buf, 58, 92, pupilX, pupilY, blinkL);
            drawDetailedEye(buf, 142, 92, pupilX, pupilY, blinkR);
            drawCuteBlush(buf, 28, 118);
            drawCuteBlush(buf, 172, 118);
            drawMouth(buf, 100, 138, MOUTH_REST);

            // Destellitos sutiles de alegria/magia
            drawSparkle(buf, 22, 74, 3);
            drawSparkle(buf, 178, 74, 3);

            drawLine(buf, 16, 162, 184, 162, 0, 1);
            drawString(buf, 64, 170, "COPITO", 0, 2);
        }
    }

    // 2. LISTENING: Focused alert eyes, acoustic wave ripples around ears, sweet smile & big bold 'ESCUCHANDO...'
    static void renderListeningFrame(uint8_t *buf, int iterFrame = 0) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        uint8_t pack = getFacePack();

        if (pack == FACE_PACK_KRAKEN) {
            drawKrakenFace(buf, 100, 0, 0, BLINK_OPEN, BLINK_OPEN, 136, true, MOUTH_REST, iterFrame);
        } else if (pack == FACE_PACK_RUDO) {
            drawRudoShades(buf, 86);
            drawRudoMouth(buf, 100, 134);
            drawCigarAndSmoke(buf, 100, 134, iterFrame);
        } else if (pack == FACE_PACK_FEMENINO) {
            drawDetailedEye(buf, 58, 86, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 86, 0, 0, BLINK_OPEN);
            drawFeminineAccents(buf);
            drawCuteBlush(buf, 30, 116);
            drawCuteBlush(buf, 170, 116);
            drawMouth(buf, 100, 134, MOUTH_REST);
        } else if (pack == FACE_PACK_GEEK) {
            drawDetailedEye(buf, 58, 86, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 86, 0, 0, BLINK_OPEN);
            drawGeekSunglasses(buf);
            drawMouth(buf, 100, 134, MOUTH_REST);
        } else {
            drawEyebrow(buf, 58, 60, 1);
            drawEyebrow(buf, 142, 60, 1);
            drawDetailedEye(buf, 58, 86, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 86, 0, 0, BLINK_OPEN);
            drawCuteBlush(buf, 28, 114);
            drawCuteBlush(buf, 172, 114);
            drawMouth(buf, 100, 134, MOUTH_REST);
        }

        // Dynamic acoustic sound wave ripples on both ears showing active audio capture
        int wavePhase = iterFrame % 3;
        for (int arc = 0; arc < 3; arc++) {
            int r = 16 + arc * 6;
            for (int a = -40; a <= 40; a += 8) {
                float rad = a * 0.0174533f;
                // Left sound wave
                int pxL = 34 - (int)(cos(rad) * r);
                int pyL = 86 + (int)(sin(rad) * r);
                drawPixel(buf, pxL, pyL, 0);
                if (arc == wavePhase) {
                    drawPixel(buf, pxL - 1, pyL, 0);
                    drawPixel(buf, pxL, pyL + 1, 0);
                }

                // Right sound wave
                int pxR = 166 + (int)(cos(rad) * r);
                int pyR = 86 + (int)(sin(rad) * r);
                drawPixel(buf, pxR, pyR, 0);
                if (arc == wavePhase) {
                    drawPixel(buf, pxR + 1, pyR, 0);
                    drawPixel(buf, pxR, pyR + 1, 0);
                }
            }
        }

        // Clean separator and BIG BOLD status text
        drawLine(buf, 16, 162, 184, 162, 0, 1);
        drawString(buf, 22, 170, "ESCUCHANDO...", 0, 2);
    }

    // 3. THINKING: Inquisitive brow, floating thought bubbles with sparkle, cute pursed mouth & big bold 'PENSANDO...'
    static void renderThinkingFrame(uint8_t *buf, int iterFrame = 0) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        uint8_t pack = getFacePack();

        if (pack == FACE_PACK_KRAKEN) {
            drawKrakenFace(buf, 100, 5, -5, BLINK_OPEN, BLINK_OPEN, 136, true, MOUTH_REST, iterFrame);
        } else if (pack == FACE_PACK_RUDO) {
            drawRudoShades(buf, 88, -2);
            drawRudoMouth(buf, 100, 136);
            drawCigarAndSmoke(buf, 100, 136, iterFrame);
        } else if (pack == FACE_PACK_FEMENINO) {
            drawDetailedEye(buf, 58, 88, 5, -5, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 5, -5, BLINK_OPEN);
            drawFeminineAccents(buf);
            drawLine(buf, 44, 63, 72, 65, 0, 2);
            drawLine(buf, 128, 58, 158, 64, 0, 2);
            drawCuteBlush(buf, 32, 118);
            drawCuteBlush(buf, 168, 118);
            drawMouth(buf, 100, 136, MOUTH_THINKING);
        } else if (pack == FACE_PACK_GEEK) {
            drawDetailedEye(buf, 58, 88, 5, -5, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 5, -5, BLINK_OPEN);
            drawGeekSunglasses(buf);
            drawMouth(buf, 100, 136, MOUTH_THINKING);
        } else {
            // Ceja izquierda levantada curiosa, ceja derecha concentrada
            drawEyebrow(buf, 58, 62, 2);
            drawEyebrow(buf, 142, 65, 3);
            drawDetailedEye(buf, 58, 88, 5, -5, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 5, -5, BLINK_OPEN);
            drawCuteBlush(buf, 30, 116);
            drawCuteBlush(buf, 170, 116);
            drawMouth(buf, 100, 136, MOUTH_THINKING);
        }

        // Floating thought bubbles drifting up to top-right
        drawCircle(buf, 164, 60, 4, 0, false);
        drawCircle(buf, 174, 46, 6, 0, false);
        drawCircle(buf, 185, 30, 8, 0, false);
        // Sparkle inside the largest bubble
        drawSparkle(buf, 185, 30, 4);

        // Clean separator and BIG BOLD status text
        drawLine(buf, 16, 162, 184, 162, 0, 1);
        drawString(buf, 34, 170, "PENSANDO...", 0, 2);
    }

    // 4. SPEAKING: Full Lip-Sync phonemes + clean separator & big bold 'HABLANDO...'
    static void renderSpeakingFrame(uint8_t *buf, int phonemeIndex) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        uint8_t pack = getFacePack();

        MouthPhoneme m = (MouthPhoneme)(phonemeIndex % 4);

        if (pack == FACE_PACK_KRAKEN) {
            drawKrakenFace(buf, 100, 0, 0, BLINK_OPEN, BLINK_OPEN, 138, false, m, phonemeIndex);
        } else if (pack == FACE_PACK_RUDO) {
            drawRudoShades(buf, 88);
            drawMouth(buf, 100, 138, m);
            drawCigarAndSmoke(buf, 100, 138, phonemeIndex);
        } else if (pack == FACE_PACK_FEMENINO) {
            drawDetailedEye(buf, 58, 88, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 0, 0, BLINK_OPEN);
            drawFeminineAccents(buf);
            drawCuteBlush(buf, 32, 118);
            drawCuteBlush(buf, 168, 118);
            drawMouth(buf, 100, 138, m);
        } else if (pack == FACE_PACK_GEEK) {
            drawDetailedEye(buf, 58, 88, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 0, 0, BLINK_OPEN);
            drawGeekSunglasses(buf);
            drawMouth(buf, 100, 138, m);
        } else {
            drawEyebrow(buf, 58, 64, 0);
            drawEyebrow(buf, 142, 64, 0);
            drawDetailedEye(buf, 58, 88, 0, 0, BLINK_OPEN);
            drawDetailedEye(buf, 142, 88, 0, 0, BLINK_OPEN);
            drawCuteBlush(buf, 30, 116);
            drawCuteBlush(buf, 170, 116);
            drawMouth(buf, 100, 138, m);
            if (phonemeIndex % 2 == 0) {
                drawSparkle(buf, 22, 88, 2);
                drawSparkle(buf, 178, 88, 2);
            }
        }

        drawLine(buf, 16, 162, 184, 162, 0, 1);
        drawString(buf, 34, 170, "HABLANDO...", 0, 2);
    }

    // 5. SLEEP (REPOSO): Closed peaceful eyes (-.-) + Zzz symbols + sleep mode instructions
    static void renderSleepFrame(uint8_t *buf) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        // Left closed sleeping eye (sweet arc with eyelashes)
        for (int x = -20; x <= 20; x++) {
            float norm = (float)x / 20.0f;
            int y = 92 + (int)((1.0f - norm * norm) * 8.0f);
            int thick = (abs(x) < 12) ? 3 : 2;
            for (int t = 0; t < thick; t++) {
                drawPixel(buf, 60 + x, y + t, 0);
                drawPixel(buf, 140 + x, y + t, 0);
            }
        }
        // Cute delicate eyelashes
        drawLine(buf, 48, 98, 44, 104, 0, 2);
        drawLine(buf, 60, 101, 60, 107, 0, 2);
        drawLine(buf, 72, 98, 76, 104, 0, 2);

        drawLine(buf, 128, 98, 124, 104, 0, 2);
        drawLine(buf, 140, 101, 140, 107, 0, 2);
        drawLine(buf, 152, 98, 156, 104, 0, 2);

        // Cheeks
        drawCuteBlush(buf, 28, 120);
        drawCuteBlush(buf, 172, 120);

        // Gentle smiling rest mouth
        drawMouth(buf, 100, 138, MOUTH_REST);

        // Zzz Floating up to the right with increasing scale
        drawString(buf, 152, 50, "z", 0, 1);
        drawString(buf, 164, 38, "z", 0, 1);
        drawString(buf, 176, 24, "Z", 0, 2);

        drawLine(buf, 20, 162, 180, 162, 0, 1);
        drawString(buf, 46, 168, "EN REPOSO (Zzz)", 0, 1);
        drawString(buf, 30, 180, "Pulsa BOOT para hablar", 0, 1);
    }

    // 6. USB PC MODE: Connected to Computer
    static void renderUsbPcFrame(uint8_t *buf) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf, false, true);

        // Happy crescent eyes
        for (int x = -18; x <= 18; x++) {
            float norm = (float)x / 18.0f;
            int y = 84 - (int)((1.0f - norm * norm) * 8.0f);
            drawPixel(buf, 60 + x, y, 0);
            drawPixel(buf, 60 + x, y + 1, 0);
            drawPixel(buf, 140 + x, y, 0);
            drawPixel(buf, 140 + x, y + 1, 0);
        }

        // Cute Laptop / Computer icon at center
        drawLine(buf, 80, 104, 120, 104, 0, 2);
        drawLine(buf, 80, 122, 120, 122, 0, 2);
        drawLine(buf, 80, 104, 80, 122, 0, 2);
        drawLine(buf, 120, 104, 120, 122, 0, 2);
        // Base of laptop
        drawLine(buf, 72, 124, 128, 124, 0, 3);

        // Smile
        drawMouth(buf, 100, 144, MOUTH_REST);

        drawLine(buf, 20, 166, 180, 166, 0, 1);
        drawString(buf, 44, 174, "CONECTADO A PC (USB)", 0);
    }

    // 7. CHARGING MODE: Wall Charger / Power only
    static void renderChargingFrame(uint8_t *buf) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf, true, false);

        // Large Battery Capsule at center (x = 76..124, y = 80..116)
        drawLine(buf, 80, 80, 120, 80, 0, 2);
        drawLine(buf, 80, 116, 120, 116, 0, 2);
        drawLine(buf, 78, 82, 78, 114, 0, 2);
        drawLine(buf, 122, 82, 122, 114, 0, 2);
        // Positive terminal (+) pip
        drawLine(buf, 124, 92, 124, 104, 0, 4);

        // Big Lightning Bolt inside
        drawLine(buf, 104, 84, 96, 98, 0, 3);
        drawLine(buf, 96, 98, 106, 98, 0, 3);
        drawLine(buf, 106, 98, 98, 112, 0, 3);

        // Happy smile mouth
        drawMouth(buf, 100, 142, MOUTH_REST);

        drawLine(buf, 20, 166, 180, 166, 0, 1);
        drawString(buf, 48, 174, "CARGANDO BATERIA", 0);
    }

    // 8. HAPPY / JOY: Cute anime smiling arched eyes (^ . ^) with blush cheeks
    static void renderHappyFrame(uint8_t *buf) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        uint8_t pack = getFacePack();

        // Cejas altas y felices
        drawEyebrow(buf, 58, 64, 1);
        drawEyebrow(buf, 142, 64, 1);

        // Arched joyful eyes: left eye at (58, 92), right eye at (142, 92)
        for (int x = -19; x <= 19; x++) {
            float norm = (float)x / 19.0f;
            int arcY = (int)((1.0f - (norm * norm)) * 10.0f);
            int thick = (abs(x) < 13) ? 4 : 2;
            for (int t = 0; t < thick; t++) {
                drawPixel(buf, 58 + x, 96 - arcY + t, 0);
                drawPixel(buf, 142 + x, 96 - arcY + t, 0);
            }
        }
        // Pestañitas alegres en los bordes exteriores
        drawLine(buf, 38, 96, 33, 93, 0, 2);
        drawLine(buf, 162, 96, 167, 93, 0, 2);

        // Cute blush marks on cheeks
        drawCuteBlush(buf, 28, 116);
        drawCuteBlush(buf, 172, 116);

        // Destellos de alegria
        drawSparkle(buf, 20, 70, 4);
        drawSparkle(buf, 180, 70, 4);

        // Big joyful smile mouth
        drawMouth(buf, 100, 136, MOUTH_A);

        if (pack == FACE_PACK_KRAKEN) {
            drawKrakenFace(buf, 102, 0, 0, BLINK_OPEN, BLINK_OPEN, 136, false, MOUTH_A, 0);
            drawLine(buf, 20, 166, 180, 166, 0, 1);
            drawString(buf, 70, 174, "SOY KRAKEN", 0);
        } else if (pack == FACE_PACK_RUDO) {
            drawRudoShades(buf, 88);
            drawMouth(buf, 100, 136, MOUTH_A);
            drawCigarAndSmoke(buf, 100, 136, 0);
            drawLine(buf, 20, 166, 180, 166, 0, 1);
            drawString(buf, 22, 174, "KRAKEN [RUDO]", 0);
        } else if (pack == FACE_PACK_FEMENINO) {
            drawFeminineAccents(buf);
            drawLine(buf, 20, 166, 180, 166, 0, 1);
            drawString(buf, 10, 174, "KRAKEN [MAIDEN]", 0);
        } else if (pack == FACE_PACK_GEEK) {
            drawGeekSunglasses(buf);
            drawLine(buf, 20, 166, 180, 166, 0, 1);
            drawString(buf, 22, 174, "KRAKEN [GEEK]", 0);
        } else {
            drawLine(buf, 20, 164, 180, 164, 0, 1);
            drawString(buf, 44, 172, "HOLA! SOY COPITO", 0, 1);
        }
    }

    // 9. ERROR: Anime dizzy spiral eyes with clear status text
    static void renderErrorFrame(uint8_t *buf, const char *line1 = "ERROR DE IA", const char *line2 = "Intente de nuevo") {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        for (int t = 0; t <= 720; t += 5) {
            float r = (float)t / 720.0f * 18.0f;
            float rad = t * 3.14159f / 180.0f;
            drawPixel(buf, 58 + (int)(cos(rad) * r), 90 + (int)(sin(rad) * r), 0);
            drawPixel(buf, 58 + (int)(cos(rad) * r) + 1, 90 + (int)(sin(rad) * r), 0);
            drawPixel(buf, 142 + (int)(cos(rad) * r), 90 + (int)(sin(rad) * r), 0);
            drawPixel(buf, 142 + (int)(cos(rad) * r) + 1, 90 + (int)(sin(rad) * r), 0);
        }

        drawCircle(buf, 168, 56, 6, 0, false);
        drawLine(buf, 168, 44, 163, 54, 0, 1);
        drawLine(buf, 168, 44, 173, 54, 0, 1);

        for (int x = -20; x <= 20; x++) {
            float norm = (float)x / 20.0f;
            int y = 140 - (int)((norm * norm) * 7.0f);
            drawPixel(buf, 100 + x, y, 0);
            drawPixel(buf, 100 + x, y + 1, 0);
            drawPixel(buf, 100 + x, y + 2, 0);
        }

        drawLine(buf, 20, 154, 180, 154, 0, 1);

        if (line1 && strlen(line1) > 0) {
            int len1 = strlen(line1);
            int cx1 = 100 - (len1 * 6) / 2;
            if (cx1 < 4) cx1 = 4;
            drawString(buf, cx1, 158, line1, 0);
        }
        if (line2 && strlen(line2) > 0) {
            int len2 = strlen(line2);
            int cx2 = 100 - (len2 * 6) / 2;
            if (cx2 < 4) cx2 = 4;
            drawString(buf, cx2, 168, line2, 0);
        }
    }

    // 10. PORTAL: WiFi hotspot beacon
    static void renderPortalFrame(uint8_t *buf, int iterFrame = 0) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        int activeRing = (iterFrame % 3);
        int ringIdx = 0;
        for (int r = 12; r <= 38; r += 13) {
            int thick = (ringIdx == activeRing) ? 2 : 1;
            for (int a = 30; a <= 150; a++) {
                float rad = a * 3.14159f / 180.0f;
                for (int t = 0; t < thick; t++) {
                    int px = 100 + (int)(cos(rad) * (r + t));
                    int py = 72 - (int)(sin(rad) * (r + t));
                    drawPixel(buf, px, py, 0);
                }
            }
            ringIdx++;
        }
        drawCircle(buf, 100, 74, 4, 0, true);

        drawDetailedEye(buf, 62, 115, 0, 0, BLINK_OPEN);
        drawDetailedEye(buf, 138, 115, 0, 0, BLINK_OPEN);
        drawMouth(buf, 100, 142, MOUTH_REST);

        drawLine(buf, 20, 154, 180, 154, 0, 1);
        drawString(buf, 14, 158, "MODO CONFIG WIFI", 0);
        drawString(buf, 10, 168, "KRAKEN-CONFIG en Wi-Fi", 0);
    }

    // 11. WIFI_CONNECTING: Loading state
    static void renderConnectingFrame(uint8_t *buf, int iterFrame = 0) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        for (int d = 0; d < 8; d++) {
            float angle = d * 45.0f * 3.14159f / 180.0f;
            int cx = 100 + (int)(cos(angle) * 30);
            int cy = 90 + (int)(sin(angle) * 30);
            int active = (iterFrame + d) % 8;
            if (active < 4) {
                drawCircle(buf, cx, cy, 4, 0, true);
            } else {
                drawCircle(buf, cx, cy, 3, 0, false);
            }
        }
        drawCircle(buf, 100, 90, 5, 0, true);

        drawLine(buf, 20, 154, 180, 154, 0, 1);
        drawString(buf, 32, 158, "CONECTANDO WiFi", 0);
        const char *dots[] = {".", "..", "...", ""};
        drawString(buf, 86, 168, dots[iterFrame % 4], 0);
    }

    // ==========================================================================
    // DRAWINGS & FALLBACK TEXT SPEECH BUBBLE (FOR AGENT TOOLS & AUDIO FALLBACK)
    // ==========================================================================

    // Sweet lovely Heart
    static void drawHeart(uint8_t *buf, int cx, int cy) {
        // Heart lobes
        drawCircle(buf, cx - 9, cy - 6, 9, 0, true);
        drawCircle(buf, cx + 9, cy - 6, 9, 0, true);
        // Triangle base
        for (int y = 0; y <= 16; y++) {
            int span = (16 - y);
            drawLine(buf, cx - span, cy - 2 + y, cx + span, cy - 2 + y, 0, 1);
        }
    }

    // Five-pointed Star
    static void drawStar(uint8_t *buf, int cx, int cy) {
        for (int a = 0; a < 5; a++) {
            float a1 = (a * 72 - 90) * 3.14159f / 180.0f;
            float a2 = (a * 72 + 36 - 90) * 3.14159f / 180.0f;
            int x1 = cx + (int)(cos(a1) * 16);
            int y1 = cy + (int)(sin(a1) * 16);
            int x2 = cx + (int)(cos(a2) * 8);
            int y2 = cy + (int)(sin(a2) * 8);
            drawLine(buf, cx, cy, x1, y1, 0, 2);
            drawLine(buf, cx, cy, x2, y2, 0, 2);
        }
        drawCircle(buf, cx, cy, 5, 0, true);
    }

    // Radiant Sun
    static void drawSun(uint8_t *buf, int cx, int cy) {
        drawCircle(buf, cx, cy, 10, 0, true);
        for (int i = 0; i < 8; i++) {
            float rad = (i * 45) * 3.14159f / 180.0f;
            int x1 = cx + (int)(cos(rad) * 13);
            int y1 = cy + (int)(sin(rad) * 13);
            int x2 = cx + (int)(cos(rad) * 19);
            int y2 = cy + (int)(sin(rad) * 19);
            drawLine(buf, x1, y1, x2, y2, 0, 2);
        }
    }

    // Fluffy Cloud with Raindrops
    static void drawCloud(uint8_t *buf, int cx, int cy) {
        // 3 overlapping puffs
        drawCircle(buf, cx - 12, cy, 8, 0, true);
        drawCircle(buf, cx, cy - 4, 10, 0, true);
        drawCircle(buf, cx + 12, cy, 8, 0, true);
        drawRect(buf, cx - 12, cy, 24, 7, 0, true);
        // Raindrops falling underneath
        drawLine(buf, cx - 10, cy + 10, cx - 12, cy + 15, 0, 2);
        drawLine(buf, cx,      cy + 11, cx - 2,  cy + 16, 0, 2);
        drawLine(buf, cx + 10, cy + 10, cx + 8,  cy + 15, 0, 2);
    }

    // Cozy Little House
    static void drawHome(uint8_t *buf, int cx, int cy) {
        // Triangular roof
        for (int y = 0; y <= 10; y++) {
            int span = y * 16 / 10;
            drawLine(buf, cx - span, cy - 8 + y, cx + span, cy - 8 + y, 0, 1);
        }
        // Chimney
        drawRect(buf, cx + 8, cy - 8, 4, 6, 0, true);
        // Walls
        drawRect(buf, cx - 14, cy + 3, 28, 14, 0, false);
        // Door
        drawRect(buf, cx - 4, cy + 8, 8, 9, 0, true);
    }

    // Musical Notes
    static void drawMusic(uint8_t *buf, int cx, int cy) {
        // Two note heads
        drawCircle(buf, cx - 10, cy + 6, 4, 0, true);
        drawCircle(buf, cx + 8, cy + 3, 4, 0, true);
        // Stems
        drawLine(buf, cx - 6, cy + 6, cx - 6, cy - 8, 0, 2);
        drawLine(buf, cx + 12, cy + 3, cx + 12, cy - 11, 0, 2);
        // Connecting double beam
        drawLine(buf, cx - 6, cy - 8, cx + 12, cy - 11, 0, 3);
        drawLine(buf, cx - 6, cy - 4, cx + 12, cy - 7, 0, 2);
    }

    // Crescent Moon (LUNA)
    static void drawMoon(uint8_t *buf, int cx, int cy) {
        drawCircle(buf, cx, cy, 15, 0, true);
        drawCircle(buf, cx + 6, cy - 3, 12, 1, true);
        // Little stars around
        drawPixel(buf, cx + 14, cy - 12, 0); drawPixel(buf, cx + 15, cy - 12, 0);
        drawPixel(buf, cx + 18, cy + 2, 0); drawPixel(buf, cx + 18, cy + 3, 0);
    }

    // Open Book (LIBRO)
    static void drawBook(uint8_t *buf, int cx, int cy) {
        drawRect(buf, cx - 16, cy - 10, 32, 20, 0, false);
        drawLine(buf, cx, cy - 10, cx, cy + 10, 0, 1);
        drawLine(buf, cx - 12, cy - 5, cx - 4, cy - 5, 0, 1);
        drawLine(buf, cx - 12, cy - 1, cx - 4, cy - 1, 0, 1);
        drawLine(buf, cx + 4, cy - 5, cx + 12, cy - 5, 0, 1);
        drawLine(buf, cx + 4, cy - 1, cx + 12, cy - 1, 0, 1);
    }

    // Simple Flower (FLOR)
    static void drawFlower(uint8_t *buf, int cx, int cy) {
        drawCircle(buf, cx, cy - 10, 5, 0, true);
        drawCircle(buf, cx, cy + 10, 5, 0, true);
        drawCircle(buf, cx - 10, cy, 5, 0, true);
        drawCircle(buf, cx + 10, cy, 5, 0, true);
        drawCircle(buf, cx, cy, 5, 0, true);
        drawCircle(buf, cx, cy, 2, 1, true);
        drawLine(buf, cx, cy + 14, cx, cy + 22, 0, 2);
    }

    // Lightning Bolt (RAYO)
    static void drawBolt(uint8_t *buf, int cx, int cy) {
        drawLine(buf, cx + 4, cy - 16, cx - 4, cy + 0, 0, 3);
        drawLine(buf, cx - 4, cy + 0, cx + 4, cy + 0, 0, 3);
        drawLine(buf, cx + 4, cy + 0, cx - 4, cy + 16, 0, 3);
    }

    static void drawMiniFace(uint8_t *buf, int cx, int cy) {
        // Marco de la cabeza comun
        drawRect(buf, cx - 14, cy - 9, 28, 18, 0, false);

        if (g_currentFacePack == FACE_PACK_RUDO) {
            // Badass Cyber Shades polarizadas con reflejo y puro humeante
            drawRect(buf, cx - 10, cy - 4, 9, 6, 0, true);
            drawRect(buf, cx + 1, cy - 4, 9, 6, 0, true);
            drawRect(buf, cx - 2, cy - 3, 4, 2, 0, true); // Puente nasal
            // Reflejos blancos diagonales
            drawPixel(buf, cx - 7, cy - 2, 1);
            drawPixel(buf, cx - 6, cy - 1, 1);
            drawPixel(buf, cx + 4, cy - 2, 1);
            drawPixel(buf, cx + 5, cy - 1, 1);
            // Smirk ladeado
            drawLine(buf, cx - 4, cy + 5, cx + 2, cy + 6, 0, 1);
            // Puro robusto con ceniza y humo en margen derecho
            drawRect(buf, cx + 2, cy + 5, 8, 3, 0, true);
            drawPixel(buf, cx + 11, cy + 4, 0);
            drawPixel(buf, cx + 12, cy + 2, 0);
        } else if (g_currentFacePack == FACE_PACK_GEEK) {
            // Gafas 8-bit geek
            drawRect(buf, cx - 10, cy - 4, 9, 6, 0, false);
            drawRect(buf, cx + 1, cy - 4, 9, 6, 0, false);
            drawLine(buf, cx - 2, cy - 2, cx + 1, cy - 2, 0, 1);
            drawPixel(buf, cx - 6, cy - 1, 0);
            drawPixel(buf, cx + 5, cy - 1, 0);
            drawLine(buf, cx - 4, cy + 4, cx + 4, cy + 4, 0, 1);
        } else if (g_currentFacePack == FACE_PACK_FEMENINO) {
            // Ojos con pestanias curvadas
            drawCircle(buf, cx - 6, cy - 1, 3, 0, true);
            drawCircle(buf, cx + 6, cy - 1, 3, 0, true);
            drawPixel(buf, cx - 7, cy - 2, 1); drawPixel(buf, cx + 5, cy - 2, 1);
            drawLine(buf, cx - 10, cy - 4, cx - 8, cy - 3, 0, 1);
            drawLine(buf, cx + 10, cy - 4, cx + 8, cy - 3, 0, 1);
            // Sonrisa tierna
            drawLine(buf, cx - 4, cy + 4, cx + 4, cy + 4, 0, 1);
            drawPixel(buf, cx - 5, cy + 3, 0); drawPixel(buf, cx + 5, cy + 3, 0);
        } else if (g_currentFacePack == FACE_PACK_KRAKEN) {
            // Mini-Kraken pulpo: domo + ojos fieros + boca firme + tentaculitos
            drawBezierCurve(buf, cx - 12, cy + 2, cx, cy - 14, cx + 12, cy + 2, 0, 2);
            drawLine(buf, cx - 8, cy - 4, cx - 3, cy - 2, 0, 2);
            drawLine(buf, cx + 8, cy - 4, cx + 3, cy - 2, 0, 2);
            drawPixel(buf, cx - 5, cy - 1, 0); drawPixel(buf, cx + 5, cy - 1, 0);
            drawLine(buf, cx - 3, cy + 3, cx + 3, cy + 3, 0, 2);
            drawLine(buf, cx - 6, cy + 5, cx - 6, cy + 8, 0, 2);
            drawLine(buf, cx, cy + 5, cx, cy + 8, 0, 2);
            drawLine(buf, cx + 6, cy + 5, cx + 6, cy + 8, 0, 2);
        } else {
            // Antena clasica
            drawLine(buf, cx, cy - 9, cx, cy - 14, 0, 2);
            drawPixel(buf, cx, cy - 15, 0);
            // Ojos circulares brillantes
            drawCircle(buf, cx - 6, cy - 1, 3, 0, true);
            drawPixel(buf, cx - 7, cy - 2, 1);
            drawCircle(buf, cx + 6, cy - 1, 3, 0, true);
            drawPixel(buf, cx + 5, cy - 2, 1);
            // Sonrisa
            drawLine(buf, cx - 4, cy + 4, cx + 4, cy + 4, 0, 1);
            drawPixel(buf, cx - 5, cy + 3, 0); drawPixel(buf, cx + 5, cy + 3, 0);
        }
    }

    // 12. FALLBACK / TEXT DRAW FRAME: Comic Speech Bubble with dynamic font wrap + AI Art Icon
    static void renderFallbackDrawFrame(uint8_t *buf, int drawType, const char *rawText) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);

        // Draw Icon requested by agent (0=MiniFace, 1=Heart, 2=Star, 3=Sun, 4=Cloud, 5=Home, 6=Music, 7=Moon, 8=Book, 9=Flower, 10=Bolt)
        int iconY = 44;
        if (drawType == 2) drawStar(buf, 100, iconY);
        else if (drawType == 3) drawSun(buf, 100, iconY);
        else if (drawType == 4) drawCloud(buf, 100, iconY);
        else if (drawType == 5) drawHome(buf, 100, iconY);
        else if (drawType == 6) drawMusic(buf, 100, iconY);
        else if (drawType == 7) drawMoon(buf, 100, iconY);
        else if (drawType == 8) drawBook(buf, 100, iconY);
        else if (drawType == 9) drawFlower(buf, 100, iconY);
        else if (drawType == 10) drawBolt(buf, 100, iconY);
        else if (drawType == 1) drawHeart(buf, 100, iconY);
        else drawMiniFace(buf, 100, iconY);

        // Speech bubble rectangle: x = 10..190, y = 66..188
        int bx = 10, by = 66, bw = 180, bh = 122;
        drawLine(buf, bx + 4, by, bx + bw - 4, by, 0, 2);
        drawLine(buf, bx + 4, by + bh, bx + bw - 4, by + bh, 0, 2);
        drawLine(buf, bx, by + 4, bx, by + bh - 4, 0, 2);
        drawLine(buf, bx + bw, by + 4, bx + bw, by + bh - 4, 0, 2);
        // Rounded corners
        drawPixel(buf, bx + 1, by + 1, 0); drawPixel(buf, bx + bw - 1, by + 1, 0);
        drawPixel(buf, bx + 1, by + bh - 1, 0); drawPixel(buf, bx + bw - 1, by + bh - 1, 0);

        // Bubble pointer pointing up to the icon
        drawLine(buf, 94, by, 100, by - 8, 0, 2);
        drawLine(buf, 106, by, 100, by - 8, 0, 2);
        drawLine(buf, 95, by, 105, by, 1, 3); // Erase base

        // Dynamic font scale: 2X for short phrases, 1X for rich explanations so nothing truncates!
        if (!rawText || strlen(rawText) == 0)
            rawText = (g_currentFacePack == FACE_PACK_CLASICO) ? "HOLA! SOY COPITO" : "SOY KRAKEN";
        int textLen = strlen(rawText);
        int scale = (textLen <= 65) ? 2 : 1;
        int charW = (scale == 2) ? 12 : 6;
        int lineStep = (scale == 2) ? 22 : 12;
        int maxCharsPerLine = (bw - 20) / charW; // 13 chars in 2X, 26 chars in 1X
        int maxLines = (scale == 2) ? 5 : 9;     // Up to 5 lines in 2X, up to 9 lines in 1X
        int cursorY = by + ((scale == 2) ? 10 : 8);
        int lineCount = 0;

        char word[48];
        int wIdx = 0;
        const char *p = rawText;
        int lineLen = 0;

        while (*p && lineCount < maxLines) {
            // Read one word
            wIdx = 0;
            while (*p && *p != ' ' && *p != '\n' && wIdx < 45) {
                word[wIdx++] = *p++;
            }
            word[wIdx] = '\0';
            if (*p == ' ') p++;
            else if (*p == '\n') p++;

            // If word exceeds line, break to next line
            if (lineLen + wIdx > maxCharsPerLine && lineLen > 0) {
                cursorY += lineStep;
                lineLen = 0;
                lineCount++;
                if (lineCount >= maxLines) break;
            }

            // Draw word
            drawString(buf, bx + 10 + (lineLen * charW), cursorY, word, 0, scale);
            lineLen += wIdx + 1; // +1 for space
        }
    }

    // Largo visible (sin contar el 2do byte de cada letra con tilde/enie).
    static int musicDispLen(const char *s) {
        int n = 0;
        while (s && *s) {
            uint8_t b = (uint8_t)*s++;
            if (b < 0x80 || b >= 0xC0) n++;
        }
        return n;
    }

    // --- PANTALLA DE MUSICA: titulo CENTRADO + estado abajo ---
    // La burbuja generica alineaba todo a la izquierda y cortaba los
    // titulos largos. Aca cada renglon se centra y el estado
    // ("SONANDO", "Vol 70%") va fijo abajo para no mover el titulo.
    static void renderMusicFrame(uint8_t *buf, const char *rawTitle, const char *status) {
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);
        drawMusic(buf, 100, 50);
        drawLine(buf, 14, 72, 186, 72, 0, 2);

        if (!rawTitle || !*rawTitle) rawTitle = "Musica";
        // Escala: 2X si entra en 3 renglones de 13, si no 1X (6 x 26).
        int n = musicDispLen(rawTitle);
        int scale = (n <= 39) ? 2 : 1;
        int charW = (scale == 2) ? 12 : 6;
        int maxCh = (scale == 2) ? 13 : 26;
        int maxLines = (scale == 2) ? 3 : 5;
        int lineStep = (scale == 2) ? 22 : 13;
        int y = (scale == 2) ? 84 : 80;

        // Cortar en palabras y centrar cada renglon
        char line[64];
        int li = 0, lines = 0;
        const char *p = rawTitle;
        char word[48];
        while (*p && lines < maxLines) {
            int wi = 0;
            while (*p && *p != ' ' && *p != '\n' && wi < 46) word[wi++] = *p++;
            word[wi] = '\0';
            if (*p == ' ' || *p == '\n') p++;
            int wlen = musicDispLen(word);
            int curLen = musicDispLen(line);
            if (curLen > 0 && curLen + 1 + wlen > maxCh) {
                int x = (200 - curLen * charW) / 2;
                if (x < 4) x = 4;
                drawString(buf, x, y, line, 0, scale);
                y += lineStep; lines++;
                line[0] = '\0'; li = 0; curLen = 0;
                if (lines >= maxLines) break;
            }
            if (curLen > 0) { line[li++] = ' '; }
            for (int i = 0; word[i] && li < 62; i++) line[li++] = word[i];
            line[li] = '\0';
        }
        if (line[0] && lines < maxLines) {
            int curLen = musicDispLen(line);
            int x = (200 - curLen * charW) / 2;
            if (x < 4) x = 4;
            drawString(buf, x, y, line, 0, scale);
        }
        // Estado fijo abajo, centrado
        if (!status || !*status) status = "SONANDO";
        int sl = musicDispLen(status);
        int sx = (200 - sl * 6) / 2;
        if (sx < 4) sx = 4;
        drawLine(buf, 40, 182, 160, 182, 0, 1);
        drawString(buf, sx, 186, status, 0, 1);
    }

    // Copia un logo 64x64 1-bit (512 bytes, mismo packing que el buffer).
    static void blitLogo64(uint8_t *buf, int dx, int dy, const uint8_t *logo) {
        if (!logo) return;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                uint8_t b = logo[y * 8 + (x >> 3)];
                drawPixel(buf, dx + x, dy + y, (b >> (7 - (x & 7))) & 1);
            }
        }
    }

    // --- PANTALLA DE MUSICA CON LOGO: tapa chiquita a la izquierda,
    // titulo al lado (centrado en su caja) y desborde a ancho completo.
    static void renderMusicFrameWithLogo(uint8_t *buf, const char *rawTitle,
                                        const char *status, const uint8_t *logo) {
        if (!logo) { renderMusicFrame(buf, rawTitle, status); return; }
        clearBuffer(buf, 0xFF);
        drawTopBar(buf);
        if (!rawTitle || !*rawTitle) rawTitle = "Musica";
        int n = musicDispLen(rawTitle);
        int scale = (n <= 27) ? 2 : 1;
        int charW = (scale == 2) ? 12 : 6;
        int lineStep = (scale == 2) ? 22 : 13;
        int sideMaxCh = (scale == 2) ? 9 : 19;
        int fullMaxCh = (scale == 2) ? 13 : 26;
        int sideMaxLines = (scale == 2) ? 3 : 5;
        int fullMaxLines = (scale == 2) ? 2 : 3;

        const int lx = 8, ly = 40;
        blitLogo64(buf, lx, ly, logo);
        drawRect(buf, lx - 2, ly - 2, 68, 68, 0, false);

        // Armar renglones (palabras, sin partir)
        char lines[8][64];
        int nLines = 0;
        {
            char line[64] = {0};
            const char *p = rawTitle;
            char word[48];
            int li = 0;
            while (*p && nLines < 8) {
                int wi = 0;
                while (*p && *p != ' ' && *p != '\n' && wi < 46) word[wi++] = *p++;
                word[wi] = '\0';
                if (*p == ' ' || *p == '\n') p++;
                int cap = (nLines < sideMaxLines) ? sideMaxCh : fullMaxCh;
                int wlen = musicDispLen(word);
                int curLen = musicDispLen(line);
                if (curLen > 0 && curLen + 1 + wlen > cap) {
                    strncpy(lines[nLines], line, 63);
                    lines[nLines][63] = '\0';
                    nLines++;
                    line[0] = '\0'; li = 0;
                    if (nLines >= 8) break;
                }
                if (line[0]) { line[li++] = ' '; }
                for (int i = 0; word[i] && li < 62; i++) line[li++] = word[i];
                line[li] = '\0';
            }
            if (line[0] && nLines < 8) {
                strncpy(lines[nLines], line, 63);
                lines[nLines][63] = '\0';
                nLines++;
            }
        }
        int ySide0 = (scale == 2) ? 48 : 44;
        for (int i = 0; i < nLines; i++) {
            int len = musicDispLen(lines[i]);
            int x, y;
            if (i < sideMaxLines) {
                x = 80 + (114 - len * charW) / 2; // centrado al lado del logo
                if (x < 80) x = 80;
                y = ySide0 + i * lineStep;
            } else {
                int k = i - sideMaxLines;
                if (k >= fullMaxLines) break;
                x = (200 - len * charW) / 2;
                if (x < 4) x = 4;
                y = 112 + k * lineStep;
            }
            drawString(buf, x, y, lines[i], 0, scale);
        }
        if (!status || !*status) status = "SONANDO";
        int sl = musicDispLen(status);
        int sx = (200 - sl * 6) / 2;
        if (sx < 4) sx = 4;
        drawLine(buf, 40, 182, 160, 182, 0, 1);
        drawString(buf, sx, 186, status, 0, 1);
    }

    // --- BIG DIGIT DRAWING (60px high, 30px wide, 8px solid block stroke) ---
    static void drawBigDigit(uint8_t *buf, int x, int y, int w, int h, int s, char digit) {
        if (digit == '0') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
            drawRect(buf, x, y, s, h, 0, true);                     // left
            drawRect(buf, x + w - s, y, s, h, 0, true);             // right
        } else if (digit == '1') {
            int cx = x + (w - s) / 2;
            drawRect(buf, cx, y, s, h, 0, true);                    // tronco centrado
            drawLine(buf, cx, y + 4, cx - 7, y + 14, 0, s);         // gancho solido
            drawRect(buf, x + 4, y + h - s, w - 8, s, 0, true);     // base simetrica
        } else if (digit == '2') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x + w - s, y, s, h / 2, 0, true);         // right top
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x, y + h / 2 - s / 2, s, h / 2 + s / 2, 0, true); // left bot
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
        } else if (digit == '3') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x + w - s, y, s, h, 0, true);             // right full
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
        } else if (digit == '4') {
            drawRect(buf, x, y, s, h / 2 + s / 2, 0, true);         // left top
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x + w - s, y, s, h, 0, true);             // right full
        } else if (digit == '5') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x, y, s, h / 2, 0, true);                 // left top
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x + w - s, y + h / 2 - s / 2, s, h / 2 + s / 2, 0, true); // right bot
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
        } else if (digit == '6') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x, y, s, h, 0, true);                     // left full
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x + w - s, y + h / 2 - s / 2, s, h / 2 + s / 2, 0, true); // right bot
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
        } else if (digit == '7') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawLine(buf, x + w - s / 2, y + 4, x + 6, y + h - 4, 0, s); // diagonal sólida centrada
        } else if (digit == '8') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
            drawRect(buf, x, y, s, h, 0, true);                     // left
            drawRect(buf, x + w - s, y, s, h, 0, true);             // right
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
        } else if (digit == '9') {
            drawRect(buf, x, y, w, s, 0, true);                     // top
            drawRect(buf, x, y, s, h / 2 + s / 2, 0, true);         // left top
            drawRect(buf, x + w - s, y, s, h, 0, true);             // right full
            drawRect(buf, x, y + h / 2 - s / 2, w, s, 0, true);     // mid
            drawRect(buf, x, y + h - s, w, s, 0, true);             // bot
        }
    }

    // --- MODO RELOJ FIJO (DESK CLOCK / BEDSIDE CLOCK) ---
    static void renderClockFace(uint8_t *buf, int hour, int min, float tempC, float humPct, int battPct, const char *dateStr) {
        clearBuffer(buf, 0xFF);

        // Marco exterior triple trazo (3px solido negro)
        drawRect(buf, 2, 2, 196, 196, 0, false);
        drawRect(buf, 3, 3, 194, 194, 0, false);
        drawRect(buf, 4, 4, 192, 192, 0, false);

        // Header con fecha
        drawLine(buf, 6, 32, 194, 32, 0, 3);
        if (dateStr && strlen(dateStr) > 0) {
            int l = strlen(dateStr);
            int startX = (200 - (l * 6)) / 2;
            if (startX < 8) startX = 8;
            drawString(buf, startX, 12, dateStr, 0, 1);
        } else {
            drawString(buf, 62, 12, "KRAKEN RELOJ", 0, 1);
        }

        // Hora gigante centrada (60px de alto, 30px de ancho, trazo 8px)
        int w = 30, h = 60, s = 8, gap = 7;
        int x0 = 22, y0 = 45;

        char hStr[6];
        snprintf(hStr, sizeof(hStr), "%02d:%02d", hour, min);

        // Dígito 1 y 2
        drawBigDigit(buf, x0, y0, w, h, s, hStr[0]);
        drawBigDigit(buf, x0 + w + gap, y0, w, h, s, hStr[1]);

        // Dos puntos cuadrados macizos (7x7 px)
        int cx_dots = x0 + (w + gap) * 2 + 2;
        drawRect(buf, cx_dots, y0 + 17, 7, 7, 0, true);
        drawRect(buf, cx_dots, y0 + 37, 7, 7, 0, true);

        // Dígito 3 y 4
        int x3 = cx_dots + 15;
        drawBigDigit(buf, x3, y0, w, h, s, hStr[3]);
        drawBigDigit(buf, x3 + w + gap, y0, w, h, s, hStr[4]);

        // Línea divisoria intermedia gruesa (3px)
        drawLine(buf, 6, 122, 194, 122, 0, 3);

        // Widgets de sensores ambientales y porcentaje de batería numérico
        // 1. Termómetro macizo
        int tx = 16;
        drawRect(buf, tx + 2, 133, 4, 12, 0, true);
        drawCircle(buf, tx + 4, 145, 4, 0, true);
        char tBuf[16];
        if (tempC > -40.0f) snprintf(tBuf, sizeof(tBuf), "%.1fC", tempC);
        else snprintf(tBuf, sizeof(tBuf), "--.-C");
        drawString(buf, tx + 12, 136, tBuf, 0, 1);

        // 2. Gota de humedad maciza
        int hx = 88;
        drawLine(buf, hx + 4, 132, hx, 142, 0, 2);
        drawLine(buf, hx + 4, 132, hx + 8, 142, 0, 2);
        drawCircle(buf, hx + 4, 144, 4, 0, true);
        char huBuf[16];
        if (humPct >= 0.0f) snprintf(huBuf, sizeof(huBuf), "%d%% HR", (int)round(humPct));
        else snprintf(huBuf, sizeof(huBuf), "--%% HR");
        drawString(buf, hx + 12, 136, huBuf, 0, 1);

        // 3. Batería con icono monocromático HD y porcentaje numérico
        int bx = 146;
        bool isChg = (battPct >= 100 && WiFi.status() == WL_CONNECTED);
        drawBatteryIcon(buf, bx, 137, battPct, isChg);
        char bBuf[8];
        snprintf(bBuf, sizeof(bBuf), "%d%%", constrain(battPct, 0, 100));
        drawString(buf, bx + 18, 137, bBuf, 0, 1);

        // Pie de página con instrucción
        drawLine(buf, 6, 162, 194, 162, 0, 2);
        drawString(buf, 26, 172, "* PULSA BOOT PARA SALIR *", 0, 1);
    }

    // --- MODO CLIMA FULL-SCREEN (200x200 WEATHER STATION) ---
    static void renderWeatherScreen(uint8_t *buf, int hour, int min, float tempC, float humPct, int battPct, const char *conditionText) {
        clearBuffer(buf, 0xFF);

        // 1. Marco exterior industrial y elegante (trazo 2px)
        drawRect(buf, 2, 2, 196, 196, 0, false);
        drawRect(buf, 3, 3, 194, 194, 0, false);

        // 2. TopBar Superior (y=4..26)
        char timeStr[12];
        if (hour >= 0 && min >= 0) {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, min);
        } else {
            snprintf(timeStr, sizeof(timeStr), "CLIMA");
        }
        drawString(buf, 10, 10, timeStr, 0, 1);

        // WiFi status al centro
        if (WiFi.status() == WL_CONNECTED) {
            int rssi = WiFi.RSSI();
            char wBuf[16];
            snprintf(wBuf, sizeof(wBuf), "WIFI %ddBm", rssi);
            drawString(buf, 68, 10, wBuf, 0, 1);
        } else {
            drawString(buf, 74, 10, "OFFLINE", 0, 1);
        }

        // Batería a la derecha
        bool isChg = (battPct >= 100 && WiFi.status() == WL_CONNECTED);
        drawBatteryIcon(buf, 142, 9, battPct, isChg);
        char bBuf[8];
        snprintf(bBuf, sizeof(bBuf), "%d%%", constrain(battPct, 0, 100));
        drawString(buf, 160, 10, bBuf, 0, 1);

        // Línea divisoria superior gruesa
        drawLine(buf, 4, 28, 196, 28, 0, 2);

        // 3. Panel Central: Icono meteorológico HD + Temperatura Gigante
        bool isRain = (humPct >= 75.0f);
        bool isSunny = (tempC >= 23.0f && !isRain);

        int iconX = 14, iconY = 40;
        if (isSunny) {
            // Icono SOL HD: Círculo central 16px + 8 Rayos potentes
            drawCircle(buf, iconX + 22, iconY + 22, 10, 0, true);
            // Rayos cardinales
            drawLine(buf, iconX + 22, iconY + 4,  iconX + 22, iconY + 9, 0, 2);
            drawLine(buf, iconX + 22, iconY + 35, iconX + 22, iconY + 40, 0, 2);
            drawLine(buf, iconX + 4,  iconY + 22, iconX + 9,  iconY + 22, 0, 2);
            drawLine(buf, iconX + 35, iconY + 22, iconX + 40, iconY + 22, 0, 2);
            // Rayos diagonales
            drawLine(buf, iconX + 9,  iconY + 9,  iconX + 13, iconY + 13, 0, 2);
            drawLine(buf, iconX + 31, iconY + 31, iconX + 35, iconY + 35, 0, 2);
            drawLine(buf, iconX + 31, iconY + 9,  iconX + 27, iconY + 13, 0, 2);
            drawLine(buf, iconX + 9,  iconY + 31, iconX + 13, iconY + 27, 0, 2);
        } else if (isRain) {
            // Icono NUBE CON LLUVIA HD
            drawCircle(buf, iconX + 16, iconY + 20, 10, 0, true);
            drawCircle(buf, iconX + 28, iconY + 16, 12, 0, true);
            drawCircle(buf, iconX + 38, iconY + 22, 8, 0, true);
            drawRect(buf, iconX + 16, iconY + 20, 24, 10, 0, true);
            // Gotas de lluvia diagonales (trazo 2px)
            drawLine(buf, iconX + 16, iconY + 34, iconX + 12, iconY + 42, 0, 2);
            drawLine(buf, iconX + 26, iconY + 34, iconX + 22, iconY + 42, 0, 2);
            drawLine(buf, iconX + 36, iconY + 34, iconX + 32, iconY + 42, 0, 2);
        } else {
            // Icono NUBE / TEMPLADO HD
            drawCircle(buf, iconX + 16, iconY + 22, 10, 0, true);
            drawCircle(buf, iconX + 28, iconY + 18, 12, 0, true);
            drawCircle(buf, iconX + 38, iconY + 24, 8, 0, true);
            drawRect(buf, iconX + 16, iconY + 22, 24, 10, 0, true);
            drawCircle(buf, iconX + 12, iconY + 12, 6, 0, false);
            drawLine(buf, iconX + 12, iconY + 3, iconX + 12, iconY + 5, 0, 1);
            drawLine(buf, iconX + 3, iconY + 12, iconX + 5, iconY + 12, 0, 1);
        }

        // Temperatura Gigante (38px alto) a la derecha del icono
        int tempX = 74, tempY = 38;
        char tStr[16];
        if (tempC > -40.0f) {
            snprintf(tStr, sizeof(tStr), "%.1f", tempC);
        } else {
            snprintf(tStr, sizeof(tStr), "--.-");
        }
        
        int digitW = 18, digitH = 38, stroke = 5, gap = 4;
        int curX = tempX;
        for (int i = 0; tStr[i] != '\0'; i++) {
            char c = tStr[i];
            if (c >= '0' && c <= '9') {
                drawBigDigit(buf, curX, tempY, digitW, digitH, stroke, c);
                curX += digitW + gap;
            } else if (c == '.') {
                drawRect(buf, curX + 1, tempY + digitH - 6, 6, 6, 0, true);
                curX += 9;
            } else if (c == '-') {
                drawRect(buf, curX, tempY + digitH / 2 - 2, 10, 4, 0, true);
                curX += 12;
            }
        }
        // Grado °C
        drawCircle(buf, curX + 4, tempY + 4, 3, 0, false);
        drawString(buf, curX + 10, tempY + 6, "C", 0, 2);

        // Texto de condición debajo de la temperatura
        char condBuf[32];
        if (conditionText && strlen(conditionText) > 0) {
            strncpy(condBuf, conditionText, sizeof(condBuf) - 1);
            condBuf[sizeof(condBuf) - 1] = '\0';
        } else {
            if (tempC >= 28.0f) snprintf(condBuf, sizeof(condBuf), "CLIMA MUY CALUROSO");
            else if (tempC >= 22.0f) snprintf(condBuf, sizeof(condBuf), "TEMPLADO Y AGRADABLE");
            else if (tempC >= 15.0f) snprintf(condBuf, sizeof(condBuf), "CLIMA FRESCO");
            else if (tempC > -40.0f) snprintf(condBuf, sizeof(condBuf), "CLIMA FRIO");
            else snprintf(condBuf, sizeof(condBuf), "SENSANDO AMBIENTE...");
        }
        int condLen = strlen(condBuf);
        int condX = (200 - condLen * 6) / 2;
        if (condX < 8) condX = 8;
        drawString(buf, condX, 94, condBuf, 0, 1);

        // 4. Panel Inferior: Sensores Ambientales (y=112..196)
        drawLine(buf, 4, 114, 196, 114, 0, 2);

        // Columna Izquierda: Humedad Relativa
        int hx = 14, hy = 124;
        drawLine(buf, hx + 5, hy, hx + 1, hy + 8, 0, 2);
        drawLine(buf, hx + 5, hy, hx + 9, hy + 8, 0, 2);
        drawCircle(buf, hx + 5, hy + 9, 5, 0, true);
        char humText[24];
        if (humPct >= 0.0f) snprintf(humText, sizeof(humText), "HUMEDAD: %d%% HR", (int)round(humPct));
        else snprintf(humText, sizeof(humText), "HUMEDAD: --%% HR");
        drawString(buf, hx + 18, hy + 4, humText, 0, 1);

        // Sensación / Confort ambiental
        int cx = 14, cy = 144;
        drawRect(buf, cx + 2, cy, 3, 10, 0, true);
        drawCircle(buf, cx + 3, cy + 12, 4, 0, true);
        char confText[28];
        if (tempC > -40.0f && humPct >= 0.0f) {
            if (humPct > 70.0f && tempC > 24.0f) snprintf(confText, sizeof(confText), "CONFORT: BOCHORNOSO");
            else if (humPct < 30.0f) snprintf(confText, sizeof(confText), "CONFORT: AIRE SECO");
            else snprintf(confText, sizeof(confText), "CONFORT: EXCELENTE");
        } else {
            snprintf(confText, sizeof(confText), "CONFORT: NORMAL");
        }
        drawString(buf, cx + 18, cy + 4, confText, 0, 1);

        // Pie con instrucción
        drawLine(buf, 4, 168, 196, 168, 0, 1);
        drawString(buf, 26, 176, "* PULSA BOOT PARA SALIR *", 0, 1);
    }

    // Dibuja una curva cuadrática de Bézier (útil para sonrisas, olas, colinas, cejas)
    static void drawBezierCurve(uint8_t *buf, int x0, int y0, int cx, int cy, int x1, int y1, int color = 0, int thickness = 1) {
        int lastX = x0;
        int lastY = y0;
        for (int i = 1; i <= 20; i++) {
            float t = (float)i / 20.0f;
            float it = 1.0f - t;
            int curX = (int)roundf(it * it * (float)x0 + 2.0f * it * t * (float)cx + t * t * (float)x1);
            int curY = (int)roundf(it * it * (float)y0 + 2.0f * it * t * (float)cy + t * t * (float)y1);
            drawLine(buf, lastX, lastY, curX, curY, color, thickness);
            lastX = curX;
            lastY = curY;
        }
    }

    // Renderizador de macros de escenas modulares de alta fidelidad
    static void renderMacro(uint8_t *buf, const char *name, int x, int y, int p1, int p2) {
        if (!buf || !name) return;
        if (strcmp(name, "SUN") == 0 || strcmp(name, "SOL") == 0) {
            int r = (p1 > 5 && p1 < 50) ? p1 : 16;
            drawCircle(buf, x, y, r, 0, false);
            drawCircle(buf, x, y, (r > 4 ? r - 4 : r), 0, true);
            for (int ang = 0; ang < 360; ang += 45) {
                float rad = (float)ang * 0.01745329f;
                int x_in = x + (int)roundf(cosf(rad) * (float)(r + 3));
                int y_in = y + (int)roundf(sinf(rad) * (float)(r + 3));
                int x_out = x + (int)roundf(cosf(rad) * (float)(r + 10));
                int y_out = y + (int)roundf(sinf(rad) * (float)(r + 10));
                drawLine(buf, x_in, y_in, x_out, y_out, 0, 2);
            }
        } else if (strcmp(name, "MOON") == 0 || strcmp(name, "LUNA") == 0 || strcmp(name, "MOON_SMILE") == 0) {
            bool smile = (p2 == 1 || strcmp(name, "MOON_SMILE") == 0);
            if (!smile) {
                int r = (p1 > 10 && p1 < 60) ? p1 : 40;
                drawCircle(buf, x, y, r, 0, false);
                drawCircle(buf, x + r / 2, y - r / 6, r * 4 / 5, 1, true);
                drawCircle(buf, x, y, r, 0, false);
            } else {
                drawBezierCurve(buf, x - 15, y - 65, x + 85, y, x - 15, y + 65, 0, 3);
                drawBezierCurve(buf, x - 15, y - 65, x + 5, y, x - 15, y + 65, 0, 3);
                drawBezierCurve(buf, x + 22, y - 18, x + 30, y - 27, x + 38, y - 18, 0, 2);
                drawLine(buf, x + 26, y - 23, x + 23, y - 28, 0, 2);
                drawLine(buf, x + 34, y - 23, x + 37, y - 28, 0, 2);
                drawBezierCurve(buf, x + 38, y - 3, x + 46, y + 2, x + 40, y + 8, 0, 2);
                drawBezierCurve(buf, x + 20, y + 20, x + 34, y + 34, x + 48, y + 22, 0, 3);
                drawLine(buf, x + 18, y + 18, x + 20, y + 22, 0, 2);
                drawLine(buf, x + 48, y + 20, x + 51, y + 25, 0, 2);
                drawCircle(buf, x + 18, y + 7, 3, 0, false);
            }
        } else if (strcmp(name, "HOUSE") == 0 || strcmp(name, "CASA") == 0) {
            int w = (p1 > 20 && p1 < 120) ? p1 : 65;
            int h = (p2 > 20 && p2 < 120) ? p2 : 65;
            int wallH = h * 6 / 10;
            int wallY = y + (h - wallH);
            drawRect(buf, x, wallY, w, wallH, 0, false);
            drawLine(buf, x - 4, wallY, x + w / 2, y, 0, 3);
            drawLine(buf, x + w / 2, y, x + w + 4, wallY, 0, 3);
            drawLine(buf, x - 4, wallY, x + w + 4, wallY, 0, 2);
            int chimW = w / 7;
            int chimH = h / 4;
            drawRect(buf, x + w * 7 / 10, y + h / 10, chimW, chimH, 0, false);
            drawCircle(buf, x + w * 7 / 10 + 2, y + h / 10 - 4, 2, 0, false);
            drawCircle(buf, x + w * 7 / 10 + 5, y + h / 10 - 9, 3, 0, false);
            int doorW = w / 3;
            int doorH = wallH * 6 / 10;
            drawRect(buf, x + w / 2 - doorW / 2, y + h - doorH, doorW, doorH, 0, false);
            drawCircle(buf, x + w / 2 + doorW / 4, y + h - doorH / 2, 2, 0, true);
            int winW = w / 4;
            int winH = winW;
            int winX = x + w / 7;
            int winY = wallY + wallH / 5;
            drawRect(buf, winX, winY, winW, winH, 0, false);
            drawLine(buf, winX + winW / 2, winY, winX + winW / 2, winY + winH, 0, 1);
            drawLine(buf, winX, winY + winH / 2, winX + winW, winY + winH / 2, 0, 1);
        } else if (strcmp(name, "COW") == 0 || strcmp(name, "VACA") == 0) {
            drawRect(buf, x, y, 55, 32, 0, false);
            drawLine(buf, x + 8, y + 32, x + 8, y + 46, 0, 3);
            drawLine(buf, x + 18, y + 32, x + 18, y + 46, 0, 3);
            drawLine(buf, x + 38, y + 32, x + 38, y + 46, 0, 3);
            drawLine(buf, x + 48, y + 32, x + 48, y + 46, 0, 3);
            drawCircle(buf, x + 63, y - 4, 14, 0, false);
            drawCircle(buf, x + 71, y, 8, 1, true);
            drawCircle(buf, x + 71, y, 8, 0, false);
            drawCircle(buf, x + 69, y - 1, 1, 0, true);
            drawCircle(buf, x + 73, y - 1, 1, 0, true);
            drawLine(buf, x + 59, y - 16, x + 55, y - 23, 0, 2);
            drawLine(buf, x + 67, y - 16, x + 71, y - 23, 0, 2);
            drawLine(buf, x + 53, y - 10, x + 45, y - 8, 0, 2);
            drawLine(buf, x + 73, y - 10, x + 81, y - 8, 0, 2);
            drawCircle(buf, x + 63, y - 8, 2, 0, true);
            drawCircle(buf, x + 20, y + 10, 7, 0, true);
            drawCircle(buf, x + 40, y + 16, 8, 0, true);
            drawCircle(buf, x + 30, y + 6, 5, 0, true);
            drawLine(buf, x, y + 5, x - 7, y + 18, 0, 2);
            drawCircle(buf, x - 8, y + 19, 2, 0, true);
        } else if (strcmp(name, "STAR") == 0 || strcmp(name, "ESTRELLA") == 0) {
            int r = (p1 > 2 && p1 < 30) ? p1 : 7;
            drawCircle(buf, x, y, 2, 0, true);
            drawLine(buf, x, y - r, x, y + r, 0, 1);
            drawLine(buf, x - r, y, x + r, y, 0, 1);
            int d = (int)roundf((float)r * 0.65f);
            drawLine(buf, x - d, y - d, x + d, y + d, 0, 1);
            drawLine(buf, x - d, y + d, x + d, y - d, 0, 1);
        } else if (strcmp(name, "CLOUD") == 0 || strcmp(name, "NUBE") == 0) {
            drawCircle(buf, x - 15, y, 12, 0, true);
            drawCircle(buf, x + 2, y - 8, 16, 0, true);
            drawCircle(buf, x + 18, y, 12, 0, true);
            drawRect(buf, x - 15, y, 33, 12, 0, true);
        } else if (strcmp(name, "TREE") == 0 || strcmp(name, "ARBOL") == 0) {
            drawRect(buf, x - 4, y, 8, 30, 0, false);
            drawCircle(buf, x, y - 10, 20, 0, false);
            drawCircle(buf, x - 8, y - 8, 12, 0, true);
            drawCircle(buf, x + 8, y - 8, 12, 0, true);
            drawCircle(buf, x, y - 18, 14, 0, true);
        } else if (strcmp(name, "DRAGON") == 0 || strcmp(name, "DINO") == 0) {
            drawLine(buf, x-30, y-15, x-55, y-8, 0, 3);
            drawLine(buf, x-55, y-8, x-52, y, 0, 3);
            drawLine(buf, x-52, y, x-35, y-2, 0, 2);
            drawLine(buf, x-35, y-2, x-48, y+8, 0, 2);
            drawLine(buf, x-48, y+8, x-28, y+6, 0, 2);
            drawLine(buf, x-30, y-15, x-12, y-32, 0, 3);
            drawCircle(buf, x-36, y-13, 2, 0, true);
            drawBezierCurve(buf, x-28, y+6, x-15, y+30, x-5, y+45, 0, 3);
            drawBezierCurve(buf, x-26, y-13, x-5, y+5, x+5, y+25, 0, 3);
            drawLine(buf, x+2, y+25, x+35, y-40, 0, 3);
            drawLine(buf, x+35, y-40, x+80, y-30, 0, 3);
            drawLine(buf, x+35, y-40, x+70, y-5, 0, 2);
            drawLine(buf, x+35, y-40, x+55, y+10, 0, 2);
            drawBezierCurve(buf, x+80, y-30, x+75, y-15, x+70, y-5, 0, 2);
            drawBezierCurve(buf, x+70, y-5, x+62, y+5, x+55, y+10, 0, 2);
            drawBezierCurve(buf, x+55, y+10, x+35, y+25, x+18, y+35, 0, 2);
            drawLine(buf, x-2, y+58, x-12, y+85, 0, 3);
            drawLine(buf, x+28, y+62, x+32, y+88, 0, 3);
            drawBezierCurve(buf, x+30, y+65, x+60, y+75, x+65, y+95, 0, 3);
            drawBezierCurve(buf, x+65, y+95, x+68, y+110, x+85, y+100, 0, 3);
            drawBezierCurve(buf, x-54, y+4, x-75, y, x-92, y-15, 0, 2);
            drawBezierCurve(buf, x-52, y+6, x-72, y+12, x-88, y+8, 0, 2);
        }
    }

    // --- RENDERIZADOR DE BOCETOS VECTORIALES (SKETCH MICRO-DSL EXTENDIDO) ---
    static void renderVectorSketch(uint8_t *buf, const char *dsl, const char *caption = nullptr) {
        if (!buf || !dsl) return;
        clearBuffer(buf, 0xFF); // Fondo blanco puro por defecto

        const char *p = dsl;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (strncmp(p, "[SKETCH:", 8) == 0) {
            p += 8;
        } else if (strncmp(p, "[DRAW:", 6) == 0) {
            p += 6;
        }

        // Auto-detección y escalado si el LLM emitió coordenadas mayores a 200 (ej. canvas 400x400 u 800x600)
        int maxCoord = 0;
        const char *scanP = p;
        while (*scanP) {
            if (*scanP >= '0' && *scanP <= '9') {
                int num = atoi(scanP);
                if (num > maxCoord && num < 2500) maxCoord = num;
                while (*scanP >= '0' && *scanP <= '9') scanP++;
            } else {
                scanP++;
            }
        }
        float coordScale = (maxCoord > 210) ? (190.0f / (float)maxCoord) : 1.0f;

        char cmd[256];
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ';') p++;
            if (*p == '\0' || *p == ']') break;

            int len = 0;
            while (*p && *p != ';' && *p != ']' && len < (int)(sizeof(cmd) - 1)) {
                cmd[len++] = *p++;
            }
            cmd[len] = '\0';
            if (*p == ';') p++;

            char op[16] = {0};
            int val1 = 0, val2 = 0, val3 = 0, val4 = 0, val5 = 0, val6 = 0;
            char *token = strtok(cmd, " ,\t");
            if (!token) continue;
            strncpy(op, token, sizeof(op) - 1);
            for (int i = 0; op[i]; i++) {
                if (op[i] >= 'a' && op[i] <= 'z') op[i] = op[i] - 'a' + 'A';
            }

            auto readScaled = [&](char *tok) -> int {
                if (!tok) return 0;
                int n = atoi(tok);
                return (coordScale < 0.99f) ? (int)roundf((float)n * coordScale) : n;
            };

            auto readColor = [&](char *tok, int defCol) -> int {
                if (!tok) return defCol;
                if (strcasecmp(tok, "WHITE") == 0 || strcasecmp(tok, "BLANCO") == 0 || strcasecmp(tok, "1") == 0) return 1;
                return 0;
            };

            if (strcmp(op, "CLEAR") == 0) {
                token = strtok(nullptr, " ,\t");
                int col = token ? atoi(token) : 1;
                clearBuffer(buf, col ? 0xFF : 0x00);
            } else if (strcmp(op, "L") == 0 || strcmp(op, "LINE") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                val4 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val5 = token ? atoi(token) : 2;
                token = strtok(nullptr, " ,\t"); val6 = readColor(token, 0);
                drawLine(buf, val1, val2, val3, val4, val6, val5);
            } else if (strcmp(op, "C") == 0 || strcmp(op, "CIRCLE") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val4 = token ? atoi(token) : 0;
                token = strtok(nullptr, " ,\t"); val5 = readColor(token, 0);
                drawCircle(buf, val1, val2, val3, val5, val4 != 0);
            } else if (strcmp(op, "FC") == 0 || strcmp(op, "FCIRCLE") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val4 = readColor(token, 0);
                drawCircle(buf, val1, val2, val3, val4, true);
            } else if (strcmp(op, "R") == 0 || strcmp(op, "RECT") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                val4 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val5 = token ? atoi(token) : 0;
                token = strtok(nullptr, " ,\t"); val6 = readColor(token, 0);
                drawRect(buf, val1, val2, val3, val4, val6, val5 != 0);
            } else if (strcmp(op, "FR") == 0 || strcmp(op, "FRECT") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                val4 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val5 = readColor(token, 0);
                drawRect(buf, val1, val2, val3, val4, val5, true);
            } else if (strcmp(op, "CURVE") == 0 || strcmp(op, "BEZIER") == 0 || strcmp(op, "ARC") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                val4 = readScaled(strtok(nullptr, " ,\t"));
                val5 = readScaled(strtok(nullptr, " ,\t"));
                val6 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); int th = token ? atoi(token) : 2;
                token = strtok(nullptr, " ,\t"); int col = readColor(token, 0);
                drawBezierCurve(buf, val1, val2, val3, val4, val5, val6, col, th);
            } else if (strcmp(op, "MACRO") == 0 || strcmp(op, "M") == 0) {
                token = strtok(nullptr, " ,\t");
                if (token) {
                    char mName[32] = {0};
                    strncpy(mName, token, sizeof(mName) - 1);
                    for (int i = 0; mName[i]; i++) {
                        if (mName[i] >= 'a' && mName[i] <= 'z') mName[i] = mName[i] - 'a' + 'A';
                    }
                    int mx = readScaled(strtok(nullptr, " ,\t"));
                    int my = readScaled(strtok(nullptr, " ,\t"));
                    token = strtok(nullptr, " ,\t"); int mp1 = token ? atoi(token) : 0;
                    token = strtok(nullptr, " ,\t"); int mp2 = token ? atoi(token) : 0;
                    renderMacro(buf, mName, mx, my, mp1, mp2);
                }
            } else if (strcmp(op, "D") == 0 || strcmp(op, "DITHER") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                val3 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val4 = token ? atoi(token) : 8;
                drawFilledCircleDither(buf, val1, val2, val3, (uint8_t)val4);
            } else if (strcmp(op, "T") == 0 || strcmp(op, "TEXT") == 0) {
                val1 = readScaled(strtok(nullptr, " ,\t"));
                val2 = readScaled(strtok(nullptr, " ,\t"));
                token = strtok(nullptr, " ,\t"); val3 = token ? atoi(token) : 1;
                char *txt = strtok(nullptr, "");
                if (txt) {
                    while (*txt == ' ') txt++;
                    drawString(buf, val1, val2, txt, 0, val3);
                }
            }
        }

        if (caption && strlen(caption) > 0) {
            drawRect(buf, 0, 182, 200, 18, 1, true);
            drawLine(buf, 0, 182, 200, 182, 0, 1);
            int cLen = strlen(caption);
            int cx = (200 - cLen * 6) / 2;
            if (cx < 4) cx = 4;
            drawString(buf, cx, 187, caption, 0, 1);
        }
    }
};

#endif // FACES_BITMAPS_H
