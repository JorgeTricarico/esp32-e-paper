#ifndef COPITO_LOG_H
#define COPITO_LOG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
// COPITO LOG: bitácora persistente en RAM + flash con visor web (/api/logs)
// ============================================================================
// - Ring buffer circular (200 entradas) en PSRAM/heap: costo ~0, sobrevive
//   todo menos un reboot.
// - Nivel WARN y ERROR además se anexan a /logs.txt (LittleFS, con rotación
//   a 24 KB): sobreviven reboots y deep-sleep. Al arrancar se recarga la cola.
// - CLOGI/CLOGW/CLOGE escriben al ring (+ espejo a Serial con el mismo texto)
//   así el monitor USB y la consola web muestran lo mismo.
// - La web expone GET /api/logs?n=80&level=I&src=ram y POST /api/logs_clear.
//
// Uso: CLOGI(TAG, "MP3 %d Hz, %d ch", hz, ch);  (TAG = "AI", "AUDIO", ...)
// ============================================================================

#define COPITO_LOG_CAPACITY 200
#define COPITO_LOG_TAG_LEN  16
#define COPITO_LOG_MSG_LEN  120
#define COPITO_LOG_FILE     "/logs.txt"
#define COPITO_LOG_FILE_MAX (24 * 1024)

struct CopitoLogEntry {
    uint32_t bootMs;                    // millis() al registrar
    char level;                         // 'I' (info), 'W' (warn), 'E' (error)
    char tag[COPITO_LOG_TAG_LEN];
    char msg[COPITO_LOG_MSG_LEN];
};

class CopitoLog {
public:
    CopitoLog();
    void begin();  // reserva ring + recarga cola de /logs.txt
    void write(char level, const char *tag, const char *fmt, ...);
    void clear();  // vacía ring + borra /logs.txt

    size_t toJson(String &out, size_t maxEntries, char minLevel, bool fromFlash);
    size_t count() const { return stored; }

private:
    CopitoLogEntry *ring;
    size_t head;    // próxima posición a escribir
    size_t stored;  // entradas válidas (<= CAPACITY)
    SemaphoreHandle_t logMutex;

    static int levelRank(char lvl);
    void persist(const CopitoLogEntry &e);
    size_t fileTailJson(String &out, size_t maxEntries, char minLevel);
};

extern CopitoLog copitoLog;

#define CLOGI(tag, fmt, ...) copitoLog.write('I', tag, fmt, ##__VA_ARGS__)
#define CLOGW(tag, fmt, ...) copitoLog.write('W', tag, fmt, ##__VA_ARGS__)
#define CLOGE(tag, fmt, ...) copitoLog.write('E', tag, fmt, ##__VA_ARGS__)

#endif // COPITO_LOG_H
