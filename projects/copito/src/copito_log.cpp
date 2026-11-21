#include "copito_log.h"
#include <LittleFS.h>
#include <stdarg.h>

CopitoLog copitoLog;

CopitoLog::CopitoLog() : ring(nullptr), head(0), stored(0), logMutex(nullptr) {}

void CopitoLog::begin() {
    if (!logMutex) {
        logMutex = xSemaphoreCreateMutex();
    }
    if (!ring) {
        if (psramFound()) {
            ring = (CopitoLogEntry *)ps_malloc(sizeof(CopitoLogEntry) * COPITO_LOG_CAPACITY);
        }
        if (!ring) {
            ring = (CopitoLogEntry *)malloc(sizeof(CopitoLogEntry) * COPITO_LOG_CAPACITY);
        }
    }
    if (!ring) return;
    memset(ring, 0, sizeof(CopitoLogEntry) * COPITO_LOG_CAPACITY);

    // Recargar cola de flash: el historial de la sesión anterior (incluido lo
    // que pasó justo antes de un reboot/deep-sleep) vuelve a estar visible.
    if (LittleFS.begin(false)) {
        File f = LittleFS.open(COPITO_LOG_FILE, "r");
        if (f) {
            char line[COPITO_LOG_TAG_LEN + COPITO_LOG_MSG_LEN + 24];
            while (f.available()) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                if (n == 0) continue;
                line[n] = '\0';
                // Formato: [bootMs][L][tag] msg
                uint32_t t = 0;
                char lvl = 'I';
                char tg[COPITO_LOG_TAG_LEN] = {0};
                char ms[COPITO_LOG_MSG_LEN] = {0};
                if (sscanf(line, "[%u][%c][%15[^]]] %119[^\n]", &t, &lvl, tg, ms) == 4) {
                    CopitoLogEntry &e = ring[head];
                    e.bootMs = t;
                    e.level = lvl;
                    strncpy(e.tag, tg, COPITO_LOG_TAG_LEN - 1);
                    strncpy(e.msg, ms, COPITO_LOG_MSG_LEN - 1);
                    head = (head + 1) % COPITO_LOG_CAPACITY;
                    if (stored < COPITO_LOG_CAPACITY) stored++;
                }
            }
            f.close();
        }
    }
}

int CopitoLog::levelRank(char lvl) {
    if (lvl == 'E') return 2;
    if (lvl == 'W') return 1;
    return 0;
}

void CopitoLog::write(char level, const char *tag, const char *fmt, ...) {
    if (!ring) return;  // begin() aún no corrió (logs ultra-tempranos): solo Serial
    char tmp[COPITO_LOG_MSG_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    // Una entrada = una línea: los %s multilínea (bodies HTTP) se aplanan acá
    // para no romper ni el ring, ni /logs.txt, ni su recarga al boot.
    for (char *p = tmp; *p; p++) {
        if (*p == '\n' || *p == '\r') *p = ' ';
    }

    if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
    CopitoLogEntry &e = ring[head];
    e.bootMs = millis();
    e.level = level;
    strncpy(e.tag, tag ? tag : "?", COPITO_LOG_TAG_LEN - 1);
    e.tag[COPITO_LOG_TAG_LEN - 1] = '\0';
    strncpy(e.msg, tmp, COPITO_LOG_MSG_LEN - 1);
    e.msg[COPITO_LOG_MSG_LEN - 1] = '\0';
    head = (head + 1) % COPITO_LOG_CAPACITY;
    if (stored < COPITO_LOG_CAPACITY) stored++;

    // Espejo a Serial con el mismo texto (el monitor USB muestra lo mismo)
    Serial.printf("[%lu][%c][%s] %s\n", (unsigned long)e.bootMs, level, e.tag, e.msg);

    // WARN/ERROR se persisten en flash en vivo (son raros; no desgasta)
    if (level == 'W' || level == 'E') {
        persist(e);
    }
    if (logMutex) xSemaphoreGive(logMutex);
}

void CopitoLog::persist(const CopitoLogEntry &e) {
    if (!LittleFS.begin(false)) return;
    File f = LittleFS.open(COPITO_LOG_FILE, "r");
    size_t cur = f ? f.size() : 0;
    if (f) f.close();
    if (cur >= COPITO_LOG_FILE_MAX) {
        LittleFS.remove(COPITO_LOG_FILE);  // rotación simple: arrancar de cero
        File m = LittleFS.open(COPITO_LOG_FILE, "w");
        if (m) {
            m.println("[0][W][LOG] rotacion de /logs.txt (24KB max)");
            m.close();
        }
    }
    File a = LittleFS.open(COPITO_LOG_FILE, "a");
    if (a) {
        // Sanitizar: un %s con saltos de línea (ej. body de error HTTP) rompería
        // el formato una-línea-una-entrada y contaminaría la recarga y /api/logs.
        String m = e.msg;
        m.replace("\n", " ");
        m.replace("\r", " ");
        if (m.length() > COPITO_LOG_MSG_LEN - 1) m = m.substring(0, COPITO_LOG_MSG_LEN - 1);
        a.printf("[%lu][%c][%s] %s\n", (unsigned long)e.bootMs, e.level, e.tag, m.c_str());
        a.close();
    }
}

void CopitoLog::clear() {
    if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
    head = 0;
    stored = 0;
    if (ring) memset(ring, 0, sizeof(CopitoLogEntry) * COPITO_LOG_CAPACITY);
    if (LittleFS.begin(false)) {
        LittleFS.remove(COPITO_LOG_FILE);
    }
    if (logMutex) xSemaphoreGive(logMutex);
}

size_t CopitoLog::toJson(String &out, size_t maxEntries, char minLevel, bool fromFlash) {
    if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
    if (fromFlash) {
        size_t res = fileTailJson(out, maxEntries, minLevel);
        if (logMutex) xSemaphoreGive(logMutex);
        return res;
    }
    if (!ring || stored == 0) {
        out = "{\"success\":true,\"count\":0,\"entries\":[]}";
        if (logMutex) xSemaphoreGive(logMutex);
        return 0;
    }
    int rank = levelRank(minLevel);
    size_t n = (stored < maxEntries) ? stored : maxEntries;
    size_t start = (head + COPITO_LOG_CAPACITY - n) % COPITO_LOG_CAPACITY;
    String body;
    size_t emitted = 0;
    for (size_t k = 0; k < n; k++) {
        const CopitoLogEntry &e = ring[(start + k) % COPITO_LOG_CAPACITY];
        if (levelRank(e.level) < rank) continue;
        if (emitted > 0) body += ",";
        body += "{\"t\":";
        body += String((unsigned)e.bootMs);
        body += ",\"lvl\":\"";
        body += e.level;
        body += "\",\"tag\":\"";
        body += e.tag;
        body += "\",\"msg\":\"";
        String m = e.msg;
        m.replace("\\", "\\\\");
        m.replace("\"", "\\\"");
        m.replace("\n", " ");
        m.replace("\r", " ");
        body += m;
        body += "\"}";
        emitted++;
    }
    out = "{\"success\":true,\"count\":";
    out += String((unsigned)emitted);
    out += ",\"entries\":[";
    out += body;
    out += "]}";
    if (logMutex) xSemaphoreGive(logMutex);
    return emitted;
}

size_t CopitoLog::fileTailJson(String &out, size_t maxEntries, char minLevel) {
    out = "{\"success\":true,\"count\":0,\"entries\":[]}";
    if (!LittleFS.begin(false)) return 0;
    File f = LittleFS.open(COPITO_LOG_FILE, "r");
    if (!f) return 0;
    // Archivo acotado a 24KB: leer entero a PSRAM/heap es seguro
    String content = f.readString();
    f.close();
    int rank = levelRank(minLevel);
    // Quedarse con las últimas maxEntries líneas que pasen el filtro
    int total = 1;
    for (size_t i = 0; i < content.length(); i++) {
        if (content.charAt(i) == '\n') total++;
    }
    int skip = total - (int)maxEntries - 1;
    if (skip < 0) skip = 0;
    out = "{\"success\":true,\"entries\":[";
    size_t emitted = 0;
    int lineNo = 0;
    int pos = 0;
    while (pos >= 0) {
        int nl = content.indexOf('\n', pos);
        String line = (nl < 0) ? content.substring(pos) : content.substring(pos, nl);
        pos = (nl < 0) ? -1 : nl + 1;
        line.trim();
        if (line.length() == 0) continue;
        // Solo líneas con formato de bitácora; restos viejos (ej. bodies JSON
        // multi-línea de antes del fix) se ignoran en vez de mostrarse.
        if (line.charAt(0) != '[' || line.indexOf("][") < 0) continue;
        if (lineNo++ < skip) continue;
        // Filtro por nivel: [t][L][tag] msg
        int p1 = line.indexOf("][");
        int p2 = (p1 > 0) ? line.indexOf("][", p1 + 2) : -1;
        char lvl = (p1 > 0 && p1 + 2 < (int)line.length()) ? line.charAt(p1 + 2) : 'I';
        if (levelRank(lvl) < rank) continue;
        line.replace("\\", "\\\\");
        line.replace("\"", "\\\"");
        if (emitted > 0) out += ",";
        out += "{\"raw\":\"";
        out += line;
        out += "\"}";
        emitted++;
        if (emitted >= maxEntries) break;
    }
    out += "],\"count\":";
    out += String((unsigned)emitted);
    out += "}";
    return emitted;
}
