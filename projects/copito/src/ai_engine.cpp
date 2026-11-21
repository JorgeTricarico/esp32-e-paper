#include "ai_engine.h"
#include "copito_tools.h"
#include "wifi_portal.h"
#include "copito_memory.h"
#include "copito_log.h"
#include "epaper_driver.h"
#include "rtc_pcf85063.h"
#include "battery_monitor.h"
#include <Preferences.h>
#include <time.h>
#include <rom/tjpgd.h>
#include <ArduinoJson.h>
#include <vector>

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "voice_validate.h"

// Cancelacion del cuento en cascada (story_cascade.cpp): los fetch de imagen
// la miran para abortar rapido al salir con PWR o pedir otro cuento.
extern volatile bool g_storyCancel;
// Plan leve de energía (main.cpp): voz/streaming con radio full.
extern void powerBoost();
extern void powerRelax();
// Guardia RAII: streaming (música/radio) con boost garantizado y relax al
// salir por CUALQUIER return. Sin esto, el modem-sleep entrecorta el PCM
// (el jitter del radio vacía el prebuffer).
struct PowerStreamGuard {
    PowerStreamGuard() { powerBoost(); }
    ~PowerStreamGuard() { powerRelax(); }
};

static const char *TAG = "AI";

// Transporte recordado: 0 = sin dato, 1 = LAN, 2 = funnel. Evita pagar
// LAN(6s)+funnel(15s) en serie en cada arranque: se prueba primero el último
// que funcionó. También marca cuándo falló la LAN para la ruta de calle.
static int s_lastGoodTransport = 0;
static unsigned long s_lastLanFailMs = 0;
// Fast-fail TTS local: si los servidores LAN fallaron hace poco, no pagar
// 8s+45s por host antes de la nube (en la calle eso era ~1 min de silencio).
static unsigned long s_lastLocalTtsFailMs = 0;
// Voz narrador: cuando narrateStoryPart trabaja, streamTtsSingle prueba
// primero Gemini TTS con voz Sulafat lenta (cuento) antes que la cadena normal.
static bool s_storyVoice = false;
// Estadísticas de voz para /api/selftest.
static uint32_t s_ttsOk = 0, s_ttsFail = 0;
static char s_ttsLastErr[96] = "";
static void aiVoiceNote(bool ok, const char *err) {
    if (ok) s_ttsOk++;
    else {
        s_ttsFail++;
        if (err && err[0]) {
            strncpy(s_ttsLastErr, err, sizeof(s_ttsLastErr) - 1);
            s_ttsLastErr[sizeof(s_ttsLastErr) - 1] = '\0';
        }
    }
}
void aiVoiceStats(uint32_t &ok, uint32_t &fail, char *lastErr, size_t lastErrCap) {
    ok = s_ttsOk;
    fail = s_ttsFail;
    if (lastErr && lastErrCap > 0) {
        strncpy(lastErr, s_ttsLastErr, lastErrCap - 1);
        lastErr[lastErrCap - 1] = '\0';
    }
}

AIEngine aiEngine;
String g_lastVoiceTranscript = "";
String g_lastSpoken = "";                 // ultimo texto hablado (comando "repeti")
volatile bool g_musicStopRequested = false; // "para" frena stream ESP32 y Pi3
String g_musicEsp32Title = "";            // sonando EN Copito ("" = nada)
volatile bool g_musicPlaying = false;     // true mientras suena musica/radio
// (los botones cambian de funcion: BOOT=subir, PWR=bajar, mantener=frenar)
volatile bool g_speaking = false;         // true mientras el TTS habla
volatile bool g_ttsStopRequested = false; // BOOT mientras habla = callar

AIEngine::AIEngine() {
}

AIEngine::~AIEngine() {
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void AIEngine::streamBase64(WiFiClientSecure &cli, const uint8_t *data, size_t len) {
    char outChunk[1025];
    size_t outIdx = 0;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t a = data[i];
        uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t c = (i + 2 < len) ? data[i + 2] : 0;

        uint32_t triple = (a << 16) + (b << 8) + c;

        outChunk[outIdx++] = b64_table[(triple >> 18) & 0x3F];
        outChunk[outIdx++] = b64_table[(triple >> 12) & 0x3F];
        outChunk[outIdx++] = (i + 1 < len) ? b64_table[(triple >> 6) & 0x3F] : '=';
        outChunk[outIdx++] = (i + 2 < len) ? b64_table[triple & 0x3F] : '=';

        if (outIdx >= 1024) {
            cli.write((const uint8_t *)outChunk, outIdx);
            outIdx = 0;
        }
    }
    if (outIdx > 0) {
        cli.write((const uint8_t *)outChunk, outIdx);
    }
}

// URL-encode para query de busqueda (SearXNG va como query param).
static String urlEncode(const String &src) {
    static const char hex[] = "0123456789ABCDEF";
    const char *safe = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~";
    String out;
    for (size_t i = 0; i < src.length(); i++) {
        char c = src.charAt(i);
        if (strchr(safe, c)) {
            out += c;
        } else {
            uint8_t b = (uint8_t)c;
            out += '%';
            out += hex[b >> 4];
            out += hex[b & 0xF];
        }
    }
    return out;
}

// Prototipos (definidas mas abajo, se usan en queryGeminiAudio/Text).
static String musicNorm(const String &src);
static bool isStoryRequest(const String &norm);

// Orden para la IA cuando piden cuento/historia: historia completa y larga,
// con cierre de modo para el icono libro. Se suma al prompt del pedido.
static const char *STORY_DIRECTIVE =
    " [MODO CUENTO LARGO: el usuario pidio expresamente un cuento, historia o relato."
    " Es un cuento INFANTIL para ninos de 3 a 8 anios: lenguaje simple y dulce,"
    " sin miedo ni violencia, con final feliz."
    " IGNORA el limite de 40 palabras: conta la historia COMPLETA y larga"
    " (hasta unas 1100 letras), con principio, desarrollo y final."
    " Termina OBLIGATORIAMENTE con el tag [MODE:CUENTO].]";

static String readHttpBody(WiFiClient &cli, bool isChunked) {
    String body = "";
    unsigned long bodyStart = millis();
    if (isChunked) {
        while ((cli.connected() || cli.available()) && (millis() - bodyStart < 25000)) {
            if (!cli.available()) {
                delay(2);
                continue;
            }
            String hexLine = cli.readStringUntil('\n');
            hexLine.trim();
            if (hexLine.length() == 0) continue;

            long chunkSize = strtol(hexLine.c_str(), NULL, 16);
            if (chunkSize <= 0) {
                break; // 0 chunk indicates end of body
            }

            long remaining = chunkSize;
            unsigned long lastDataMs = millis();
            while (remaining > 0 && (cli.connected() || cli.available())) {
                char buf[128];
                size_t toRead = (size_t)min((long)sizeof(buf) - 1, remaining);
                size_t bytesRead = cli.readBytes(buf, toRead);
                if (bytesRead > 0) {
                    buf[bytesRead] = '\0';
                    body += buf;
                    remaining -= bytesRead;
                    lastDataMs = millis();
                } else {
                    if (millis() - lastDataMs > 12000) break;
                    delay(2);
                }
            }
            if (remaining > 0) break;

            // Consume trailing \r\n after chunk data
            unsigned long crlfWait = millis();
            while (cli.available() && (millis() - crlfWait < 500)) {
                char c = cli.read();
                if (c == '\n') break;
            }
        }
    } else {
        body = cli.readString();
    }
    return body;
}

static String sanitizeTtsText(const String &input) {
    // Modelos razonadores (qwen/gemini-thinking) devuelven "thinking...</think>
    // We want ONLY the final answer (after </thinking> if present; fallback: keep all).
    String src = input;
    {
        int endThink = src.indexOf("</thinking>");
        if (endThink > 0) {
            int after = src.indexOf("\n", endThink);
            if (after < 0) after = endThink + 11;
            src = src.substring(after + 1);
        } else {
            int startThink = src.indexOf(" thinking");
            if (startThink > 0) {
                int respTag = src.indexOf(" response");
                // Si el tag de cierre aparece antes que el tag de apertura, todo es final
                if (respTag > startThink) {
                    int bodyStart = src.indexOf("\n", respTag);
                    if (bodyStart > 0) src = src.substring(bodyStart + 1);
                }
            }
        }
    }
    String out = "";
    size_t i = 0;
    size_t len = src.length();
    // Prefijos de agente (para detectar tags sin cerrar por truncado).
    auto isAgentTag = [](const String &t) -> bool {
        return t.startsWith("[DRAW:") || t.startsWith("[SKETCH:") || t.startsWith("[MODE:") ||
               t.startsWith("[TEXT:") || t.startsWith("[TOOL:") || t.startsWith("[FACE:") ||
               t.startsWith("[ICON:") || t.startsWith("[REMEMBER:") ||
               t.startsWith("[draw:") || t.startsWith("[sketch:") || t.startsWith("[mode:") ||
               t.startsWith("[text:") || t.startsWith("[tool:") || t.startsWith("[face:") ||
               t.startsWith("[icon:") || t.startsWith("[remember:");
    };
    while (i < len) {
        char c = src.charAt(i);
        // If it is an agent action tool tag like [DRAW:...], [SKETCH:...], [MODE:...], [TEXT:...], [TOOL:...], preserve it!
        if (c == '[') {
            int closeIdx = src.indexOf(']', i);
            if (closeIdx < 0) {
                // Sin cerrar (respuesta truncada): si es markup, amputar el
                // resto (2026-09-10: se vocalizaba el sketch crudo); si no,
                // cortar el corchete suelto y seguir con el texto.
                String tail = src.substring(i);
                if (isAgentTag(tail)) break;
                i++;
                continue;
            }
            {
                String tag = src.substring(i, closeIdx + 1);
                if (tag.startsWith("[DRAW:") || tag.startsWith("[SKETCH:") || tag.startsWith("[MODE:") ||
                    tag.startsWith("[TEXT:") || tag.startsWith("[TOOL:") || tag.startsWith("[FACE:") ||
                    tag.startsWith("[ICON:") || tag.startsWith("[REMEMBER:")) {
                    out += tag;
                    i = closeIdx + 1;
                    continue;
                } else {
                    // General bracket annotation (e.g. [sound effect]), skip
                    i = closeIdx + 1;
                    continue;
                }
            }
        }

        // Remove markdown formatting characters (asterisks, underscores, hashes, backticks, tildes)
        if (c == '*' || c == '_' || c == '#' || c == '`' || c == '~') {
            i++;
            continue;
        }
        // Normalize newlines to spaces
        if (c == '\r' || c == '\n') {
            if (out.length() > 0 && out.charAt(out.length() - 1) != ' ') {
                out += ' ';
            }
            i++;
            continue;
        }
        out += c;
        i++;
    }
    out.trim();
    return out;
}

static size_t readHttpBinaryBody(WiFiClientSecure &cli, bool isChunked, uint8_t **outBuf) {
    size_t capacity = 32 * 1024; // 32KB initial buffer (fits easily in internal RAM or PSRAM)
    uint8_t *buf = (uint8_t *)ps_malloc(capacity);
    if (!buf) {
        buf = (uint8_t *)malloc(capacity);
    }
    if (!buf) {
        CLOGE(TAG, "readHttpBinaryBody: out of memory!");
        return 0;
    }

    size_t totalRead = 0;

    if (isChunked) {
        unsigned long binStart = millis();
        while ((cli.connected() || cli.available()) && (millis() - binStart < 35000)) {
            if (!cli.available()) {
                delay(2);
                continue;
            }
            String hexLine = cli.readStringUntil('\n');
            hexLine.trim();
            if (hexLine.length() == 0) continue;

            long chunkSize = strtol(hexLine.c_str(), NULL, 16);
            if (chunkSize <= 0) break; // 0 chunk indicates end of body

            if (totalRead + chunkSize > capacity) {
                size_t newCap = capacity + max((size_t)16384, (size_t)chunkSize);
                uint8_t *newBuf = (uint8_t *)ps_realloc(buf, newCap);
                if (!newBuf) newBuf = (uint8_t *)realloc(buf, newCap);
                if (!newBuf) {
                    CLOGE(TAG, "readHttpBinaryBody: out of memory on realloc (%u bytes)", newCap);
                    free(buf);
                    return 0;
                }
                buf = newBuf;
                capacity = newCap;
            }

            long remaining = chunkSize;
            unsigned long chunkStart = millis();
            while (remaining > 0 && (cli.connected() || cli.available())) {
                size_t toRead = min((long)1024, remaining);
                size_t bytesRead = cli.readBytes((char *)(buf + totalRead), toRead);
                if (bytesRead > 0) {
                    totalRead += bytesRead;
                    remaining -= bytesRead;
                    chunkStart = millis();
                } else {
                    if (millis() - chunkStart > 12000) { // hotspot: rafagas con pausas largas
                        CLOGW(TAG, "readHttpBinaryBody: chunk read timeout");
                        break;
                    }
                    delay(2);
                }
            }
            if (remaining > 0) break;

            // Consume trailing \r\n after chunk data
            while (cli.available()) {
                char c = cli.read();
                if (c == '\n') break;
            }
        }
    } else {
        unsigned long readStart = millis();
        while (cli.connected() || cli.available()) {
            size_t avail = cli.available();
            if (avail > 0) {
                if (totalRead + avail > capacity) {
                    size_t newCap = capacity + max((size_t)32768, avail);
                    uint8_t *newBuf = (uint8_t *)ps_realloc(buf, newCap);
                    if (!newBuf) newBuf = (uint8_t *)realloc(buf, newCap);
                    if (!newBuf) {
                        CLOGE(TAG, "readHttpBinaryBody: out of memory on realloc");
                        free(buf);
                        return 0;
                    }
                    buf = newBuf;
                    capacity = newCap;
                }
                size_t bytesRead = cli.readBytes((char *)(buf + totalRead), avail);
                totalRead += bytesRead;
                readStart = millis();
            } else {
                if (millis() - readStart > 10000) break; // hotspot: pausas largas
                delay(5);
            }
        }
    }

    *outBuf = buf;
    return totalRead;
}


// === Anti-saturación: validadores de payload antes de enviar nada al DAC ===
// Un body HTTP corrupto (HTML de captcha, JSON de error, WAV con header mal
// salteado) reproducido como PCM crudo suena "todo mal, saturado horrible".
// Estas funciones deciden reproducir / descartar ANTES de abrir el I2S.
static bool looksLikeMp3(const uint8_t *d, size_t n) {
    if (!d || n < 4) return false;
    // ID3v2 tag ("ID3") al inicio del stream de Google Translate TTS
    if (n >= 3 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') return true;
    // Frame sync MPEG: 0xFF seguido de 0xE0..0xFF en los 3 bits altos,
    // buscado en los primeros 256 bytes (permite tags cortos delante)
    size_t scan = (n < 256) ? n - 1 : 256;
    for (size_t i = 0; i < scan; i++) {
        if (d[i] == 0xFF && (d[i + 1] & 0xE0) == 0xE0) return true;
    }
    return false;
}

static bool looksLikeJsonError(const uint8_t *d, size_t n) {
    if (!d || n == 0) return false;
    // Salta BOM UTF-8 y espacios: un '{' inicial es JSON (error), no audio
    size_t i = 0;
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) i = 3;
    while (i < n && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n')) i++;
    if (i >= n) return false;
    if (d[i] == '{' || d[i] == '[') return true;
    // Texto plano de error típico ("ERROR", "<html", "Too Many")
    if (i + 5 <= n && memcmp(d + i, "ERROR", 5) == 0) return true;
    if (i + 5 <= n && memcmp(d + i, "<html", 5) == 0) return true;
    if (i + 5 <= n && memcmp(d + i, "<!DOC", 5) == 0) return true;
    return false;
}

static bool isPlausibleSampleRate(uint32_t hz) {
    return (hz >= 8000 && hz <= 48000);
}


String AIEngine::buildBasePrompt() {
    AppConfig &cfg = netManager.getConfig();
    String basePrompt = (strlen(cfg.system_prompt) > 0) ? String(cfg.system_prompt) : String(GEMINI_DEFAULT_SYSTEM_PROMPT);
    String memCtx = copitoMem.buildContextBlock();
    if (memCtx.length() > 0) {
        basePrompt += memCtx;
    }
    if (cfg.web_search != 0 && !m_inWebSearchRound) {
        basePrompt += " Si te preguntan por datos actuales o algo que no sabes con certeza, emite [TOOL:WEB_SEARCH:consulta corta] y espera los resultados para responder.";
    }
    return basePrompt;
}

bool AIEngine::queryGeminiAudio(const uint8_t *wavData, size_t wavSize, String &outResponseText,
                          const String &preTranscript) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.gemini_api_key) == 0) {
        CLOGE(TAG, "Gemini API key is not configured!");
        outResponseText = "Falta configurar la clave de Gemini.";
        return false;
    }

    CLOGI(TAG, "Connecting to Google Gemini API (%s:443)...", GEMINI_API_HOST);
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    if (!client.connect(GEMINI_API_HOST, GEMINI_API_PORT)) {
        CLOGE(TAG, "Connection to Gemini failed!");
        outResponseText = "Fallo de conexión con Google Gemini.";
        return false;
    }

    g_lastVoiceTranscript = "";
    String voiceTranscript;
    bool haveTranscript = false;
    if (preTranscript.length() > 0) {
        // main.cpp ya transcribio (ruteo de comandos locales): reutilizar.
        voiceTranscript = preTranscript;
        haveTranscript = true;
        g_lastVoiceTranscript = voiceTranscript;
        CLOGI(TAG, "Voz (Whisper reutilizado): '%s'", voiceTranscript.c_str());
    } else {
        haveTranscript = (transcribeGroqWhisper(wavData, wavSize, voiceTranscript) && voiceTranscript.length() > 0);
        if (haveTranscript) {
            g_lastVoiceTranscript = voiceTranscript;
            CLOGI(TAG, "Voz (Whisper): '%s'", voiceTranscript.c_str());
        }
    }

    // 1. Calculate Base64 payload size for WAV data (zero heap allocation!)
    size_t b64Len = 4 * ((wavSize + 2) / 3);

    // 2. Format system prompt safely with JSON escaping (base + memoria + contexto)
    String basePrompt = buildBasePrompt();
    JsonDocument promptDoc;
    promptDoc["p"] = basePrompt;
    String escapedPrompt;
    serializeJson(promptDoc["p"], escapedPrompt);

    // 3. Build JSON Prefix and Suffix with exact Gemini camelCase fields (inlineData, mimeType)
    String prefix = "{\"system_instruction\":{\"parts\":[{\"text\":" + escapedPrompt + "}]},\"contents\":[{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/wav\",\"data\":\"";
    
    String promptInstruction = "Por favor escucha con atencion el audio grabado por el usuario y responde directamente a lo que te dice o pregunta en espanol rioplatense como Kraken: genio enojado pero servicial, trato de respeto, cero cursileria. Cumple el pedido primero, el comentario acido despues. "
        "DIRECTIVAS CRITICAS: "
        "1. Si el usuario te pide dibujar, ilustrar o mostrar un dibujo de algo (ej: gato, casa, cohete, vaca, casita con sol y vaca, luna sonriente), "
        "responde con UNA sola frase corta hablada e incluye OBLIGATORIAMENTE al final el tag [SKETCH: comandos] para dibujarlo en la pantalla monocroma 200x200 (CLEAR; LINE x0 y0 x1 y1 grosor; CIRCLE cx cy r; FCIRCLE cx cy r; RECT x y w h; FRECT x y w h; CURVE x0 y0 cx cy x1 y1; MACRO nombre x y [p1] [p2] con SUN, MOON, MOON_SMILE, HOUSE, COW, STAR, CLOUD, TREE). En LINE y CURVE usa SIEMPRE grosor 2, 3 o 4 (el trazo de 1px no se ve en e-paper). NUNCA digas 'no puedo' ni 'no soy un artista' ni uses [TEXT:...] ni [DRAW:...] cuando dibujes. "
        "2. Si el usuario pide modo reloj, emite [MODE:RELOJ]. "
        "3. Si el usuario pide modo clima, emite [MODE:CLIMA].";
    bool storyMode = false;
    if (haveTranscript) {
        String lowV = voiceTranscript;
        lowV.toLowerCase();
        storyMode = isStoryRequest(musicNorm(voiceTranscript));
        if (storyMode) {
            promptInstruction += STORY_DIRECTIVE;
            CLOGI(TAG, "MODO CUENTO LARGO activado");
        }
        if (lowV.indexOf("dibuja") >= 0 || lowV.indexOf("dibujame") >= 0 || lowV.indexOf("dibujo") >= 0 || lowV.indexOf("ilustra") >= 0 || CopitoTools::hasDrawingIntent(lowV)) {
            promptInstruction += " [RECORDATORIO OBLIGATORIO: El usuario pidio un dibujo explicito. Responde con 1 sola frase corta hablada e incluye OBLIGATORIAMENTE al final [SKETCH: CLEAR; ...] con primitivas vectoriales o macros (LINE, CIRCLE, FCIRCLE, RECT, FRECT, CURVE, MACRO).] ";
        }
        if (lowV.indexOf("reloj") >= 0) {
            promptInstruction += " [RECORDATORIO: El usuario solicita la pantalla o modo reloj. Emite el tag [MODE:RELOJ].]";
        }
        if (lowV.indexOf("clima") >= 0 || lowV.indexOf("temperatura") >= 0) {
            promptInstruction += " [RECORDATORIO: El usuario solicita la pantalla o modo clima. Emite el tag [MODE:CLIMA].]";
        }
    }

    // Build generationConfig from config (max_tokens, temperature10)
    float temperature = (cfg.temperature10 > 0) ? (cfg.temperature10 / 10.0f) : 0.7f;
    uint16_t maxTokens = (cfg.max_tokens >= 80 && cfg.max_tokens <= 2048) ? cfg.max_tokens : 300;
    if (storyMode) maxTokens = 1400; // el cuento largo no entra en 300 tokens
    String suffix = "\"}},{\"text\":\"" + promptInstruction + "\"}]}],\"generationConfig\":{\"temperature\":"
        + String(temperature, 1) + ",\"maxOutputTokens\":" + String(maxTokens) + "}";
    suffix += "}";


    size_t totalContentLength = prefix.length() + b64Len + suffix.length();

    // 4. Send HTTP POST Request
    String modelName = strlen(cfg.gemini_model) > 0 ? String(cfg.gemini_model) : String(GEMINI_DEFAULT_MODEL);
    String endpoint = "/v1beta/models/" + modelName + ":generateContent";

    client.println("POST " + endpoint + " HTTP/1.1");
    client.println("Host: " + String(GEMINI_API_HOST));
    client.println("x-goog-api-key: " + String(cfg.gemini_api_key));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(totalContentLength));
    client.println("Connection: close");
    client.println();

    // 5. Stream JSON payload without allocating huge RAM buffer
    client.print(prefix);
    streamBase64(client, wavData, wavSize);
    client.print(suffix);
    client.flush();

    CLOGI(TAG, "Gemini request sent (%d bytes), waiting for response...", totalContentLength);

    // 6. Wait for response and parse HTTP Status Code
    unsigned long waitStart = millis();
    while (client.connected() && !client.available() && (millis() - waitStart < 20000)) {
        delay(15);
    }

    int statusCode = 0;
    bool isChunked = false;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGE(TAG, "Gemini returned HTTP %d", statusCode);
        String errorBody = client.readString();
        CLOGE(TAG, "Gemini error body: %s", errorBody.c_str());
        client.stop();

        if (statusCode == 503 || statusCode == 429) {
            // Cascada voz: Whisper(Groq) STT -> razonamiento multi-proveedor.
            // Si ya hay transcript (preTranscript o Whisper previo), NO se
            // vuelve a subir el WAV: reutilizar (ahorra una subida lenta en calle).
            // Solo si hay clave de Groq; si no, directo a contingencia.
            String transcript;
            bool haveT = false;
            if (haveTranscript) {
                transcript = voiceTranscript;
                haveT = true;
            } else {
                haveT = (transcribeGroqWhisper(wavData, wavSize, transcript) && transcript.length() > 0);
            }
            if (haveT) {
                CLOGI(TAG, "Voz via Whisper: '%s'", transcript.c_str());
                String answer;
                if (queryTextCascade(transcript, answer) && answer.length() > 0) {
                    outResponseText = answer;
                    return true;
                }
            }
            // Pico de demanda en Google: contingencia sobria y rotativa (5 variantes).
            static int s_fbIdx = 0;
            const char *fb[] = {
                "Google me dejo en espera. Proba de nuevo en un minuto. [DRAW:NUBE]",
                "Hay mucha demanda ahora mismo. Retomamos en un rato. [DRAW:ESTRELLA]",
                "No pude consultar a la IA recien. Tocame de nuevo. [DRAW:SOL]",
                "Se corto la consulta en la nube. Reintentamos. [DRAW:RAYO]",
                "Servidor ocupado. Dame un minuto y hablamos. [DRAW:LUNA]"
            };
            CLOGW(TAG, "Pico de demanda en Gemini (HTTP %d). Activando respuesta de contingencia!", statusCode);
            outResponseText = fb[s_fbIdx % 5];
            s_fbIdx++;
            return true;
        }

        if (statusCode == 400 || statusCode == 403) {
            CLOGW(TAG, "HTTP %d en Gemini. Probando cascada de texto (Clave/modelo o cuota).", statusCode);
            String transcript;
            bool haveT = false;
            if (haveTranscript) {
                transcript = voiceTranscript;
                haveT = true;
            } else {
                haveT = (transcribeGroqWhisper(wavData, wavSize, transcript) && transcript.length() > 0);
            }
            if (haveT) {
                CLOGI(TAG, "Voz via Whisper (reintento backend): '%s'", transcript.c_str());
                String answer;
                if (queryTextCascade(transcript, answer) && answer.length() > 0) {
                    outResponseText = answer;
                    return true;
                }
            }
            outResponseText = "Configure su clave en el portal WiFi.";
            return false;
        } else {
            static int s_fbIdx2 = 0;
            const char *fb2[] = {
                "Algo fallo del lado de la nube. Proba otra vez. [DRAW:NUBE]",
                "No llego la respuesta. Tocame de nuevo. [DRAW:SOL]",
                "Error de conexion con la IA. Reintentamos en un rato. [DRAW:ESTRELLA]",
                "Me quede sin respuesta valida. Decilo de nuevo. [DRAW:LUNA]",
                "Falla tecnica momentanea. Ya vuelvo. [DRAW:MUSICA]"
            };
            outResponseText = fb2[s_fbIdx2 % 5];
            s_fbIdx2++;
            return true;
        }
    }

    // 7. Read Response Body (handling chunked transfer encoding cleanly)
    String responseBody = readHttpBody(client, isChunked);
    client.stop();

    // 8. Parse JSON response
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, responseBody);
    if (err) {
        CLOGE(TAG, "JSON parse error: %s", err.c_str());
        outResponseText = "No entendi lo que dijiste.";
        return false;
    }

    const char *reply = respDoc["candidates"][0]["content"]["parts"][0]["text"];
    if (reply && strlen(reply) > 0) {
        outResponseText = sanitizeTtsText(String(reply));
        CLOGI(TAG, "Gemini reply (sanitized): %s", outResponseText.c_str());

        // Ruta audio: si la IA pidió datos actuales ([TOOL:WEB_SEARCH]),
        // buscar en internet (SearXNG -> DDG) y relanzar con los datos reales.
        resolveWebSearchRound(outResponseText, haveTranscript ? voiceTranscript : "(voz)");
        return true;
    }

    outResponseText = "No tengo una respuesta en este momento.";
    return false;
}


bool AIEngine::queryGeminiText(const String &promptText, String &outResponseText, bool jsonMode, bool noDirectives) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.gemini_api_key) == 0) {
        outResponseText = "Clave de API ausente.";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    if (!client.connect(GEMINI_API_HOST, GEMINI_API_PORT)) {
        return false;
    }

    JsonDocument doc;
    JsonObject sysInst = doc["system_instruction"].to<JsonObject>();
    JsonArray sysParts = sysInst["parts"].to<JsonArray>();
    JsonObject sysPart = sysParts.add<JsonObject>();
    // jsonMode: sistema mínimo. El prompt base (personalidad Copito, tags,
    // sketches) contradice el "SOLO JSON" y el modelo obedece al sistema =
    // prosa en vez de JSON = cascada muerta.
    sysPart["text"] = jsonMode ? String("Respondes unicamente con el JSON pedido, sin texto extra ni markdown.")
                               : buildBasePrompt();

    JsonObject content = doc["contents"].add<JsonObject>();
    JsonArray parts = content["parts"].to<JsonArray>();
    JsonObject promptPart = parts.add<JsonObject>();
    String effectivePrompt = promptText;
    // jsonMode (JSON del cuento): NINGUNA directiva extra. El prompt ya pide
    // SOLO JSON; la de dibujo ("1 frase + SKETCH") y la de cuento largo
    // (prosa + [MODE:CUENTO]) lo saboteaban y volvía prosa = sin fotos.
    // Tampoco dibujo si es pedido de cuento (un "cuento de un conejito" no
    // es un pedido de dibujo aunque mencione animales).
    if (!jsonMode && !noDirectives && !isStoryRequest(musicNorm(promptText)) &&
        CopitoTools::hasDrawingIntent(promptText) && promptText.indexOf("DIRECTIVA OBLIGATORIA DE DIBUJO") < 0) {
        effectivePrompt += " [RECORDATORIO OBLIGATORIO: El usuario pidio un dibujo explicito. Responde con 1 sola frase corta hablada y emite OBLIGATORIAMENTE al final [SKETCH: CLEAR; ...] con primitivas vectoriales (LINE, CIRCLE, FCIRCLE, RECT, FRECT, CURVE) o macros (MACRO SUN/MOON/HOUSE/COW/STAR/CLOUD/TREE).] ";
    }
    bool storyMode = !jsonMode && !noDirectives && isStoryRequest(musicNorm(promptText));
    uint16_t maxTokens = (cfg.max_tokens >= 80 && cfg.max_tokens <= 2048) ? cfg.max_tokens : 300;
    if (jsonMode) {
        maxTokens = 1500; // JSON 3-6 partes (texto+prompts) no entra en 300
    } else if (storyMode) {
        effectivePrompt += STORY_DIRECTIVE;
        maxTokens = 1400; // el cuento largo no entra en 300 tokens
        CLOGI(TAG, "MODO CUENTO LARGO activado (texto)");
    }
    promptPart["text"] = effectivePrompt;

    JsonObject genConfig = doc["generationConfig"].to<JsonObject>();
    genConfig["temperature"] = (cfg.temperature10 > 0) ? (cfg.temperature10 / 10.0f) : 0.7f;
    genConfig["maxOutputTokens"] = maxTokens;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String modelName = strlen(cfg.gemini_model) > 0 ? String(cfg.gemini_model) : String(GEMINI_DEFAULT_MODEL);
    String endpoint = "/v1beta/models/" + modelName + ":generateContent";

    client.println("POST " + endpoint + " HTTP/1.1");
    client.println("Host: " + String(GEMINI_API_HOST));
    client.println("x-goog-api-key: " + String(cfg.gemini_api_key));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(jsonPayload.length()));
    client.println("Connection: close");
    client.println();
    client.print(jsonPayload);

    // Wait for response and parse HTTP Status Code
    unsigned long waitStart = millis();
    while (client.connected() && !client.available() && (millis() - waitStart < 20000)) {
        delay(15);
    }

    int statusCode = 0;
    bool isChunked = false;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGE(TAG, "Gemini Text HTTP error %d", statusCode);
        client.stop();
        return false;
    }

    String responseBody = readHttpBody(client, isChunked);
    client.stop();

    JsonDocument respDoc;
    if (deserializeJson(respDoc, responseBody) == DeserializationError::Ok) {
        const char *reply = respDoc["candidates"][0]["content"]["parts"][0]["text"];
        if (reply && strlen(reply) > 0) {
            outResponseText = sanitizeTtsText(String(reply));
            return true;
        }
    }
    return false;
}

// === Busqueda web propia ===
// La IA emite [TOOL:WEB_SEARCH:consulta]. Buscamos en SearXNG (LAN, JSON, primario);
// si no responde, DuckDuckGo (TLS, HTML scraping, sin API key). Devolvemos un bloque
// de contexto legible para el LLM (titulo - url - snippet), o false si todo fallo.

bool AIEngine::searchSearxng(const String &query, String &outResults) {
    AppConfig &cfg = netManager.getConfig();
    if (cfg.searxng_host[0] == '\0') return false; // sin servidor configurado

    WiFiClient cli; // SearXNG en LAN: HTTP plano, sin TLS
    cli.setTimeout(6);
    CLOGI(TAG, "Busqueda: SearXNG %s:%u q='%s'...", cfg.searxng_host, cfg.searxng_port, query.c_str());
    if (!cli.connect(cfg.searxng_host, cfg.searxng_port ? cfg.searxng_port : (uint16_t)DEFAULT_SEARXNG_PORT)) {
        CLOGW(TAG, "Busqueda: SearXNG no responde en LAN, fallback DuckDuckGo");
        return false;
    }

    String path = String("/search?q=") + urlEncode(query) + "&format=json&language=es";
    cli.println(String("GET ") + path + " HTTP/1.1");
    cli.println(String("Host: ") + cfg.searxng_host);
    cli.println("User-Agent: Copito/1.0 (ESP32-S3)");
    cli.println("Accept: application/json");
    cli.println("Connection: close");
    cli.println();

    unsigned long t0 = millis();
    int status = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 8000)) {
        if (!cli.available()) { delay(2); continue; }
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    if (status != 200) {
        CLOGW(TAG, "Busqueda: SearXNG HTTP %d", status);
        cli.stop();
        return false;
    }
    String body = cli.readString();
    cli.stop();

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        CLOGW(TAG, "Busqueda: JSON SearXNG invalido");
        return false;
    }
    JsonArray results = doc["results"].as<JsonArray>();
    int n = 0;
    for (JsonVariant r : results) {
        if (n >= 5) break;
        const char *title = r["title"] | "";
        const char *url   = r["url"]   | "";
        const char *snip  = r["content"] | "";
        if (strlen(title) == 0) continue;
        String s = String("- ") + title;
        if (strlen(url) > 0) s += String(" | ") + url;
        if (strlen(snip) > 0) s += String(" | ") + snip;
        if (s.length() > 220) s = s.substring(0, 220);
        outResults += s + "\n";
        n++;
    }
    CLOGI(TAG, "Busqueda: SearXNG OK (%d resultados)", n);
    return n > 0;
}

bool AIEngine::searchDuckDuckGo(const String &query, String &outResults) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(8);
    CLOGI(TAG, "Busqueda: DuckDuckGo q='%s'...", query.c_str());
    if (!cli.connect("html.duckduckgo.com", 443)) {
        CLOGW(TAG, "Busqueda: sin conexion a DuckDuckGo");
        return false;
    }

    String q = "q=" + urlEncode(query) + "&kl=es";
    cli.println("POST /html/ HTTP/1.1");
    cli.println("Host: html.duckduckgo.com");
    cli.println("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Copito/1.0");
    cli.println("Content-Type: application/x-www-form-urlencoded");
    cli.println("Content-Length: " + String(q.length()));
    cli.println("Connection: close");
    cli.println();
    cli.print(q);

    unsigned long t0 = millis();
    int status = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 10000)) {
        if (!cli.available()) { delay(2); continue; }
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    if (status != 200) {
        CLOGW(TAG, "Busqueda: DuckDuckGo HTTP %d", status);
        cli.stop();
        return false;
    }

    // Streaming parser: saltea el boilerplate (~8KB de headers/nav) hasta
    // encontrar los bloques de resultado, así cabemos en poca RAM.
    String html;
    int hits = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 15000)) {
        if (!cli.available()) { delay(3); continue; }
        int c = cli.read();
        if (c < 0) continue;
        html += (char)c;
        if (html.length() > 30000) break;   // tope de seguridad
    }
    cli.stop();

    // Extraccion simple: cada <a class="result__a" href="...">TITULO</a>
    // seguido de <a class="result__snippet">TEXTO</a>. Sin libreria HTML.
        int pos = 0;
        while (hits < 5) {
            int t = html.indexOf("result__a", pos);
            if (t < 0 || (size_t)t + 40 > html.length()) break;
            int href = html.indexOf("href=\"", t);
            if (href < 0) { pos = t + 10; continue; }
            href += 6;
            int hrefEnd = html.indexOf('"', href);
            if (hrefEnd < 0) { pos = t + 10; continue; }
            String url = html.substring(href, hrefEnd);

            int gt = html.indexOf('>', t);
            if (gt < 0) { pos = t + 10; continue; }
            int lt = html.indexOf("</a>", gt);
            if (lt < 0) { pos = t + 10; continue; }
        String title = html.substring(gt + 1, lt);
        title.trim();
        // quitar HTML residual basico en el titulo
        int hb = title.indexOf('<');
        if (hb >= 0) title = title.substring(0, hb);
        title.trim();

        size_t s = html.indexOf("result__snippet", lt);
        String snip = "";
        if (s != std::string::npos) {
            size_t gt2 = html.indexOf('>', s);
            size_t lt2 = s + 30; // fallback: no hurgar demasiado lejos
            if (gt2 != std::string::npos) {
                size_t cl2 = html.indexOf("</a>", gt2);
                if (cl2 != std::string::npos) lt2 = cl2;
                snip = html.substring(gt2 + 1, lt2);
                snip.replace("<b>", "");
                snip.replace("</b>", "");
                snip.trim();
            }
        }

        // decodificar el redirect uddg= al URL real
        size_t uddg = url.indexOf("uddg=");
        if (uddg != std::string::npos) {
            String enc = url.substring(uddg + 5);
            int amp = enc.indexOf('&');
            if (amp >= 0) enc = enc.substring(0, amp);
            // decodificar %XX basico
            String dec;
            for (size_t i = 0; i < enc.length(); i++) {
                if (enc.charAt(i) == '%' && i + 2 < enc.length()) {
                    String hexs = enc.substring(i + 1, i + 3);
                    dec += (char)((int)strtol(hexs.c_str(), NULL, 16));
                    i += 2;
                } else if (enc.charAt(i) == '+') {
                    dec += ' ';
                } else {
                    dec += enc.charAt(i);
                }
            }
            url = dec;
        }

        if (title.length() > 0) {
            String s = String("- ") + title;
            if (url.length() > 0) s += String(" | ") + url;
            if (snip.length() > 0) s += String(" | ") + snip;
            if (s.length() > 220) s = s.substring(0, 220);
            outResults += s + "\n";
            hits++;
        }
        pos = lt + 4;
    }
    CLOGI(TAG, "Busqueda: DuckDuckGo OK (%d resultados)", hits);
    return hits > 0;
}

bool AIEngine::performWebSearch(const String &query, String &outContext) {
    if (searchSearxng(query, outContext)) return true;
    if (searchDuckDuckGo(query, outContext)) return true;
    CLOGW(TAG, "Busqueda: fallaron SearXNG y DuckDuckGo");
    return false;
}

// === Cliente PiMusic API (Pi3, HTTP plano en LAN) ===
// Endpoints: GET /api/search?q= (array JSON, tomamos el 1er resultado),
// POST /api/play {"url","title"}, POST /api/control {"action"}, GET /api/status.

// Abre conexion PiMusic: primero el último transporte que funcionó (LAN o
// funnel); el otro queda de fallback. Sin dato previo: LAN primero y funnel
// después (rápido en casa). outFunnel: true si salió por funnel (= link lento:
// rate bajo + prebuffer profundo para que no se corte en el hotspot).
static bool piMusicOpenLan(WiFiClient &plainCli, WiFiClient* &outCli, String &outHost) {
    AppConfig &cfg = netManager.getConfig();
    String lanHost = String(cfg.music_host);
    uint16_t lanPort = cfg.music_port ? cfg.music_port : (uint16_t)DEFAULT_MUSIC_PORT;
    plainCli.setTimeout(6);
    if (plainCli.connect(lanHost.c_str(), lanPort)) {
        outCli = &plainCli;
        outHost = lanHost;
        s_lastGoodTransport = 1;
        return true;
    }
    s_lastLanFailMs = millis();
    return false;
}

static bool piMusicOpenFunnel(WiFiClientSecure &tlsCli, WiFiClient* &outCli, String &outHost) {
    AppConfig &cfg = netManager.getConfig();
    if (cfg.music_pub_host[0] == '\0') return false; // sin fallback configurado
    tlsCli.setInsecure(); // cert Tailscale autofirmado por su CA; sin validacion en ESP32
    tlsCli.setTimeout(15);
    String pubHost = String(cfg.music_pub_host);
    if (tlsCli.connect(pubHost.c_str(), 443)) {
        CLOGI(TAG, "Musica: conectado via funnel %s:443", pubHost.c_str());
        outCli = &tlsCli;
        outHost = pubHost;
        s_lastGoodTransport = 2;
        return true;
    }
    return false;
}

static bool piMusicOpen(WiFiClient &plainCli, WiFiClientSecure &tlsCli,
                        WiFiClient* &outCli, String &outHost, bool *outFunnel = nullptr) {
    AppConfig &cfg = netManager.getConfig();
    if (cfg.music_host[0] == '\0') return false; // comandos de musica apagados
    bool funnel = false;
    bool ok = false;
    if (s_lastGoodTransport == 2) {
        // En la calle: el funnel va primero (ahorra los 6 s del intento LAN).
        ok = piMusicOpenFunnel(tlsCli, outCli, outHost);
        funnel = ok;
        if (!ok && piMusicOpenLan(plainCli, outCli, outHost)) { ok = true; funnel = false; }
    } else {
        ok = piMusicOpenLan(plainCli, outCli, outHost);
        funnel = false;
        if (!ok) {
            CLOGW(TAG, "Musica: LAN no responde, probando funnel publico...");
            ok = piMusicOpenFunnel(tlsCli, outCli, outHost);
            funnel = ok;
        }
    }
    if (!ok) CLOGW(TAG, "Musica: Pi3 inalcanzable (ni LAN ni funnel)");
    if (outFunnel) *outFunnel = funnel;
    return ok;
}

// El funnel publico exige X-Copito-Key; la LAN no. Se manda solo por HTTPS.
static void piMusicAuthHeader(WiFiClient *cli, bool funnel) {
    AppConfig &cfg = netManager.getConfig();
    if (funnel && cfg.funnel_key[0] != '\0')
        cli->println("X-Copito-Key: " + String(cfg.funnel_key));
}

// Lee cabeceras HTTP y devuelve el codigo de estado; deja el stream en el body.
static int piMusicReadStatus(WiFiClient &cli, unsigned long timeoutMs) {
    unsigned long t0 = millis();
    int status = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < timeoutMs)) {
        if (!cli.available()) { delay(2); continue; }
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    return status;
}

// Escapa comillas/barras para armar JSON a mano (los titulos de YouTube traen de todo).
static String jsonEscape(const String &src) {
    String o;
    o.reserve(src.length() + 8);
    for (size_t i = 0; i < src.length(); i++) {
        char c = src.charAt(i);
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += ' ';
        else if (c == '\r') o += ' ';
        else if (c == '\t') o += ' ';
        else o += c;
    }
    return o;
}

bool AIEngine::piMusicSearch(const String &query, String &outTitle, String &outUrl) {
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) {
        CLOGW(TAG, "Musica: Pi3 no responde (search)");
        return false;
    }
    CLOGI(TAG, "Musica: PiMusic %s search '%s'...", host.c_str(), query.c_str());
    String path = String("/api/search?q=") + urlEncode(query);
    cli->println(String("GET ") + path + " HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    piMusicAuthHeader(cli, funnel);
    cli->println("Accept: application/json");
    cli->println("Connection: close");
    cli->println();
    int status = piMusicReadStatus(*cli, 30000); // yt-dlp tarda; funnel suma latencia
    if (status != 200) {
        CLOGW(TAG, "Musica: search HTTP %d", status);
        cli->stop();
        return false;
    }
    String body = cli->readString();
    cli->stop();
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        CLOGW(TAG, "Musica: JSON search invalido");
        return false;
    }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) {
        CLOGW(TAG, "Musica: sin resultados para '%s'", query.c_str());
        return false;
    }
    const char *title = arr[0]["title"] | "";
    const char *url = arr[0]["url"] | "";
    if (strlen(title) == 0 || strlen(url) == 0) return false;
    outTitle = String(title);
    outUrl = String(url);
    CLOGI(TAG, "Musica: hallado '%s'", outTitle.c_str());
    return true;
}

bool AIEngine::piMusicPlay(const String &url, const String &title, String &outPlayedTitle) {
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) {
        CLOGW(TAG, "Musica: Pi3 no responde (play)");
        return false;
    }
    CLOGI(TAG, "Musica: PiMusic %s play '%s'...", host.c_str(), title.c_str());
    String payload = String("{\"url\":\"") + jsonEscape(url) + "\",\"title\":\"" + jsonEscape(title) + "\"}";
    cli->println("POST /api/play HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    piMusicAuthHeader(cli, funnel);
    cli->println("Content-Type: application/json");
    cli->println(String("Content-Length: ") + String(payload.length()));
    cli->println("Connection: close");
    cli->println();
    cli->print(payload);
    int status = piMusicReadStatus(*cli, 30000); // resuelve el stream; funnel suma latencia
    if (status != 200) {
        CLOGW(TAG, "Musica: play HTTP %d", status);
        cli->stop();
        return false;
    }
    String body = cli->readString();
    cli->stop();
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
    const char *t = doc["title"] | "";
    outPlayedTitle = strlen(t) > 0 ? String(t) : title;
    return true;
}

bool AIEngine::piMusicControl(const char *action) {
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) {
        CLOGW(TAG, "Musica: Pi3 no responde (control)");
        return false;
    }
    CLOGI(TAG, "Musica: PiMusic %s control '%s'...", host.c_str(), action);
    String payload = String("{\"action\":\"") + action + "\"}";
    cli->println("POST /api/control HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    piMusicAuthHeader(cli, funnel);
    cli->println("Content-Type: application/json");
    cli->println(String("Content-Length: ") + String(payload.length()));
    cli->println("Connection: close");
    cli->println();
    cli->print(payload);
    int status = piMusicReadStatus(*cli, 12000);
    cli->stop();
    return (status == 200);
}

bool AIEngine::piMusicStatus(String &outState, String &outTrack) {
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) return false;
    cli->println("GET /api/status HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    piMusicAuthHeader(cli, funnel);
    cli->println("Accept: application/json");
    cli->println("Connection: close");
    cli->println();
    int status = piMusicReadStatus(*cli, 12000);
    if (status != 200) { cli->stop(); return false; }
    String body = cli->readString();
    cli->stop();
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
    const char *st = doc["state"] | "";
    const char *tr = doc["current_track"] | "";
    outState = String(st);
    outTrack = String(tr);
    return true;
}

// Batería crítica (<=15% y sin cargador): un stream largo con WiFi+PA hunde
// la LiPo y puede brownoutear a mitad (reboot + riesgo de corrupción). La
// música/radio se rehúsan con mensaje claro; voz y resto siguen andando.
static bool batteryCriticalForStream() {
    BatteryStatus st = batteryMonitor.getStatus();
    return (st.isLow && !st.isCharging);
}

static bool batteryRefuseMusic(String &outSpoken, String &outScreen) {
    if (!batteryCriticalForStream()) return false;
    outSpoken = "Bateria baja, no puedo poner musica. Cargame y probamos.";
    outScreen = "Bateria baja";
    CLOGW(TAG, "Musica/radio rehusada: bateria critica (stream largo = brownout)");
    return true;
}

// Prototipo: la tapa se busca en fondo mientras suena (definida abajo,
// junto al pipeline de logos; los streamers la llaman al arrancar).
static void spawnArtworkTask(const String &query, const String &title, bool radio);

// === Musica EN el ESP32 (Copito como parlante) ===
// Pi3 transcodifica YouTube -> PCM s16le 22050Hz mono (/api/copito_stream);
// aca solo se lee por HTTP y se manda al I2S. Stop: mantener BOOT apretado.
// outTitle devuelve la consulta (para pantalla). Devuelve true si sono algo.
bool AIEngine::streamMusicOnEsp32(const String &query, String &outTitle) {
    if (batteryCriticalForStream()) {
        CLOGW(TAG, "streamMusicOnEsp32 rehusado: bateria critica");
        return false;
    }
    PowerStreamGuard ecoBoost; // plan leve: streaming con radio full
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) {
        CLOGW(TAG, "MusicaESP32: Pi3 inalcanzable");
        return false;
    }
    // Calidad adaptativa (vv_select_route): en funnel (calle/hotspot) 16000Hz
    // + prebuffer 4s + reads 8KB + stall 25s. En LAN, calidad máxima.
    // El flag 'para' NO se limpia acá: lo limpia quien inicia la
    // reproducción explícita (main/apiMusicTask), para no borrar un stop
    // concurrente.
    vv_route_t rt = vv_select_route(0, 1, 0, funnel ? 1 : 0);
    uint32_t rate = rt.rate;
    CLOGI(TAG, "MusicaESP32: stream '%s' via %s @%luHz...", query.c_str(), host.c_str(), (unsigned long)rate);
    String path = String("/api/copito_stream?q=") + urlEncode(query) + "&rate=" + String(rate);
    cli->println(String("GET ") + path + " HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    cli->println("Connection: close");
    cli->println();
    int status = piMusicReadStatus(*cli, 60000); // search + resolve + arranque ffmpeg
    if (status != 200) {
        CLOGW(TAG, "MusicaESP32: stream HTTP %d", status);
        cli->stop();
        return false;
    }
    outTitle = query;
    outTitle.trim();
    if (outTitle.length() > 90) outTitle = outTitle.substring(0, 90);
    g_musicEsp32Title = outTitle;

    // Pre-buffer adaptativo en PSRAM (absorbe el jitter del hotspot + TLS).
    // Sin colchón suficiente el DMA hace underrun = corte.
    size_t preTarget = vv_prebuffer_bytes(rate, funnel ? 1 : 0);
    uint8_t *pre = (uint8_t *)(psramFound() ? ps_malloc(preTarget) : malloc(preTarget));
    if (!pre && preTarget > 45000) {
        preTarget = 45000;
        pre = (uint8_t *)malloc(preTarget);
    }
    size_t preLen = 0;
    if (pre) {
        unsigned long t0 = millis();
        unsigned long preTimeout = funnel ? 20000 : 10000;
        while (preLen < preTarget && (millis() - t0 < preTimeout) && (cli->connected() || cli->available())
               && !g_musicStopRequested) {
            size_t avail = cli->available();
            if (avail == 0) { delay(5); continue; }
            size_t toRead = avail;
            if (toRead > preTarget - preLen) toRead = preTarget - preLen;
            int n = cli->read(pre + preLen, toRead);
            if (n > 0) preLen += n;
            else delay(5);
        }
        CLOGI(TAG, "MusicaESP32: pre-buffer %u bytes (~%lus de audio)",
              (unsigned)preLen, (unsigned long)(preLen / (rate * 2)));
    }
    if (preLen < (size_t)rate * 2 || g_musicStopRequested) { // minimo 1s de audio
        CLOGW(TAG, "MusicaESP32: stream vacio/cortado antes de arrancar");
        if (pre) free(pre);
        cli->stop();
        g_musicEsp32Title = "";
        return false;
    }

    // Velocidad EXACTA: startPlayback aplica el % de TTS (ej 110% = chipmunk).
    // La musica siempre va al rate pedido; se restaura al salir.
    uint8_t savedSpeed = audio.getPlaybackSpeed();
    if (savedSpeed != 100) audio.setPlaybackSpeed(100);
    audio.startPlayback(rate);
    g_musicPlaying = true; // los botones pasan a volumen/frenar
    // La pantalla muestra el titulo APENAS empieza a sonar (antes quedaba
    // en "Buscando..." todo el tema y el nombre aparecia solo al final).
    epd.showMusicScreen(outTitle.c_str(), "SONANDO");
    // La tapa se busca en fondo: cuando llega se suma chiquita al lado.
    spawnArtworkTask(query, outTitle, false);
    audio.writePcmChunk(pre, preLen, 128);
    if (pre) free(pre);
    // Lecturas grandes en heap (vv_select_route: 8KB funnel / 4KB LAN):
    // menos overhead TLS y ráfagas que tapan el jitter del hotspot.
    size_t rdSize = rt.rd_size;
    uint8_t *rbuf = (uint8_t *)malloc(rdSize);
    uint8_t sbuf[2048];
    if (!rbuf) { rbuf = sbuf; rdSize = sizeof(sbuf); }
    unsigned long lastData = millis();
    unsigned long lastPoke = millis();
    unsigned long tStart = millis();
    unsigned long stallMs = rt.stall_ms;
    static const unsigned long MAX_MS = 20 * 60000UL; // cap 20 min por seguridad
    size_t totalBytes = preLen;
    unsigned long bootLowSince = 0; // BOOT: click corto = subir volumen
    while ((cli->connected() || cli->available())) {
        if (g_musicStopRequested) { CLOGI(TAG, "MusicaESP32: stop pedido"); break; }
        if (millis() - tStart > MAX_MS) { CLOGI(TAG, "MusicaESP32: cap 20min"); break; }
        // Stop con BOOT MANTENIDO 800ms (sin bloquear el audio: se sigue
        // alimentando el parlante mientras se mide; el click corto lo usa
        // checkButtonEvents para subir el volumen).
        if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
            if (bootLowSince == 0) bootLowSince = millis();
            else if (millis() - bootLowSince > 800) {
                CLOGI(TAG, "MusicaESP32: stop por BOOT mantenido");
                break;
            }
        } else {
            bootLowSince = 0;
        }
        size_t avail = cli->available();
        if (avail > 0) {
            size_t toRead = avail > rdSize ? rdSize : avail;
            int n = cli->read(rbuf, toRead);
            if (n > 0) {
                // PCM s16le mono -> writePcmChunk lo duplica a stereo (ver impl).
                audio.writePcmChunk(rbuf, n & ~1UL, 128);
                totalBytes += (size_t)(n & ~1UL);
                lastData = millis();
                if (millis() - lastPoke > 25000) { lastPoke = millis(); copitoPokeActivity(); }
                continue;
            }
        }
        if (millis() - lastData > stallMs) {
            CLOGW(TAG, "MusicaESP32: stall sin datos, corto");
            break;
        }
        if (millis() - lastData > 250) {
            // Hambre de red: quanta de silencio de 1KB (el write bloqueante
            // mantiene el ritmo del DMA; sin esto el DMA repite/underrun = corte).
            static uint8_t silenceQ[1024] = {0};
            audio.writePcmChunk(silenceQ, sizeof(silenceQ));
        } else {
            delay(2);
        }
    }
    if (rbuf != sbuf) free(rbuf);
    audio.stopPlayback();
    g_musicPlaying = false; // los botones vuelven a su funcion normal
    if (savedSpeed != 100) audio.setPlaybackSpeed(savedSpeed); // restaurar TTS
    g_musicEsp32Title = "";
    cli->stop();
    CLOGI(TAG, "MusicaESP32: fin (%lus)", (millis() - tStart) / 1000);
    // Retorno honesto: true solo si sonó algo real (>3 s y >1 s de audio).
    // Antes devolvía true siempre y la UI mostraba "♪ título" aunque no sonara.
    return ((millis() - tStart) > 3000 && totalBytes > (size_t)rate * 2);
}

// Stream PCM de URL directa (radios) via Pi3 (/api/copito_stream?url=).
// Misma mecanica que streamMusicOnEsp32: pre-buffer, velocidad exacta,
// stops (BOOT/flag), anti-buzz, poke anti-sleep. Radios muy largas: cap 3h.
// (Temas YouTube usan 20min en streamMusicOnEsp32.)
static const unsigned long MAX_MS_RADIO = 3 * 3600000UL;
bool AIEngine::streamPcmUrlOnEsp32(const String &url, const String &title) {
    if (batteryCriticalForStream()) {
        CLOGW(TAG, "streamPcmUrlOnEsp32 rehusado: bateria critica");
        return false;
    }
    PowerStreamGuard ecoBoost; // plan leve: radio en vivo con radio full
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = nullptr;
    String host;
    bool funnel = false;
    if (!piMusicOpen(plainCli, tlsCli, cli, host, &funnel)) {
        CLOGW(TAG, "MusicaESP32: Pi3 inalcanzable (radio)");
        return false;
    }
    String t = title;
    t.trim();
    if (t.length() == 0) t = "Radio en vivo";
    if (t.length() > 90) t = t.substring(0, 90);
    vv_route_t rt = vv_select_route(0, 1, 0, funnel ? 1 : 0);
    uint32_t rate = rt.rate; // misma calidad adaptativa que musica
    CLOGI(TAG, "MusicaESP32: radio '%s' via %s @%luHz...", t.c_str(), host.c_str(), (unsigned long)rate);
    String path = String("/api/copito_stream?url=") + urlEncode(url) + "&rate=" + String(rate);
    cli->println(String("GET ") + path + " HTTP/1.1");
    cli->println(String("Host: ") + host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    cli->println("Connection: close");
    cli->println();
    int status = piMusicReadStatus(*cli, 45000); // ffmpeg abre el stream (hls/redirects)
    if (status != 200) {
        CLOGW(TAG, "MusicaESP32: radio HTTP %d", status);
        cli->stop();
        return false;
    }
    g_musicEsp32Title = t;

    size_t preTarget = vv_prebuffer_bytes(rate, funnel ? 1 : 0);
    uint8_t *pre = (uint8_t *)(psramFound() ? ps_malloc(preTarget) : malloc(preTarget));
    if (!pre && preTarget > 45000) {
        preTarget = 45000;
        pre = (uint8_t *)malloc(preTarget);
    }
    size_t preLen = 0;
    if (pre) {
        unsigned long t0 = millis();
        unsigned long preTimeout = funnel ? 20000 : 10000;
        while (preLen < preTarget && (millis() - t0 < preTimeout) && (cli->connected() || cli->available())
               && !g_musicStopRequested) {
            size_t avail = cli->available();
            if (avail == 0) { delay(5); continue; }
            size_t toRead = avail;
            if (toRead > preTarget - preLen) toRead = preTarget - preLen;
            int n = cli->read(pre + preLen, toRead);
            if (n > 0) preLen += n;
            else delay(5);
        }
        CLOGI(TAG, "MusicaESP32: radio pre-buffer %u bytes (~%lus)",
              (unsigned)preLen, (unsigned long)(preLen / (rate * 2)));
    }
    if (preLen < (size_t)rate * 2 || g_musicStopRequested) {
        if (pre) free(pre);
        cli->stop();
        g_musicEsp32Title = "";
        return false;
    }
    uint8_t savedSpeed = audio.getPlaybackSpeed();
    if (savedSpeed != 100) audio.setPlaybackSpeed(100);
    audio.startPlayback(rate);
    g_musicPlaying = true; // los botones pasan a volumen/frenar
    epd.showMusicScreen(t.c_str(), "SONANDO");
    spawnArtworkTask(url, t, true); // logo de la radio en fondo (si hay)
    audio.writePcmChunk(pre, preLen, 128);
    if (pre) free(pre);
    copitoPokeActivity();
    size_t rdSize = rt.rd_size;
    uint8_t *rbuf = (uint8_t *)malloc(rdSize);
    uint8_t sbuf[2048];
    if (!rbuf) { rbuf = sbuf; rdSize = sizeof(sbuf); }
    unsigned long lastData = millis();
    unsigned long lastPoke = millis();
    unsigned long tStart = millis();
    unsigned long stallMs = rt.stall_ms;
    size_t totalBytes = preLen;
    unsigned long bootLowSince = 0; // BOOT: click corto = subir volumen
    while ((cli->connected() || cli->available())) {
        if (g_musicStopRequested) break;
        if (millis() - tStart > MAX_MS_RADIO) break;
        if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
            if (bootLowSince == 0) bootLowSince = millis();
            else if (millis() - bootLowSince > 800) break; // BOOT mantenido = frenar
        } else {
            bootLowSince = 0;
        }
        size_t avail = cli->available();
        if (avail > 0) {
            size_t toRead = avail > rdSize ? rdSize : avail;
            int n = cli->read(rbuf, toRead);
            if (n > 0) {
                audio.writePcmChunk(rbuf, n & ~1UL, 128);
                totalBytes += (size_t)(n & ~1UL);
                lastData = millis();
                if (millis() - lastPoke > 25000) { lastPoke = millis(); copitoPokeActivity(); }
                continue;
            }
        }
        if (millis() - lastData > stallMs) break; // stall
        if (millis() - lastData > 250) {
            static uint8_t silenceQ[1024] = {0};
            audio.writePcmChunk(silenceQ, sizeof(silenceQ));
        } else {
            delay(2);
        }
    }
    if (rbuf != sbuf) free(rbuf);
    audio.stopPlayback();
    g_musicPlaying = false; // los botones vuelven a su funcion normal
    if (savedSpeed != 100) audio.setPlaybackSpeed(savedSpeed);
    g_musicEsp32Title = "";
    cli->stop();
    return ((millis() - tStart) > 3000 && totalBytes > (size_t)rate * 2);
}

// Normaliza transcript para matching: minusculas y sin tildes/enes
// (Whisper a veces devuelve "música", a veces "musica").
static String musicNorm(const String &src) {
    String s = src;
    s.toLowerCase();
    s.replace("á", "a"); s.replace("é", "e"); s.replace("í", "i");
    s.replace("ó", "o"); s.replace("ú", "u"); s.replace("ü", "u");
    s.replace("ñ", "n");
    s.trim();
    return s;
}

// El prompt limita todo a 40 palabras: un cuento pedido explícito necesita
// excepción o sale "concedido" y nada más (el modelo obedece lo breve).
static bool isStoryRequest(const String &norm) {
    return (norm.indexOf("cuento") >= 0 || norm.indexOf("cuentame") >= 0 ||
            norm.indexOf("contame") >= 0 || norm.indexOf("historia") >= 0 ||
            norm.indexOf("relato") >= 0 || norm.indexOf("leyenda") >= 0 ||
            norm.indexOf("fabula") >= 0 || norm.indexOf("novela") >= 0);
}

// Versión pública para main.cpp: el pedido explícito de cuento fuerza modo
// CUENTO (nunca dibujo) aunque el texto mencione animales/cosas dibujables.
bool aiIsStoryRequest(const String &text) { return isStoryRequest(musicNorm(text)); }

// (STORY_DIRECTIVE vive arriba, junto a los prototipos: se usa en los
// query_* definidos antes de este punto del archivo.)

// Extrae la consulta que sigue a un prefijo ("musica X" -> "X"). "" si no hay nada util.
// Tolera puntuación pegada al trigger ("dime, X", "pone: X", "decime... X"):
 // Whisper la pone siempre y antes el prefijo con espacio exacto no matcheaba
// (el eco "dime, ..." caía a la IA). Exige bordes de palabra para no comer
// prefijos compartidos ("deci" no muerde "decime", "pon" no muerde "pone").
static bool isCmdAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
static String musicQueryAfter(const String &norm, const char *prefix) {
    String pre = String(prefix);
    pre.trim();
    if (pre.length() == 0) return String();
    int from = 0;
    while (true) {
        int p = norm.indexOf(pre, from);
        if (p < 0) return String();
        bool okL = (p == 0) || !isCmdAlnum(norm.charAt(p - 1));
        size_t e = (size_t)p + pre.length();
        bool okR = (e >= norm.length()) || !isCmdAlnum(norm.charAt(e));
        if (okL && okR) {
            size_t i = e;
            while (i < norm.length() && !isCmdAlnum(norm.charAt(i))) i++;
            String q = norm.substring(i);
            q.trim();
            // Sacar puntuacion colgante y muletillas finales
            while (q.length() > 0) {
                char c = q.charAt(q.length() - 1);
                if (c == '.' || c == ',' || c == '!' || c == '?' || c == ':') q.remove(q.length() - 1);
                else break;
            }
            q.trim();
            if (q.length() < 2) return String();
            if (q.length() > 100) q = q.substring(0, 100);
            return q;
        }
        from = p + 1;
    }
}

// Prefijos del eco ("dime X" repite X). Fuente única: bloque DECIME (portal)
// y main.cpp por voz (staged para BOOT).
static const char *ECHO_PREFIXES[] = {
    "decime ", "dime ", "deci ", "repeti esto ", "repeti lo siguiente ",
    "repeti despues de mi ", "repite esto "
};
static const size_t ECHO_PREFIX_COUNT = sizeof(ECHO_PREFIXES) / sizeof(ECHO_PREFIXES[0]);

bool AIEngine::extractEchoPhrase(const String &transcript, String &outPhrase) {
    outPhrase = "";
    String t = musicNorm(transcript);
    if (t.length() < 3) return false;
    for (size_t i = 0; i < ECHO_PREFIX_COUNT; i++) {
        String echo = musicQueryAfter(t, ECHO_PREFIXES[i]);
        if (echo.length() > 0) {
            outPhrase = echo;
            return true;
        }
    }
    return false;
}

// Comandos de voz que NO pasan por la IA.
// "sona X" / "suena X" -> suena EN el ESP32 (stream PCM del Pi3).
// "musica X" (espanol) -> search + play en los parlantes del Pi3.
// "para la musica" -> stop. "pausa" -> pause toggle. "que esta sonando" -> status.
// outEsp32Query queda con la consulta si hay que stremear en el ESP32 (el caller
// llama a streamMusicOnEsp32); vacia en el resto de los casos.
bool AIEngine::tryLocalMusicCommand(const String &transcript, String &outSpoken, String &outScreen,
                                   String &outEsp32Query) {
    AppConfig &cfg = netManager.getConfig();
    if (cfg.music_host[0] == '\0') return false; // servidor no configurado
    String t = musicNorm(transcript);
    if (t.length() < 3) return false;
    // Whisper suele cerrar con punto ("para."): sacarlo para los matches exactos.
    while (t.length() > 0) {
        char c = t.charAt(t.length() - 1);
        if (c == '.' || c == ',' || c == '!' || c == '?') t.remove(t.length() - 1);
        else break;
    }
    t.trim();
    if (t.length() < 3) return false;

    // --- STOP: "para la musica", "silencio", "callate", "corta la musica" ---
    // Frena TODO: stream en el ESP32 (flag) + mpv en el Pi3 (API).
    if (t.indexOf("para la musica") >= 0 || t.indexOf("parar la musica") >= 0 ||
        t.indexOf("deten la musica") >= 0 || t.indexOf("apaga la musica") >= 0 ||
        t.indexOf("corta la musica") >= 0 || t.indexOf("frena la musica") >= 0 ||
        t == "silencio" || t == "callate" || t == "callate copito" || t == "callate kraken" ||
        t.indexOf("deja de sonar") >= 0 || t == "para" || t == "stop") {
        g_musicStopRequested = true; // corta stream ESP32 si lo hubiera
        bool piOk = piMusicControl("stop");
        bool hadLocal = (g_musicEsp32Title.length() > 0);
        if (piOk || hadLocal) {
            outSpoken = "Listo, música detenida.";
            outScreen = "Música detenida";
        } else {
            outSpoken = "No hay música sonando.";
            outScreen = "Sin música activa";
        }
        return true;
    }

    // --- PAUSA / SEGUIR: "pausa", "pausa la musica", "segui", "continua" ---
    if (t.indexOf("pausa la musica") >= 0 || t == "pausa" || t == "pausar" ||
        t.indexOf("congela la musica") >= 0) {
        if (g_musicEsp32Title.length() > 0) {
            // Stream en vivo sin pausa real: frenar es lo unico posible.
            g_musicStopRequested = true;
            outSpoken = "Freno mi música.";
            outScreen = "Música detenida";
            return true;
        }
        // Pi3 desactivado: sin stream local no hay nada que pausar.
        outSpoken = "No hay música sonando.";
        outScreen = "Sin música activa";
        return true;
    }
    if (t == "segui" || t == "sigue" || t == "continua" || t == "reanuda" ||
        t.indexOf("segui con la musica") >= 0) {
        if (g_musicEsp32Title.length() > 0) {
            outSpoken = String("Ya está sonando: ") + g_musicEsp32Title;
            outScreen = g_musicEsp32Title;
        } else {
            outSpoken = "No hay nada en pausa para seguir.";
            outScreen = "Sin música activa";
        }
        return true;
    }

    // --- QUE SUENA: "que esta sonando", "que suena" ---
    if (t.indexOf("que esta sonando") >= 0 || t.indexOf("que suena") >= 0 ||
        t.indexOf("que musica suena") >= 0 || t.indexOf("que se escucha") >= 0) {
        if (g_musicEsp32Title.length() > 0) {
            outSpoken = String("En mí está sonando: ") + g_musicEsp32Title;
            outScreen = g_musicEsp32Title;
            return true;
        }
        // Pi3 desactivado como parlante: solo importa el stream local.
        outSpoken = "Ahora no suena nada.";
        outScreen = "Sin música activa";
        return true;
    }

    // --- PLAY: "sona X" (EN el ESP32) vs "musica X" (parlantes Pi3) ---
    // OJO: no pisar otros comandos locales (reloj/clima/dibujo/cuento/dormir).
    if (t.indexOf("reloj") >= 0 || t.indexOf("clima") >= 0 ||
        t.indexOf("dibuj") >= 0 || t.indexOf("ilustr") >= 0 ||
        t.indexOf("cuento") >= 0 || t.indexOf("dormi") >= 0 ||
        t.indexOf("hora") >= 0 || t.indexOf("temperatura") >= 0 ||
        t.indexOf("pantalla") >= 0 || t.indexOf("boton") >= 0 ||
        t.indexOf("volumen") >= 0 || t.indexOf("brillo") >= 0) {
        return false; // lo maneja la IA / otros modos
    }
    // "sona X" / "suena X" / "pon X" / "pone X" / "poneme X" = suena EN Kraken
    // (parlante local). Solo "musica X" va al parlante Pi3.
    // Palabra a proposito distinta de "musica" (music/musica se confunden al oido
    // y Whisper los mezcla). Se chequea ANTES que los prefijos del Pi3.
    // OJO orden: "pone "/"poneme " antes que "pon " (prefijo compartido).
    String qe = musicQueryAfter(t, "sona ");
    if (qe.length() == 0) qe = musicQueryAfter(t, "soname ");
    if (qe.length() == 0) qe = musicQueryAfter(t, "pone ");
    if (qe.length() == 0) qe = musicQueryAfter(t, "poneme ");
    if (qe.length() == 0) qe = musicQueryAfter(t, "pon ");
    if (qe.length() == 0 && t.indexOf("que suena") < 0 && t.indexOf("que esta sonando") < 0) {
        qe = musicQueryAfter(t, "suena ");
    }
    if (qe.length() > 0) {
        if (batteryRefuseMusic(outSpoken, outScreen)) { outEsp32Query = ""; return true; }
        outEsp32Query = qe;
        outSpoken = String("Reproduciendo ") + qe + " en Kraken.";
        outScreen = String("♪ ") + qe;
        CLOGI(TAG, "Musica: target ESP32 q='%s'", qe.c_str());
        return true;
    }
    static const char *PLAY_PREFIXES[] = {
        "musica ", "poneme ", "reproducime ", "pasame ",
        "quiero escuchar ", "quiero oir ", "pone ", "pon ",
        "reproduci ", "reproduce ", "toca "
    };
    String q;
    for (size_t i = 0; i < sizeof(PLAY_PREFIXES) / sizeof(PLAY_PREFIXES[0]); i++) {
        q = musicQueryAfter(t, PLAY_PREFIXES[i]);
        if (q.length() > 0) break;
    }
    if (q.length() == 0) return false; // "musica" pelado u otro tema: va a la IA

    // Parlante Pi3 DESACTIVADO por pedido de Jorge: TODO suena EN Kraken (local).
    // El Pi3 solo transcodifica (endpoint /api/copito_stream); jamas reproduce.
    if (batteryRefuseMusic(outSpoken, outScreen)) { outEsp32Query = ""; return true; }
    outEsp32Query = q;
    {
        String short_ = q;
        if (short_.length() > 90) short_ = short_.substring(0, 90);
        outSpoken = String("Reproduciendo ") + short_ + " en Kraken.";
        outScreen = String("♪ ") + short_;
    }
    CLOGI(TAG, "Musica: target ESP32 q='%s' (Pi3 solo transcodifica)", q.c_str());
    return true;
}

// ============================================================================
// Comandos de sistema SIN IA (todo local en el ESP32: habla/pantalla propias,
// temporizador con tarea; radio via Pi3 pero sonando en Copito).
// ============================================================================

struct RadioPreset { const char *key; const char *name; const char *url; };
static const RadioPreset RADIO_PRESETS[] = {
    {"mega", "Mega 98.3", "https://mega.stweb.tv/mega983/live/playlist.m3u8"},
    {"98.3", "Mega 98.3", "https://mega.stweb.tv/mega983/live/playlist.m3u8"},
    {"98,3", "Mega 98.3", "https://mega.stweb.tv/mega983/live/playlist.m3u8"},
    {"aspen", "Aspen 102.3", "https://playerservices.streamtheworld.com/api/livestream-redirect/ASPEN.mp3"},
    {"102.3", "Aspen 102.3", "https://playerservices.streamtheworld.com/api/livestream-redirect/ASPEN.mp3"},
    {"urbana", "Urbana Play", "https://cdn.instream.audio:9660/stream"},
};
static const int RADIO_PRESET_COUNT = sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]);

// Primer entero en el texto, clampado; dflt si no hay.
static int parseFirstInt(const String &s, int lo, int hi, int dflt) {
    int n = -1;
    for (size_t i = 0; i < s.length(); i++) {
        if (isdigit((unsigned char)s.charAt(i))) {
            int v = 0;
            while (i < s.length() && isdigit((unsigned char)s.charAt(i))) {
                v = v * 10 + (s.charAt(i) - '0');
                i++;
            }
            n = v;
            break;
        }
    }
    if (n < 0) return dflt;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    return n;
}

// Numero 0..59 en palabras (hora en femenino se ajusta aparte).
static const char *ES_NUM[] = {"cero","uno","dos","tres","cuatro","cinco","seis","siete",
    "ocho","nueve","diez","once","doce","trece","catorce","quince","dieciseis",
    "diecisiete","dieciocho","diecinueve","veinte","veintiuno","veintidos",
    "veintitres","veinticuatro","veinticinco","veintiseis","veintisiete",
    "veintiocho","veintinueve"};
static String numWordEs(int n) {
    if (n < 0) n = 0;
    if (n > 59) n = 59;
    if (n <= 29) return String(ES_NUM[n]);
    static const char *tens[] = {"", "", "", "treinta", "cuarenta", "cincuenta"};
    String s = tens[n / 10];
    if (n % 10) { s += " y "; s += ES_NUM[n % 10]; }
    return s;
}
static String timeWordsSa(int h, int m) { // h 0..23
    String hs = (h == 1) ? "una" : (h == 21 ? "veintiuna" : numWordEs(h));
    String s = (h == 1) ? (String("Es la ") + hs) : (String("Son las ") + hs);
    if (m == 0) s += " en punto";
    else { s += " y "; s += numWordEs(m); }
    return s + ".";
}

// --- Temporizador local: tarea de fondo, cuenta regresiva apta e-paper ---
static TaskHandle_t s_timerTask = NULL;
static volatile bool s_timerCancel = false;
static volatile uint32_t s_timerEndMs = 0;
static volatile uint32_t s_timerTotalS = 0;

static void localTimerTask(void *pv) {
    (void)pv;
    unsigned long endMs = s_timerEndMs;
    int shown = -1;
    int fullCnt = 0;
    while (!s_timerCancel) {
        long remainMs = (long)(endMs - millis());
        if (remainMs <= 0) break;
        copitoPokeActivity(); // no dormir a mitad del conteo
        int rs = (int)((remainMs + 500) / 1000);
        if (rs != shown) {
            shown = rs;
            char b[16];
            if (rs >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", rs / 3600, (rs % 3600) / 60, rs % 60);
            else snprintf(b, sizeof b, "%02d:%02d", rs / 60, rs % 60);
            // Full refresh cada 4 updates para limpiar ghosting del parcial
            epd.showFallbackText(0, b, (fullCnt++ % 4) == 0);
        }
        // Esperar en rebanadas para captar cancel/BOOT rapido
        for (int i = 0; i < 30 && !s_timerCancel; i++) {
            if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
                delay(80);
                if (digitalRead(PIN_BOOT_BUTTON) == LOW) { s_timerCancel = true; break; }
            }
            delay(100);
        }
    }
    bool cancelled = s_timerCancel;
    s_timerTask = NULL;
    if (!cancelled) {
        epd.showFallbackText(0, "TIEMPO!", true);
        audio.playChimeUp(); delay(500);
        audio.playChimeUp(); delay(500);
        audio.playChimeUp();
        aiEngine.streamTtsToSpeaker("Se cumplio el temporizador.", nullptr);
    } else {
        epd.showFallbackText(0, "Timer cancelado", true);
    }
    vTaskDelete(NULL);
}

void AIEngine::startLocalTimer(uint32_t seconds, const String &label) {
    (void)label;
    if (seconds < 5) seconds = 5;
    if (seconds > 6 * 3600) seconds = 6 * 3600;
    if (s_timerTask) { s_timerCancel = true; delay(400); s_timerTask = NULL; }
    s_timerCancel = false;
    s_timerTotalS = seconds;
    s_timerEndMs = millis() + seconds * 1000UL;
    xTaskCreatePinnedToCore(localTimerTask, "copito_timer", 20480, NULL, 2, &s_timerTask, 1);
    CLOGI(TAG, "Timer local %lus en marcha", seconds);
}

void AIEngine::cancelLocalTimer() { s_timerCancel = true; }
bool AIEngine::localTimerActive() { return s_timerTask != NULL; }
String AIEngine::localTimerRemaining() {
    if (!s_timerTask) return String();
    long ms = (long)(s_timerEndMs - millis());
    if (ms < 0) ms = 0;
    int s = (int)(ms / 1000);
    char b[24];
    if (s >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    else snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60);
    return String(b);
}

// Duracion para temporizador: digitos o palabras; "media hora"/"una hora".
static uint32_t parseTimerSeconds(const String &t) {
    if (t.indexOf("media hora") >= 0) return 1800;
    if (t.indexOf("una hora") >= 0) return 3600;
    int n = parseFirstInt(t, 0, 9999, -1);
    if (n < 0) {
        static const char *W[] = {"uno","dos","tres","cuatro","cinco","seis","siete","ocho",
            "nueve","diez","once","doce","trece","catorce","quince","veinte","treinta",
            "cuarenta","cincuenta","sesenta"};
        static const int V[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,20,30,40,50,60};
        for (size_t k = 0; k < 20; k++) {
            if (t.indexOf(W[k]) >= 0) { n = V[k]; break; }
        }
    }
    if (n < 0) return 0;
    if (t.indexOf("hora") >= 0) return (uint32_t)n * 3600;
    if (t.indexOf("seg") >= 0) return (uint32_t)n;
    return (uint32_t)n * 60; // default minutos ("temporizador 10")
}

static String fmtDurWords(uint32_t sec) {
    if (sec >= 3600) {
        int h = sec / 3600, m = (sec % 3600) / 60;
        String s = (h == 1) ? "una hora" : numWordEs(h) + " horas";
        if (m > 0) { s += " y "; s += numWordEs(m); s += (m == 1) ? " minuto" : " minutos"; }
        return s;
    }
    if (sec >= 60) {
        int m = sec / 60;
        return (m == 1) ? "un minuto" : numWordEs(m) + " minutos";
    }
    return (sec == 1) ? "un segundo" : numWordEs((int)sec) + " segundos";
}

bool AIEngine::tryLocalSystemCommand(const String &transcript, String &outSpoken, String &outScreen,
                                    String &outStreamUrl, String &outStreamTitle) {
    AppConfig &cfg = netManager.getConfig();
    String t = musicNorm(transcript);
    if (t.length() < 3) return false;
    while (t.length() > 0) {
        char c = t.charAt(t.length() - 1);
        if (c == '.' || c == ',' || c == '!' || c == '?') t.remove(t.length() - 1);
        else break;
    }
    t.trim();
    if (t.length() < 3) return false;

    // --- CANCELAR TIMER (antes que el parse de duracion) ---
    if ((t.indexOf("cancel") >= 0 || t.indexOf("apaga") >= 0 || t.indexOf("deten") >= 0 ||
         t.indexOf("para el") >= 0 || t.indexOf("frena") >= 0) &&
        (t.indexOf("temporizador") >= 0 || t.indexOf("timer") >= 0 || t.indexOf("alarma") >= 0)) {
        if (localTimerActive()) {
            cancelLocalTimer();
            outSpoken = "Temporizador cancelado.";
            outScreen = "Timer cancelado";
        } else {
            outSpoken = "No hay ningún temporizador activo.";
            outScreen = "Sin timer";
        }
        return true;
    }

    // --- CUANTO FALTA ---
    if (t.indexOf("cuanto falta") >= 0 || t.indexOf("cuanto queda") >= 0 ||
        t.indexOf("cuanto tiempo") >= 0 || t.indexOf("cuanto le queda") >= 0) {
        if (localTimerActive()) {
            String r = localTimerRemaining();
            outSpoken = String("Faltan ") + r + ".";
            outScreen = r;
        } else {
            outSpoken = "No hay ningún temporizador activo.";
            outScreen = "Sin timer";
        }
        return true;
    }

    // --- RADIO (presets verificados; suenan EN Copito via PCM) ---
    // Requiere contexto de radio/emisora para no pisar "musica urbana" (genero -> Pi3/IA).
    {
        bool radioCtx = (t.indexOf("radio") >= 0 || t.indexOf("emisora") >= 0 ||
                         t.indexOf("sintoniza") >= 0 || t.indexOf("sintonia") >= 0 ||
                         t.indexOf("pone ") >= 0 || t.indexOf("pon ") >= 0 ||
                         t.indexOf("sona ") >= 0 || t.indexOf("suena ") >= 0 ||
                         t.indexOf("escuchar") >= 0 || t.indexOf("pasame") >= 0);
        if (radioCtx) {
            for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
                if (t.indexOf(RADIO_PRESETS[i].key) >= 0) {
                    if (batteryCriticalForStream()) {
                        outStreamUrl = "";
                        outStreamTitle = "";
                        outSpoken = "Bateria baja, no puedo sintonizar. Cargame y probamos.";
                        outScreen = "Bateria baja";
                        CLOGW(TAG, "Radio rehusada: bateria critica");
                        return true;
                    }
                    outStreamUrl = String(RADIO_PRESETS[i].url);
                    outStreamTitle = String(RADIO_PRESETS[i].name);
                    outSpoken = String("Sintonizando ") + outStreamTitle + ".";
                    outScreen = String("♪ ") + outStreamTitle;
                    CLOGI(TAG, "Radio local: %s", outStreamTitle.c_str());
                    return true;
                }
            }
        }
    }

    // --- VOLUMEN (0-100, +persistencia NVS) ---
    if (t.indexOf("volumen") >= 0 || t.indexOf("volume") >= 0) {
        int v = (int)cfg.volume;
        int num = parseFirstInt(t, 0, 100, -1);
        if (num >= 0) v = num;
        else if (t.indexOf("subi") >= 0 || t.indexOf("aumenta") >= 0 || t.indexOf("mas ") >= 0 ||
                 t.indexOf("alta") >= 0 || t.indexOf("fuerte") >= 0) v += 15;
        else if (t.indexOf("baja") >= 0 || t.indexOf("menos ") >= 0 || t.indexOf("disminu") >= 0 ||
                 t.indexOf("suave") >= 0 || t.indexOf("bajo") >= 0) v -= 15;
        else if (t.indexOf("maximo") >= 0 || t.indexOf("toda") >= 0) v = 100;
        else if (t.indexOf("minimo") >= 0 || t.indexOf("mute") >= 0 || t.indexOf("cero") >= 0) v = 0;
        else {
            outSpoken = String("Volumen al ") + String(v) + " por ciento.";
            outScreen = String("Vol ") + String(v) + "%";
            return true;
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        audio.setVolume((uint8_t)v);
        cfg.volume = (uint8_t)v;
        Preferences pr;
        pr.begin("assistant", false);
        pr.putUChar("volume", (uint8_t)v);
        pr.end();
        outSpoken = String("Volumen al ") + String(v) + " por ciento.";
        outScreen = String("Vol ") + String(v) + "%";
        CLOGI(TAG, "Volumen local -> %d", v);
        return true;
    }

    // --- HORA (RTC/sistema, sin IA) ---
    // "dime/decime la hora" NO entran acá: la familia dime/decime es solo eco
    // (lo guarda y BOOT lo dice). La hora se pide con "que hora es", etc.
    if (t.indexOf("que hora es") >= 0 || t.indexOf("que hora son") >= 0 ||
        t.indexOf("que hora tenes") >= 0 || t.indexOf("que hora tienes") >= 0 ||
        t == "hora" || t == "la hora") {
        struct tm ti;
        memset(&ti, 0, sizeof(ti));
        bool okT = getLocalTime(&ti, 1500);
        if (!okT) okT = rtcDriver.readTime(ti);
        if (!okT) {
            outSpoken = "No tengo la hora sincronizada todavía.";
            outScreen = "Sin hora";
            return true;
        }
        String said = timeWordsSa(ti.tm_hour, ti.tm_min);
        outSpoken = said;
        char b[8];
        snprintf(b, sizeof b, "%02d:%02d", ti.tm_hour, ti.tm_min);
        outScreen = String(b);
        CLOGI(TAG, "Hora local: %s", said.c_str());
        return true;
    }

    // --- REPETI (re-habla lo ultimo dicho por Kraken) ---
    if (t.indexOf("repeti") >= 0 || t.indexOf("repetilo") >= 0 || t.indexOf("repite") >= 0 ||
        t.indexOf("decilo de nuevo") >= 0 || t.indexOf("decimelo de nuevo") >= 0 ||
        t.indexOf("que dijiste") >= 0 || t.indexOf("como era") >= 0 || t.indexOf("como dijiste") >= 0) {
        if (g_lastSpoken.length() > 0) {
            outSpoken = g_lastSpoken;
            outScreen = g_lastSpoken.length() > 90 ? g_lastSpoken.substring(0, 90) : g_lastSpoken;
        } else {
            outSpoken = "Todavía no dije nada para repetir.";
            outScreen = "Nada que repetir";
        }
        return true;
    }

    // --- DECIME (repite textual lo que le dictaste: "decime hola mundo") ---
    // Va DESPUES de hora/repeti para no pisar "decime la hora" ni
    // "decimelo de nuevo". Sale en minusculas porque el transcript se
    // normaliza para el matcheo (se pronuncia igual).
    // Por voz main.cpp lo intercepta antes (staged: BOOT lo dice); acá queda
    // para el portal, que habla en el acto.
    {
        String echo;
        if (extractEchoPhrase(transcript, echo)) {
            if (echo.length() > 220) echo = echo.substring(0, 220);
            outSpoken = echo;
            outScreen = echo.length() > 90 ? echo.substring(0, 90) : echo;
            CLOGI(TAG, "Decime (eco textual): '%s'", echo.c_str());
            return true;
        }
    }

    // --- CARA / PACK ("cara kraken", "ponete la cara de copito", "cara ruda") ---
    // Cambia el pack visual al vuelo SOLO por la sesión (sin persistir en NVS:
    // un transcript mal oído dejaba Kraken/Rudo PARA SIEMPRE y la pantalla
    // mezclaba caras vieja+nueva por falta de redibujo). Copito (Clásico) es
    // el default pegajoso: sobrevive reboot. El portal /api/face_pack sí persiste.
    if (t.indexOf("cara") >= 0 || t.indexOf("pack") >= 0 || t.indexOf("skin") >= 0 ||
        t.indexOf("rostro") >= 0 || t.indexOf("mascara") >= 0) {
        int pack = -1;
        const char *packName = nullptr;
        if (t.indexOf("kraken") >= 0) {
            pack = FACE_PACK_KRAKEN; packName = "Kraken";
        } else if (t.indexOf("copito") >= 0 || t.indexOf("clasico") >= 0 ||
                   t.indexOf("tierno") >= 0 || t.indexOf("cute") >= 0) {
            pack = FACE_PACK_CLASICO; packName = "Copito";
        } else if (t.indexOf("rudo") >= 0 || t.indexOf("habano") >= 0 ||
                   t.indexOf("badass") >= 0 || t.indexOf("canchero") >= 0) {
            pack = FACE_PACK_RUDO; packName = "Rudo";
        } else if (t.indexOf("maiden") >= 0 || t.indexOf("femenino") >= 0 ||
                   t.indexOf("femenina") >= 0 || t.indexOf("monio") >= 0) {
            pack = FACE_PACK_FEMENINO; packName = "Maiden";
        } else if (t.indexOf("geek") >= 0 || t.indexOf("gamer") >= 0 ||
                   t.indexOf("gafas") >= 0 || t.indexOf("8-bit") >= 0) {
            pack = FACE_PACK_GEEK; packName = "Geek";
        }
        if (pack >= 0) {
            g_currentFacePack = (uint8_t)pack; // extern de faces_bitmaps.h via epaper_driver.h
            cfg.face_pack = (uint8_t)pack;
            // Sin putUChar: la voz NO persiste (ver comentario arriba). El portal sí.
            epd.showFace(FACE_IDLE, 0, true); // FULL ya: sin mezcla vieja+nueva
            if (pack == FACE_PACK_KRAKEN) {
                outSpoken = "Cara de Kraken. Como debe ser.";
                outScreen = "KRAKEN AI";
            } else if (pack == FACE_PACK_CLASICO) {
                outSpoken = "Cara de Copito. Tierno... como vos quieras.";
                outScreen = "COPITO";
            } else {
                outSpoken = String("Cara de ") + packName + ".";
                outScreen = packName;
            }
            CLOGI(TAG, "Pack visual local -> %s (%d)", packName, pack);
            return true;
        }
        return false; // dijo "cara" pero sin pack reconocible: que responda la IA
    }

    // --- ACTUALIZACION ("busca actualizacion", "actualizate", "hay update") ---
    // Pull-OTA bajo demanda: puede tardar minutos por funnel (bloquea la voz).
    if (t.indexOf("actualiz") >= 0 || t.indexOf("update") >= 0 || t.indexOf("firmware nuevo") >= 0) {
        outScreen = "Buscando update";
        int r = netManager.pollStagedFirmware();
        if (r == 1) {
            outSpoken = "Actualizando, reinicio.";
            outScreen = "Flasheando...";
        } else if (r == 2) {
            outSpoken = "Falló la actualización.";
            outScreen = "Update fallido";
        } else {
            outSpoken = "Estoy al día, no hay actualización.";
            outScreen = "Sin updates";
        }
        CLOGI(TAG, "Pull-OTA por voz -> %d", r);
        return true;
    }

    // --- TEMPORIZADOR ("temporizador 10 min", "timer 30 segundos") ---
    if (t.indexOf("temporizador") >= 0 || t.indexOf("timer") >= 0 || t.indexOf("temporisa") >= 0) {
        uint32_t sec = parseTimerSeconds(t);
        if (sec == 0) return false; // sin duracion: que pregunte la IA
        String words = fmtDurWords(sec);
        startLocalTimer(sec, words);
        outSpoken = String("Temporizador de ") + words + " en marcha.";
        outScreen = words;
        return true;
    }

    return false;
}

// Deteccion lexica de "pedido de datos en internet" aunque la IA no emita el tag.
// Devuelve una consulta corta para buscar, o cadena vacia si no hay intencion.
// NOTA: se evalua en minusculas; por eso los acentos/caracteres locales importan.
static const char *WEB_INTENT_KEYWORDS[] = {
    "noticia", "noticias", "actualidad", "ultima hora", "ultimas",
    "dolar", "dólar", "euro", "cotizacion", "cotización", "precio", "precios", "peso",
    "clima ", "pronostico", "pronóstico", "lluvia", "temperatura en",
    "partido", "resultado", "resultados", "fixture", "ganador", "copa",
    "busca", "buscar", "buscame", "búscame", "busques", "averigua", "averiguar",
    "internet", "la web", "en google", "en linea", "online",
    "quien gano", "quien ganó", "quien es", "quien esta", "de hoy", "hoy "
};
static const int WEB_INTENT_KEYWORD_COUNT =
    sizeof(WEB_INTENT_KEYWORDS) / sizeof(WEB_INTENT_KEYWORDS[0]);

static String detectWebSearchIntent(const String &userText) {
    String low = userText;
    low.toLowerCase();
    if (low.length() < 3) return String();
    for (int i = 0; i < WEB_INTENT_KEYWORD_COUNT; i++) {
        if (low.indexOf(WEB_INTENT_KEYWORDS[i]) >= 0) {
            // Reconocer "busca/noticias/..." -> usar el texto del usuario como consulta
            // (recortada para no mandar frases enormes como query).
            String q = userText;
            q.trim();
            if (q.length() > 120) q = q.substring(0, 120);
            return q;
        }
    }
    return String();
}

// Deteccion del tag [TOOL:WEB_SEARCH:consulta] (puede haber 1 o varios).
// Si lo hay, buscamos cada consulta, inyectamos los resultados y relanzamos
// la pregunta al mismo flujo (cascada) para la respuesta final real.
bool AIEngine::resolveWebSearchRound(String &ioText, const String &originalUserPrompt) {
    // Guarda anti-recursion: durante la 2da ronda NO se resuelve otro tag.
    if (m_inWebSearchRound) return false;
    AppConfig &cfg = netManager.getConfig();

    String lower = ioText;
    lower.toLowerCase();
    int idx = lower.indexOf("[tool:web_search:");
    bool hadTag = (idx >= 0);

    // Recoger todas las consultas del texto (por si la IA emite varias).
    std::vector<String> queries;
    if (hadTag) {
        size_t scan = 0;
        while (true) {
            size_t f = lower.indexOf("[tool:web_search:", scan);
            if (f == std::string::npos) break;
            size_t close = ioText.indexOf("]", f);
            if (close == std::string::npos) break;
            String q = ioText.substring(f + 17, close);
            q.trim();
            if (q.length() > 0) queries.push_back(q);
            scan = close + 1;
        }
    }

    // Fallback heurístico: aunque la IA no emita el tag, si el usuario pidió
    // datos actuales (noticias, dólar, clima, "buscame", internet, ...) buscamos igual.
    bool heuristic = false;
    if (!hadTag && cfg.web_search != 0 && !m_inWebSearchRound) {
        String intent = detectWebSearchIntent(originalUserPrompt);
        if (intent.length() > 0) {
            CLOGI(TAG, "WebSearch heurística: intencion detectada en '%s' -> '%s'",
                  originalUserPrompt.c_str(), intent.c_str());
            queries.push_back(intent);
            heuristic = true;
        }
    }

    if (queries.empty()) return false;

    // Primero limpiar todos los tags del texto (para que la respuesta final
    // no arrastre ruido si la 2da ronda falla).
    while (true) {
        int s = ioText.indexOf("[TOOL:WEB_SEARCH:");
        int s2 = ioText.indexOf("[tool:web_search:");
        int start = (s >= 0 && (s2 < 0 || s < s2)) ? s : s2;
        if (start < 0) break;
        int close = ioText.indexOf(']', start);
        if (close < 0) break;
        ioText.remove(start, close - start + 1);
        lower = ioText;
        lower.toLowerCase();
    }
    ioText.trim();

    String context = "";
    for (size_t i = 0; i < queries.size(); i++) {
        String c;
        if (performWebSearch(queries[i], c)) {
            context += c;
        }
    }
    context.trim();

    if (context.length() == 0) {
        if (hadTag) {
            // Con tag: reemplazar el texto por una nota honesta para el TTS
            // (la IA dependía de datos que no pudimos obtener).
            ioText = "No pude encontrar datos actuales sobre eso en internet. Si no tenes el dato, decilo directo sin inventar. [DRAW:NUBE]";
            return true;
        }
        // Heurística sin tag: conservar la respuesta original del modelo;
        // simplemente no forzamos la búsqueda si esta falló.
        return false;
    }

    // Inyectar el contexto como si fuera un turno de usuario adicional: la IA
    // debe redactar su respuesta final basandose SOLO en estos datos.
    String followUp = "[CONTEXTO DE BUSQUEDA WEB, responde tus preguntas con estos datos reales, citando la fuente]:\n"
                      + context + "\n\nPregunta original: " + originalUserPrompt;
    m_inWebSearchRound = true; // evita que la IA vuelva a pedir [TOOL:WEB_SEARCH]
    String second;
    bool ok = queryTextCascade(followUp, second);
    m_inWebSearchRound = false;
    if (ok && second.length() > 0) {
        ioText = second;
    }
    return true;
}

// Override de max_tokens para chatOpenAI (el JSON del cuento necesita ~1500;
// el default 300 lo trunca = "respuesta sin JSON" eterna en los fallbacks).
static uint16_t s_chatTokensOverride = 0;
struct ChatTokensGuard {
    uint16_t prev;
    ChatTokensGuard(uint16_t v) : prev(s_chatTokensOverride) { s_chatTokensOverride = v; }
    ~ChatTokensGuard() { s_chatTokensOverride = prev; }
};

// === Chat estilo OpenAI: un solo POST/parse para Groq, Cerebras, Pollinations y Ollamas ===
bool AIEngine::chatOpenAI(const ChatEndpoint &ep, const String &systemPrompt, const String &userText, String &outReply) {
    AppConfig &cfg = netManager.getConfig();
    if (!ep.host || !ep.host[0] || ep.port == 0 || !ep.model || !ep.model[0]) return false;

    WiFiClientSecure tlsCli;
    WiFiClient plainCli;
    WiFiClient *cli;
    if (ep.tls) {
        tlsCli.setInsecure();
        tlsCli.setTimeout(25);
        cli = &tlsCli;
    } else {
        plainCli.setTimeout(30);
        cli = &plainCli;
    }

    CLOGI(TAG, "IA [%s]: %s:%u modelo %s...", ep.label, ep.host, ep.port, ep.model);
    if (!cli->connect(ep.host, ep.port)) {
        CLOGW(TAG, "IA [%s]: sin conexion, siguiente", ep.label);
        return false;
    }

    JsonDocument doc;
    doc["model"] = ep.model;
    JsonArray msgs = doc["messages"].to<JsonArray>();
    JsonObject sysM = msgs.add<JsonObject>();
    sysM["role"] = "system";
    sysM["content"] = systemPrompt;
    JsonObject usrM = msgs.add<JsonObject>();
    usrM["role"] = "user";
    usrM["content"] = userText;
    doc["temperature"] = (cfg.temperature10 > 0) ? (cfg.temperature10 / 10.0f) : 0.7f;
    uint16_t chatTokens = s_chatTokensOverride ? s_chatTokensOverride
        : ((cfg.max_tokens >= 80 && cfg.max_tokens <= 2048) ? cfg.max_tokens : 800);
    doc["max_tokens"] = chatTokens;
    String body;
    serializeJson(doc, body);

    cli->println(String("POST ") + ep.path + " HTTP/1.1");
    cli->println(String("Host: ") + ep.host);
    cli->println("User-Agent: Copito/1.0 (ESP32-S3)");
    cli->println("Content-Type: application/json");
    if (ep.apiKey && ep.apiKey[0]) {
        cli->println(String("Authorization: Bearer ") + ep.apiKey);
    }
    cli->println(String("Content-Length: ") + String(body.length()));
    cli->println("Connection: close");
    cli->println();
    cli->print(body);

    unsigned long waitStart = millis();
    while (cli->connected() && !cli->available() && (millis() - waitStart < ep.timeoutMs)) {
        delay(15);
    }

    int statusCode = 0;
    bool isChunked = false;
    while (cli->connected() || cli->available()) {
        String line = cli->readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) statusCode = line.substring(sp + 1, sp + 4).toInt();
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGW(TAG, "IA [%s]: HTTP %d, siguiente", ep.label, statusCode);
        cli->stop();
        return false;
    }

    String responseBody = readHttpBody(*cli, isChunked);
    cli->stop();

    JsonDocument respDoc;
    if (deserializeJson(respDoc, responseBody) != DeserializationError::Ok) {
        CLOGW(TAG, "IA [%s]: JSON invalido, siguiente", ep.label);
        return false;
    }
    const char *reply = respDoc["choices"][0]["message"]["content"];
    if (reply && strlen(reply) > 0) {
        outReply = sanitizeTtsText(String(reply));
        // Salvaguarda anti-basura: si quedó un bloque de thinking sin cierre
        // (modelo razonador truncado), esto es audífono-destrozable: descartar.
        if (outReply.indexOf("thinking process") >= 0 || outReply.indexOf(" response") >= 0
            || (outReply.indexOf(" thinking") >= 0 && outReply.indexOf("[DRAW") < 0)) {
            CLOGW(TAG, "IA [%s]: respuesta = thinking basura (%u chars), siguiente", ep.label, outReply.length());
            return false;
        }
        CLOGI(TAG, "IA [%s]: OK (%u chars)", ep.label, outReply.length());
        return true;
    }
    CLOGW(TAG, "IA [%s]: sin contenido, siguiente", ep.label);
    return false;
}

bool AIEngine::queryTextCascade(const String &promptText, String &outResponseText, bool jsonMode, bool noDirectives) {
    AppConfig &cfg = netManager.getConfig();
    String ans;
    // jsonMode: guarda de tokens para el JSON + NINGUNA directiva saboteadora
    ChatTokensGuard tokGuard(jsonMode ? 1500 : 0);
    // Aceptador único: en jsonMode exige un JSON útil (llaves con contenido);
    // la prosa con 200 (típico sin cuota) pasa al siguiente proveedor en vez
    // de matar la cascada ilustrada.
    auto acceptAns = [&](String &a) -> bool {
        if (jsonMode) {
            int ba = a.indexOf('{'), bb = a.lastIndexOf('}');
            if (ba < 0 || bb <= ba || (bb - ba) < 100) {
                CLOGW(TAG, "Cascada JSON: respuesta sin JSON util, siguiente proveedor");
                return false;
            }
        }
        if (resolveWebSearchRound(a, promptText)) { outResponseText = a; return true; }
        if (a.length() > 0) { outResponseText = a; return true; }
        return false;
    };

    // Detectar intención de dibujo para inyectar directiva obligatoria de vector sketch
    // (nunca en cuentos: el conejo del cuento no se dibuja, se cuenta).
    bool wantsDraw = !jsonMode && !noDirectives && !isStoryRequest(musicNorm(promptText)) &&
                     CopitoTools::hasDrawingIntent(promptText);
    String effectivePrompt = promptText;
    if (wantsDraw) {
        effectivePrompt += " [DIRECTIVA OBLIGATORIA DE DIBUJO E-PAPER: Jorge pide un dibujo en su pantalla e-paper 200x200 monocroma. Responde en voz con 1 sola frase corta hablada (máximo 12 palabras) y al final incluye OBLIGATORIAMENTE el tag [SKETCH: CLEAR; ...] con primitivas vectoriales (LINE, CIRCLE, FCIRCLE, RECT, FRECT, CURVE, DITHER) o macros componibles (MACRO SUN x y; MACRO HOUSE x y; MACRO COW x y; MACRO MOON_SMILE x y; MACRO STAR x y; MACRO CLOUD x y; MACRO TREE x y) en coordenadas 0..199. En LINE y CURVE usa SIEMPRE grosor 2, 3 o 4 (el trazo de 1px no se ve en e-paper). NUNCA digas que hiciste el boceto sin emitir el tag [SKETCH: ...]. Jamás uses [DRAW:...] ni [TEXT:...].]";
    }
    if (!jsonMode && !noDirectives && isStoryRequest(musicNorm(promptText))) {
        effectivePrompt += STORY_DIRECTIVE;
        CLOGI(TAG, "MODO CUENTO LARGO activado (cascada)");
    }

    // 1. Gemini nativo
    if (strlen(cfg.gemini_api_key) > 0) {
        if (queryGeminiText(effectivePrompt, ans, jsonMode, noDirectives)) {
            if (acceptAns(ans)) return true;
        }
        CLOGW(TAG, "Cascada: Gemini fallo, probando Groq...");
    }

    String sys = jsonMode ? String("Respondes unicamente con el JSON pedido, sin texto extra ni markdown.")
                          : buildBasePrompt();

    // 2. Groq (gratis, ultra-rápido: qwen3.8-27b con fallback a qwen3.6-27b)
    if (strlen(cfg.ai_groq_key) > 0) {
        ChatEndpoint e = {"Groq", "api.groq.com", 443, true,
                          "/openai/v1/chat/completions", cfg.ai_groq_key,
                          "qwen/qwen3.8-27b", 25000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
        ChatEndpoint e2 = {"Groq-Fallback", "api.groq.com", 443, true,
                           "/openai/v1/chat/completions", cfg.ai_groq_key,
                           "qwen/qwen3.6-27b", 25000};
        if (chatOpenAI(e2, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 3. Cerebras (gratis, alto throughput)
    if (strlen(cfg.ai_cerebras_key) > 0) {
        ChatEndpoint e = {"Cerebras", "api.cerebras.ai", 443, true,
                          "/v1/chat/completions", cfg.ai_cerebras_key,
                          "qwen-3.8-27b", 25000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 4. SambaNova (requiere pago; si no hay créditos da 402 y salta solo)
    if (strlen(cfg.ai_sambanova_key) > 0) {
        ChatEndpoint e = {"SambaNova", "api.sambanova.ai", 443, true,
                          "/v1/chat/completions", cfg.ai_sambanova_key,
                          "Meta-Llama-3.3-70B-Instruct", 25000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 5. Mistral (rápido, límites free - si da 429 salta solo)
    if (strlen(cfg.ai_mistral_key) > 0) {
        ChatEndpoint e = {"Mistral", "api.mistral.ai", 443, true,
                          "/v1/chat/completions", cfg.ai_mistral_key,
                          "mistral-small-latest", 25000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 6. xAI Grok (requiere créditos; si no hay da 403 y salta solo)
    if (strlen(cfg.ai_xai_key) > 0) {
        ChatEndpoint e = {"xAI", "api.x.ai", 443, true,
                          "/v1/chat/completions", cfg.ai_xai_key,
                          "grok-4", 25000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 7. Pollinations (key opcional; anónimo suele dar 402 y se saltea solo)
    {
        ChatEndpoint e = {"Pollinations", "text.pollinations.ai", 443, true,
                          "/openai", cfg.ai_pollinations_key, "openai", 45000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 8. Ollama PC (potente, LAN)
    if (cfg.ollama_host_pc[0] != '\0') {
        ChatEndpoint e = {"OllamaPC", cfg.ollama_host_pc,
                          cfg.ollama_port_pc ? cfg.ollama_port_pc : (uint16_t)11434,
                          false, "/v1/chat/completions", "",
                          cfg.ollama_model_pc[0] ? cfg.ollama_model_pc : "llama3.1:8b", 90000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    // 9. Ollama Pi5 (siempre viva, modelo liviano)
    if (cfg.ollama_host_pi[0] != '\0') {
        ChatEndpoint e = {"OllamaPi", cfg.ollama_host_pi,
                          cfg.ollama_port_pi ? cfg.ollama_port_pi : (uint16_t)11434,
                          false, "/v1/chat/completions", "",
                          cfg.ollama_model_pi[0] ? cfg.ollama_model_pi : "qwen2.5:1.5b", 120000};
        if (chatOpenAI(e, sys, effectivePrompt, ans)) {
            if (acceptAns(ans)) return true;
        }
    }

    CLOGE(TAG, "Cascada: todos los proveedores fallaron");
    return false;
}

bool AIEngine::transcribeGroqWhisper(const uint8_t *wavData, size_t wavSize, String &outTranscript) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.ai_groq_key) == 0 || !wavData || wavSize < 1600) return false;
    if (wavSize > 1024 * 1024) wavSize = 1024 * 1024; // tope 1MB por request

    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(30);
    CLOGI(TAG, "Whisper: transcribiendo %u bytes en Groq...", wavSize);
    if (!cli.connect("api.groq.com", 443)) {
        CLOGW(TAG, "Whisper: sin conexion, siguiente");
        return false;
    }

    // Multipart con WAV en streaming (sin copia gigante en RAM)
    const char *boundary = "----CopitoSTT";
    String p1 = String("--") + boundary
        + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        + "Content-Type: audio/wav\r\n\r\n";
    String p2 = String("\r\n--") + boundary
        + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3-turbo"
        + "\r\n--" + boundary
        + "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nes"
        + "\r\n--" + boundary + "--\r\n";
    size_t total = p1.length() + wavSize + p2.length();

    cli.println("POST /openai/v1/audio/transcriptions HTTP/1.1");
    cli.println("Host: api.groq.com");
    cli.println(String("Authorization: Bearer ") + cfg.ai_groq_key);
    cli.println(String("Content-Type: multipart/form-data; boundary=") + boundary);
    cli.println(String("Content-Length: ") + String(total));
    cli.println("Connection: close");
    cli.println();
    cli.print(p1);
    size_t off = 0;
    while (off < wavSize) {
        size_t n = wavSize - off;
        if (n > 4096) n = 4096;
        if (cli.write(wavData + off, n) != n) {
            CLOGW(TAG, "Whisper: corte subiendo audio");
            cli.stop();
            return false;
        }
        off += n;
    }
    cli.print(p2);

    unsigned long waitStart = millis();
    while (cli.connected() && !cli.available() && (millis() - waitStart < 30000)) {
        delay(15);
    }
    int statusCode = 0;
    bool isChunked = false;
    while (cli.connected() || cli.available()) {
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) statusCode = line.substring(sp + 1, sp + 4).toInt();
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }
    if (statusCode != 200) {
        CLOGW(TAG, "Whisper: HTTP %d", statusCode);
        cli.stop();
        return false;
    }
    String responseBody = readHttpBody(cli, isChunked);
    cli.stop();

    JsonDocument respDoc;
    if (deserializeJson(respDoc, responseBody) != DeserializationError::Ok) return false;
    const char *txt = respDoc["text"];
    if (txt && strlen(txt) > 0) {
        outTranscript = String(txt);
        outTranscript.trim();
        CLOGI(TAG, "Whisper OK: '%s'", outTranscript.c_str());
        return outTranscript.length() > 0;
    }
    return false;
}

bool AIEngine::streamAudioData(WiFiClient &cli, int sampleRate, AudioStreamCallback cb, bool isChunked,
                               const uint8_t *prelude, size_t preludeLen) {
    audio.startPlayback(sampleRate);
    if (cb) cb(true); // Signal speaking transition right when audio begins
    // Prelude: bytes ya validados (peek anti-JSON) que se entregan AL DAC
    // después de abrir el I2S. Antes se escribían antes del startPlayback
    // (I2S no configurado) y rompían el framing si era chunked.
    size_t totalBytes = 0;
    if (prelude && preludeLen > 0) {
        size_t even = preludeLen & ~(size_t)1;
        if (even > 0) {
            audio.writePcmChunk(prelude, even);
            totalBytes += even;
        }
    }
    uint8_t buffer[1024];
    int frameCounter = 0;
    unsigned long lastDataMs = millis();
    // Stall global: conexión abierta pero sin datos (hotspot colgado) no debe
    // trabar la voz para siempre. 12 s sin nada = cortar y reportar fracaso.
    static const unsigned long STALL_MS = 12000;

    if (isChunked) {
        while (cli.connected() || cli.available()) {
            if (g_ttsStopRequested) break; // BOOT mientras habla = callar
            String hexLine = cli.readStringUntil('\n');
            hexLine.trim();
            if (hexLine.length() == 0) {
                if (millis() - lastDataMs > STALL_MS) break;
                continue;
            }

            long chunkSize = strtol(hexLine.c_str(), NULL, 16);
            if (chunkSize <= 0) {
                break; // 0 chunk indicates end of body
            }

            long remaining = chunkSize;
            while (remaining > 0 && (cli.connected() || cli.available())) {
                if (g_ttsStopRequested) break;
                size_t toRead = min((size_t)sizeof(buffer), (size_t)remaining);
                size_t bytesRead = cli.read(buffer, toRead);
                if (bytesRead > 0) {
                    audio.writePcmChunk(buffer, bytesRead);
                    totalBytes += bytesRead;
                    lastDataMs = millis();
                    remaining -= bytesRead;
                    if (cb && (++frameCounter % 6 == 0)) {
                        cb(frameCounter % 12 < 6);
                    }
                } else {
                    if (millis() - lastDataMs > STALL_MS) break;
                    delay(2);
                }
            }

            // Consume trailing \r\n after chunk data
            while (cli.available()) {
                char c = cli.read();
                if (c == '\n') break;
            }
        }
    } else {
        while (cli.connected() || cli.available()) {
            if (g_ttsStopRequested) break; // BOOT mientras habla = callar
            size_t avail = cli.available();
            if (avail > 0) {
                size_t toRead = min(sizeof(buffer), avail);
                size_t bytesRead = cli.read(buffer, toRead);
                if (bytesRead > 0) {
                    audio.writePcmChunk(buffer, bytesRead);
                    totalBytes += bytesRead;
                    lastDataMs = millis();
                    if (cb && (++frameCounter % 6 == 0)) {
                        cb(frameCounter % 12 < 6);
                    }
                }
            } else {
                if (millis() - lastDataMs > STALL_MS) break;
                delay(5);
            }
        }
    }

    audio.stopPlayback();
    cli.stop();
    // Retorno honesto: menos de ~100 ms de audio (3200 bytes) = truncado.
    // Antes devolvía true siempre y ningún fallback se activaba.
    return totalBytes >= 3200;
}

bool AIEngine::streamGeminiTts(const String &text, AudioStreamCallback cb, const char *voice, const char *stylePrefix, int gain256) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.gemini_api_key) == 0) {
        CLOGE(TAG, "Gemini API key is not configured for Gemini TTS!");
        return false;
    }

    WiFiClientSecure ttsCli;
    ttsCli.setInsecure();
    ttsCli.setTimeout(25);

    CLOGI(TAG, "Conectando a Gemini TTS (%s:443)...", GEMINI_API_HOST);
    if (!ttsCli.connect(GEMINI_API_HOST, GEMINI_API_PORT)) {
        CLOGE(TAG, "Fallo de conexión a Gemini TTS!");
        return false;
    }

    const char *voiceName = (voice && voice[0]) ? voice : "Aoede";
    String spoken = (stylePrefix && stylePrefix[0]) ? (String(stylePrefix) + text) : text;
    JsonDocument doc;
    doc["contents"][0]["parts"][0]["text"] = spoken;
    doc["generationConfig"]["responseModalities"][0] = "AUDIO";
    doc["generationConfig"]["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = voiceName;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String endpoint = "/v1beta/models/" + String(GEMINI_TTS_MODEL) + ":generateContent?key=" + String(cfg.gemini_api_key);

    ttsCli.println("POST " + endpoint + " HTTP/1.1");
    ttsCli.println("Host: " + String(GEMINI_API_HOST));
    ttsCli.println("Content-Type: application/json");
    ttsCli.println("Content-Length: " + String(jsonPayload.length()));
    ttsCli.println("Connection: close");
    ttsCli.println();
    ttsCli.print(jsonPayload);
    ttsCli.flush();

    CLOGI(TAG, "Gemini TTS solicitud enviada (%d bytes). Esperando audio...", jsonPayload.length());

    unsigned long waitStart = millis();
    while (ttsCli.connected() && !ttsCli.available() && (millis() - waitStart < 20000)) {
        delay(15);
    }

    int statusCode = 0;
    bool isChunked = false;
    // Sin timeout global, un servidor que gotea cabecera colgaba la voz para
    // siempre (el cuento quedaba mudo a mitad). 60 s y a fallback.
    unsigned long hdrStart = millis();
    while (ttsCli.connected() || ttsCli.available()) {
        if (millis() - hdrStart > 60000) {
            CLOGW(TAG, "Gemini TTS: cabecera sin completar en 60s, corto frase");
            ttsCli.stop();
            return false;
        }
        if (!ttsCli.available()) { delay(10); continue; }
        String line = ttsCli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGW(TAG, "Gemini TTS devolvió HTTP %d (limite de tasa o cuota). Activando fallback!", statusCode);
        ttsCli.stop();
        return false;
    }

    // Bomba de bytes con de-chunking (2026-09-09): si la respuesta viene con
    // Transfer-Encoding: chunked, los tamaños de chunk en hex son base64
    // válido y decodificarían como ráfagas de estática ("lluvia" al final de
    // la frase). No bloquea: devuelve 0 si aún no hay datos.
    struct TtsChunkPump {
        WiFiClientSecure *sock; // (no "cli": es macro en Arduino)
        bool chunked;
        long chunkLeft;   // bytes de body restantes (<0 = falta leer tamaño)
        uint8_t crlfNeed; // bytes \r\n pendientes entre chunks
        bool eos;         // último chunk (0) consumido
        char lineBuf[20];
        uint8_t lineLen;
        TtsChunkPump(WiFiClientSecure *c, bool ch)
            : sock(c), chunked(ch), chunkLeft(ch ? -1 : LONG_MAX),
              crlfNeed(0), eos(false), lineLen(0) { lineBuf[0] = '\0'; }
        long pollChunkSize() { // -2 incompleto, -1 error, >=0 tamaño (0 = fin)
            while (sock->available()) {
                int c = sock->read();
                if (c < 0) break;
                if (c == '\r') continue;
                if (c == '\n') {
                    lineBuf[lineLen] = '\0';
                    long v = (lineLen == 0) ? -1 : strtol(lineBuf, NULL, 16);
                    lineLen = 0;
                    return v;
                }
                if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = (char)c;
                else { lineLen = 0; return -1; }
            }
            return -2;
        }
        size_t readBody(uint8_t *dst, size_t n) { // bytes de BODY, sin framing
            if (eos || n == 0 || !dst) return 0;
            if (!chunked) {
                int r = sock->read(dst, n);
                return (r > 0) ? (size_t)r : 0;
            }
            size_t got = 0;
            while (got < n) {
                if (chunkLeft == -2) { // CRLF entre chunks, después tamaño
                    while (crlfNeed > 0) {
                        if (!sock->available()) return got;
                        int c = sock->read();
                        if (c < 0) return got;
                        if (c == '\r' || c == '\n') crlfNeed--;
                        else { eos = true; return got; } // desync: cerrar
                    }
                    chunkLeft = -1;
                    continue;
                }
                if (chunkLeft < 0) {
                    long sz = pollChunkSize();
                    if (sz == -2) return got; // incompleto: luego
                    if (sz <= 0) { eos = true; return got; } // 0 = fin, <0 = error
                    chunkLeft = sz;
                    continue;
                }
                if (chunkLeft == 0) { chunkLeft = -2; crlfNeed = 2; continue; }
                size_t want = n - got;
                if ((long)want > chunkLeft) want = (size_t)chunkLeft;
                int r = sock->read(dst + got, want);
                if (r <= 0) return got;
                got += (size_t)r;
                chunkLeft -= r;
            }
            return got;
        }
    };
    TtsChunkPump pump(&ttsCli, isChunked);
    if (isChunked) CLOGI(TAG, "Gemini TTS: respuesta chunked (de-chunking activo)");

    // Buscar el patrón "data": " o "data":" en el stream JSON
    char patBuf[16];
    size_t patLen = 0;
    bool foundData = false;
    unsigned long searchStart = millis();

    while (!pump.eos && (ttsCli.connected() || ttsCli.available()) && (millis() - searchStart < 15000)) {
        uint8_t b = 0;
        if (pump.readBody(&b, 1) == 0) { delay(2); continue; }
        char c = (char)b;
        if (patLen < sizeof(patBuf) - 1) {
            patBuf[patLen++] = c;
        } else {
            memmove(patBuf, patBuf + 1, sizeof(patBuf) - 2);
            patBuf[sizeof(patBuf) - 2] = c;
        }
        patBuf[patLen] = '\0';

        if (strstr(patBuf, "\"data\": \"") != nullptr || strstr(patBuf, "\"data\":\"") != nullptr) {
            foundData = true;
            break;
        }
    }

    if (!foundData) {
        CLOGE(TAG, "Gemini TTS: no se encontró stream de audio en la respuesta JSON");
        ttsCli.stop();
        return false;
    }

    CLOGI(TAG, "Gemini TTS: audio detectado. Iniciando reproducción I2S a 24kHz...");
    audio.startPlayback(24000);
    if (cb) cb(true); // Signal speaking transition right when sound starts

    // Tabla inversa Base64
    static const int8_t b64_rev[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    // Decodificación base64 por BLOQUES (no byte-a-byte por TLS): cada
    // read() por TLS cuesta; leer de a 256 B reduce el tiempo de frase a una
    // fracción y evita que un stall del hotspot la trunque a la mitad.
    // Además: timeout global 60 s (una frase larga son ~30 s de audio + red),
    // stall 8 s, relleno de silencio si hay huecos >250 ms (igual que música),
    // y retorno honesto (mínimo 4800 bytes ≈ 100 ms @24kHz; si no, false).
    uint8_t pcmBuf[512];
    size_t pcmIdx = 0;
    int frameCounter = 0;
    bool done = false;
    size_t pcmBytes = 0;
    int32_t peakSrc = 0; // pico de la fuente (pre-ganancia): diagnóstico de saturación
    char b64grp[4];
    int b64n = 0;
    unsigned long ttsDeadline = millis() + 60000;
    unsigned long lastDataMs = millis();
    int silenceFills = 0;
    uint8_t netBuf[256];

    // Flush del buffer PCM: pcmBuf es de 512 B y cada triple base64 suma hasta
    // 3 B. Se vacía ANTES de escribir (2026-09-09: vaciar después del chequeo
    // pasaba 1-2 B del buffer en stack en cada ciclo = audio corrupto +
    // PANIC a mitad del cuento). Deja el byte impar residual al frente.
    auto flushPcmBuf = [&]() {
        if (pcmIdx < sizeof(int16_t)) return;
        // Descarta un posible byte impar residual del padding base64 (un
        // int16_t a medias sonaría como un chasquido a full-scale).
        size_t evenLen = pcmIdx & ~(size_t)1;
        for (size_t k = 0; k + 1 < evenLen; k += 2) {
            int16_t s = (int16_t)(pcmBuf[k] | (pcmBuf[k + 1] << 8));
            int32_t a = s < 0 ? -(int32_t)s : (int32_t)s;
            if (a > peakSrc) peakSrc = a;
        }
        audio.writePcmChunk(pcmBuf, evenLen, gain256);
        pcmBytes += evenLen;
        if (evenLen < pcmIdx) memmove(pcmBuf, pcmBuf + evenLen, pcmIdx - evenLen);
        pcmIdx -= evenLen;
        if (cb && (++frameCounter % 6 == 0)) {
            cb(frameCounter % 12 < 6);
        }
    };

    while (!pump.eos && (ttsCli.connected() || ttsCli.available()) && !done && (millis() < ttsDeadline)) {
        if (g_ttsStopRequested) { CLOGI(TAG, "Gemini TTS: frenado por BOOT"); break; }
        size_t got = pump.readBody(netBuf, sizeof(netBuf));
        if (got == 0) {
            if (pump.eos || !ttsCli.connected()) break;
            if (millis() - lastDataMs > 8000) {
                CLOGW(TAG, "Gemini TTS: stall 8 s sin datos, corto frase");
                break;
            }
            if (millis() - lastDataMs > 250 && silenceFills < 4 && pcmBytes > 0) {
                // Hueco en el hotspot: 1 KB de silencio mantiene el DMA en
                // ritmo en vez de dejar un underrun audible.
                static uint8_t silenceQ[1024] = {0};
                audio.writePcmChunk(silenceQ, sizeof(silenceQ));
                silenceFills++;
                lastDataMs = millis();
            } else {
                delay(2);
            }
            continue;
        }
        lastDataMs = millis();
        for (size_t i = 0; i < got && !done; i++) {
            char ch = (char)netBuf[i];
            if (ch == '"' || ch == '\\' || ch == '}') { done = true; break; }
            if (b64_rev[(uint8_t)ch] >= 0 || ch == '=') {
                b64grp[b64n++] = ch;
                if (b64n == 4) {
                    b64n = 0;
                    int v0 = b64_rev[(uint8_t)b64grp[0]];
                    int v1 = b64_rev[(uint8_t)b64grp[1]];
                    int v2 = (b64grp[2] == '=') ? 0 : b64_rev[(uint8_t)b64grp[2]];
                    int v3 = (b64grp[3] == '=') ? 0 : b64_rev[(uint8_t)b64grp[3]];
                    if (v0 < 0 || v1 < 0) continue;
                    if (pcmIdx + 3 > sizeof(pcmBuf)) flushPcmBuf(); // lugar antes de escribir
                    uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) | ((uint32_t)v2 << 6) | (uint32_t)v3;
                    pcmBuf[pcmIdx++] = (uint8_t)((triple >> 16) & 0xFF);
                    if (b64grp[2] != '=') pcmBuf[pcmIdx++] = (uint8_t)((triple >> 8) & 0xFF);
                    if (b64grp[3] != '=') pcmBuf[pcmIdx++] = (uint8_t)(triple & 0xFF);
                    if (pcmIdx >= sizeof(pcmBuf)) flushPcmBuf();
                }
            }
        }
    }

    if (pcmIdx >= sizeof(int16_t)) {
        // Cola del stream: lo que quedó sin vaciar (incluye el byte impar
        // residual si lo hubo). flushPcmBuf lo recorta a par.
        flushPcmBuf();
    }

    CLOGI(TAG, "Gemini TTS [%s]: pico fuente %d/32767, %u bytes", voiceName, (int)peakSrc, (unsigned)pcmBytes);

    audio.stopPlayback();
    // Sonda DSP de ESTE playback (ver /api/audio_probe?mode=last): si la
    // "lluvia" vuelve, el clip/limit dice si nació en la fuente o en el DAC.
    {
        AudioProbe pr;
        audio.probeSnapshot(pr);
        CLOGI(TAG, "Gemini TTS dsp: pre=%d post=%d clip=%u lim=%u n=%u",
              (int)pr.peakPre, (int)pr.peakPost,
              (unsigned)pr.clipped, (unsigned)pr.limited, (unsigned)pr.samples);
    }
    ttsCli.stop();
    if (pcmBytes < 4800) {
        CLOGW(TAG, "Gemini TTS: frase truncada (%u bytes), reporto fallo para fallback", (unsigned)pcmBytes);
        return false;
    }
    CLOGI(TAG, "Gemini TTS: reproducción completada (%u bytes)", (unsigned)pcmBytes);
    return true;
}

// Núcleo WAV compartido LAN/funnel (definido abajo, tras streamLocalTts).
static bool playLocalWavBody(WiFiClient &cli, unsigned long t0, const char *label,
                             const char *host, uint16_t port, AudioStreamCallback cb,
                             unsigned long hdrTimeoutMs, unsigned long bodyTimeoutMs);

bool AIEngine::streamLocalTts(const char *host, uint16_t port, const String &voice, const String &text, AudioStreamCallback cb, bool slow) {    if (!host || !host[0] || port == 0) return false;
    WiFiClient cli;
    cli.setTimeout(3);
    unsigned long t0 = millis();
    if (!cli.connect(host, port)) {
        return false; // apagado o fuera de casa: salto silencioso y rápido
    }

    JsonDocument doc;
    doc["text"] = text;
    doc["voice"] = voice;
    // Voz lenta de cuento: el Piper del Pi5 la honra (length-scale);
    // Kokoro la ignora (clave extra tolerada).
    if (slow) doc["slow"] = true;
    String payload;
    serializeJson(doc, payload);

    cli.println("POST /tts HTTP/1.1");
    cli.println("Host: " + String(host));
    cli.println("Content-Type: application/json");
    cli.println("Content-Length: " + String(payload.length()));
    cli.println("Connection: close");
    cli.println();
    cli.print(payload);

    int status = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 8000)) {
        if (!cli.available()) { delay(2); continue; }
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    if (status != 200) { cli.stop(); return false; }

    return playLocalWavBody(cli, t0, "TTS local", host, port, cb, 8000, 45000);
}

// Núcleo WAV compartido LAN/funnel: valida cabecera RIFF, descarga completa
// a PSRAM, normaliza pico y reproduce. Truncado o inválido => false (nube).
// label = "TTS local" o "TTS funnel" (solo logs).
static bool playLocalWavBody(WiFiClient &cli, unsigned long t0, const char *label,
                             const char *host, uint16_t port, AudioStreamCallback cb,
                             unsigned long hdrTimeoutMs, unsigned long bodyTimeoutMs) {
    // Cabecera WAV 44 bytes -> sample rate real (Kokoro 24kHz, Piper 16/22kHz)
    // Se exige el id "data" en 36..39: sin él, el body puede ser otro stream
    // (MP3/Opus con cabecera genérica) y sonaría como lluvia de estática.
    uint8_t hdr[44];
    size_t got = 0;
    while (got < sizeof(hdr) && (millis() - t0 < hdrTimeoutMs)) {
        if (cli.available()) {
            int r = cli.read(hdr + got, sizeof(hdr) - got);
            if (r > 0) got += (size_t)r;
        } else if (!cli.connected()) {
            break;
        } else {
            delay(2);
        }
    }
    if (got < sizeof(hdr) || memcmp(hdr, "RIFF", 4) != 0) { cli.stop(); return false; }
    uint16_t channels = (uint16_t)(hdr[22] | (hdr[23] << 8));
    uint32_t rate = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8) | ((uint32_t)hdr[26] << 16) | ((uint32_t)hdr[27] << 24);
    uint16_t bits = (uint16_t)(hdr[34] | (hdr[35] << 8));
    uint32_t dataSize = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) | ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
    if (channels != 1 || bits != 16 || rate < 8000 || rate > 48000) { cli.stop(); return false; }
    if (memcmp(hdr + 36, "data", 4) != 0) {
        CLOGW(TAG, "%s sin chunk 'data' (formato extendido). Abortando a nube.", label);
        cli.stop();
        return false;
    }
    if (dataSize < 1024 || dataSize > 1024 * 1024) {
        CLOGW(TAG, "%s dataSize inválido (%u). Abortando a nube.", label, dataSize);
        cli.stop();
        return false;
    }

    // Descarga COMPLETA a PSRAM antes de reproducir:
    // 1) el WiFi queda inactivo durante la reproducción (cero RF sobre el PA),
    // 2) permite validar tamaño exacto y normalizar picos antes del DAC.
    // Truncado o incompleto => nube, jamás ruido.
    uint8_t *pcmBody = (uint8_t *)(psramFound() ? ps_malloc(dataSize) : malloc(dataSize));
    if (!pcmBody) { cli.stop(); return false; }
    size_t gotBody = 0;
    while (gotBody < dataSize && (millis() - t0 < bodyTimeoutMs)) {
        if (cli.available()) {
            int r = cli.read(pcmBody + gotBody, dataSize - gotBody);
            if (r > 0) gotBody += (size_t)r;
        } else if (!cli.connected()) {
            break;
        } else {
            delay(2);
        }
    }
    cli.stop(); // desde acá, WiFi en silencio durante todo el playback
    if (gotBody < dataSize) {
        CLOGW(TAG, "%s truncado (%u/%u). Abortando a nube.", label, gotBody, dataSize);
        free(pcmBody);
        return false;
    }

    // Normalización por pico: lleva el pico a 26000 (~-2dBFS) con ganancia en
    // [0.5, 1.25]. Un WAV a full-scale (Piper llega a 32766) ya no aplasta el
    // limiter en distorsión áspera; uno moderado suena igual que antes.
    int16_t *samples = (int16_t *)pcmBody;
    size_t nSamp = dataSize / sizeof(int16_t);
    int32_t peak = 0;
    for (size_t i = 0; i < nSamp; i++) {
        int32_t a = samples[i] < 0 ? -(int32_t)samples[i] : (int32_t)samples[i];
        if (a > peak) peak = a;
    }
    if (peak < 200) {
        CLOGW(TAG, "%s casi en silencio (pico %d). Nada que reproducir.", label, peak);
        free(pcmBody);
        return true; // no es error: evita blast de ruido amplificado
    }
    int32_t gain256 = (26000L * 256) / peak;
    if (gain256 > 256) gain256 = 256; // nunca boost: solo atenuar (el boost crujia picos)
    if (gain256 < 128) gain256 = 128;

    audio.startPlayback((int)rate);
    if (cb) cb(true);
    int16_t normBuf[512];
    size_t pos = 0;
    int frames = 0;
    while (pos < nSamp) {
        if (g_ttsStopRequested) { CLOGI(TAG, "%s: frenado por BOOT", label); break; }
        size_t batch = nSamp - pos;
        if (batch > 512) batch = 512;
        for (size_t i = 0; i < batch; i++) {
            normBuf[i] = (int16_t)(((int32_t)samples[pos + i] * gain256) / 256);
        }
        audio.writePcmChunk((const uint8_t *)normBuf, batch * sizeof(int16_t));
        pos += batch;
        if (cb && (++frames % 6 == 0)) cb(frames % 12 < 6);
    }

    audio.stopPlayback();
    free(pcmBody);
    CLOGI(TAG, "%s OK (%s:%u @%luHz, pico %d, gain %d/256)", label, host, port, (unsigned long)rate, peak, gain256);
    return true;
}

// TTS por funnel (llavero nómade): Piper del Pi5 vía HTTPS con X-Copito-Key.
// Misma validación que LAN (playLocalWavBody): jamás ruido por red pública.
bool AIEngine::streamFunnelTts(const String &voice, const String &text, AudioStreamCallback cb, bool slow) {
    AppConfig &cfg = netManager.getConfig();
    if (cfg.music_pub_host[0] == '\0' || text.length() == 0) return false;
    WiFiClientSecure tlsCli;
    tlsCli.setInsecure();
    tlsCli.setTimeout(15);
    unsigned long t0 = millis();
    String pubHost = String(cfg.music_pub_host);
    if (!tlsCli.connect(pubHost.c_str(), 443)) return false;

    JsonDocument doc;
    doc["text"] = text;
    doc["voice"] = voice;
    // El proxy Pi3 reenvía el JSON tal cual al Piper del Pi5 (honra slow).
    if (slow) doc["slow"] = true;
    String payload;
    serializeJson(doc, payload);

    tlsCli.println("POST /copito/tts HTTP/1.1");
    tlsCli.println("Host: " + pubHost);
    tlsCli.println("Content-Type: application/json");
    if (cfg.funnel_key[0] != '\0') tlsCli.println("X-Copito-Key: " + String(cfg.funnel_key));
    tlsCli.println("Content-Length: " + String(payload.length()));
    tlsCli.println("Connection: close");
    tlsCli.println();
    tlsCli.print(payload);

    int status = 0;
    while ((tlsCli.connected() || tlsCli.available()) && (millis() - t0 < 25000)) {
        if (!tlsCli.available()) { delay(5); continue; }
        String line = tlsCli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    if (status == 403) {
        CLOGW(TAG, "TTS funnel: clave rechazada (403). Revisar 'fk' en portal.");
        tlsCli.stop();
        return false;
    }
    if (status != 200) { tlsCli.stop(); return false; }
    return playLocalWavBody(tlsCli, t0, "TTS funnel", pubHost.c_str(), 443, cb, 20000, 90000);
}

// Parte el texto largo en bloques (~450 letras, por frase): cada bloque se
// baja y habla por separado con barra de progreso. Sin esto, un cuento bajaba
// un WAV gigante entero antes de emitir sonido ("carga eterna" sin voz).
static std::vector<String> splitTtsBlocks(const String &text, size_t maxChars = 450) {
    std::vector<String> out;
    int start = 0;
    int len = text.length();
    while (start < len) {
        while (start < len && (text.charAt(start) == ' ' || text.charAt(start) == '\n' || text.charAt(start) == '\r')) start++;
        if (start >= len) break;
        int rem = len - start;
        if (rem <= (int)maxChars) {
            String b = text.substring(start);
            b.trim();
            if (b.length() > 0) out.push_back(b);
            break;
        }
        int tend = start + (int)maxChars;
        int cut = -1;
        for (int i = tend; i >= start + 40; i--) {
            char c = text.charAt(i);
            if ((c == '.' || c == '!' || c == '?' || c == ';') && (i + 1 == len || text.charAt(i + 1) == ' ')) { cut = i + 1; break; }
        }
        if (cut < 0) {
            for (int i = tend; i >= start + 40; i--) {
                char c = text.charAt(i);
                if ((c == ',' || c == ':') && (i + 1 == len || text.charAt(i + 1) == ' ')) { cut = i + 1; break; }
            }
        }
        if (cut < 0) {
            for (int i = tend; i >= start + 30; i--) {
                if (text.charAt(i) == ' ') { cut = i; break; }
            }
        }
        if (cut < 0) cut = tend;
        String b = text.substring(start, cut);
        b.trim();
        if (b.length() > 0) out.push_back(b);
        start = cut;
    }
    return out;
}

// Ruta clasica de un texto (corto o un bloque): la de siempre, intacta.
// La nueva streamTtsToSpeaker la llama por bloque con barra de progreso.
// Voz pegajosa del cuento (2026-09-09): el proveedor que ganó el bloque 1
// abre los siguientes (1=Sulafat, 2=local, 3=Gemini, 4=Google). Sin esto cada
// bloque redescubría y la voz cambiaba a mitad del relato.
static int s_storyTtsPref = 0;
void AIEngine::storyTtsResetPref() { s_storyTtsPref = 0; }
bool AIEngine::streamTtsSingle(const String &cleanText, AudioStreamCallback lipSyncCb) {
    AppConfig &cfg = netManager.getConfig();
    bool hasGeminiKey = strlen(cfg.gemini_api_key) > 0;
    // 0N. Voz narrador (cuento en cascada): Gemini TTS Sulafat lento ANTES que
    // la cadena normal, con ganancia 160 (-4dB: la voz nueva viene caliente).
    // Si falla (cuota/red), se sigue con la cadena normal.
    auto tryNarrator = [&]() -> bool {
        if (!s_storyVoice || !hasGeminiKey) return false;
        CLOGI(TAG, "TTS narrador: probando Sulafat lento...");
        if (streamGeminiTts(cleanText, lipSyncCb, "Sulafat", "[slow] ", 160)) {
            aiVoiceNote(true, nullptr);
            s_storyTtsPref = 1;
            return true;
        }
        CLOGW(TAG, "TTS narrador no disponible, sigo con cadena normal");
        return false;
    };
    // 0. Servidores locales LAN primero (PC Kokoro -> Pi5 Piper): $0, sin tokens.
    // Fast-fail: si fallaron hace menos de 5 min (típico fuera de casa), se
    // prueba host1 y se SALTA host2: antes se pagaban 8 s + 45 s por host
    // antes de llegar a la nube (~1 min de silencio en la calle).
    auto tryLocal = [&]() -> bool {
        String localVoice = strlen(cfg.tts_local_voice) > 0 ? String(cfg.tts_local_voice) : String(DEFAULT_TTS_LOCAL_VOICE);
        // Cuentos con Daniela (Piper Pi5, es_AR high): voz femenina rioplatense
        // para relatos; el resto sigue con la voz configurada.
        String storyVoice = s_storyVoice ? String(STORY_LOCAL_VOICE) : localVoice;
        bool skipHost2 = (s_lastLocalTtsFailMs != 0) && (millis() - s_lastLocalTtsFailMs < 300000);
        if (streamLocalTts(cfg.tts_local_host1, cfg.tts_local_port1, storyVoice, cleanText, lipSyncCb, s_storyVoice)) {
            s_lastLocalTtsFailMs = 0;
            aiVoiceNote(true, nullptr);
            if (s_storyVoice) s_storyTtsPref = 2;
            return true;
        }
        if (!skipHost2) {
            if (streamLocalTts(cfg.tts_local_host2, cfg.tts_local_port2, storyVoice, cleanText, lipSyncCb, s_storyVoice)) {
                s_lastLocalTtsFailMs = 0;
                aiVoiceNote(true, nullptr);
                if (s_storyVoice) s_storyTtsPref = 2;
                return true;
            }
        } else {
            CLOGI(TAG, "TTS local: host2 saltado (falló hace poco, fast-fail a nube)");
        }
        s_lastLocalTtsFailMs = millis();
        return false;
    };
    if (s_storyVoice && s_storyTtsPref > 0) {
        bool ok = false;
        if (s_storyTtsPref == 1) ok = tryNarrator();
        else if (s_storyTtsPref == 2) ok = tryLocal();
        else if (s_storyTtsPref == 3 && hasGeminiKey) {
            if (streamGeminiTts(cleanText, lipSyncCb)) { aiVoiceNote(true, nullptr); ok = true; }
        } else if (s_storyTtsPref == 4) {
            if (streamGoogleFreeTts(cleanText, lipSyncCb)) { aiVoiceNote(true, nullptr); ok = true; }
        } else if (s_storyTtsPref == 5) {
            String fv = s_storyVoice ? String(STORY_LOCAL_VOICE)
                : (strlen(cfg.tts_local_voice) > 0 ? String(cfg.tts_local_voice) : String(DEFAULT_TTS_LOCAL_VOICE));
            if (streamFunnelTts(fv, cleanText, lipSyncCb, s_storyVoice)) { aiVoiceNote(true, nullptr); ok = true; }
        }
        if (ok) return true;
        CLOGI(TAG, "Cuento: la voz preferida fallo, redescubro proveedor...");
        s_storyTtsPref = 0;
    }
    if (tryNarrator()) return true;
    if (tryLocal()) return true;
    // Funnel (Piper del Pi5 por HTTPS): el llavero nómade sin LAN usa la
    // misma voz local por internet. Solo si hay host público configurado.
    auto tryFunnel = [&]() -> bool {
        String fv = s_storyVoice ? String(STORY_LOCAL_VOICE)
            : (strlen(cfg.tts_local_voice) > 0 ? String(cfg.tts_local_voice) : String(DEFAULT_TTS_LOCAL_VOICE));
        if (streamFunnelTts(fv, cleanText, lipSyncCb, s_storyVoice)) {
            aiVoiceNote(true, nullptr);
            if (s_storyVoice) s_storyTtsPref = 5;
            return true;
        }
        return false;
    };
    if (tryFunnel()) return true;

    bool success = false;
    switch (cfg.tts_provider) {
        case TTS_GEMINI:
            if (hasGeminiKey) {
                CLOGI(TAG, "TTS [1/2]: Probando Gemini TTS (%s)...", GEMINI_TTS_MODEL);
                success = streamGeminiTts(cleanText, lipSyncCb);
                if (success && s_storyVoice) s_storyTtsPref = 3;
            }
            break;
        case TTS_GOOGLE_FREE:
            CLOGI(TAG, "TTS: Usando Google Free TTS...");
            success = streamGoogleFreeTts(cleanText, lipSyncCb);
            if (success && s_storyVoice) s_storyTtsPref = 4;
            break;
        case TTS_ELEVENLABS:
            if (strlen(cfg.tts_api_key) > 0) {
                success = streamElevenLabs(cleanText, lipSyncCb);
            }
            break;
        case TTS_OPENAI:
            if (strlen(cfg.tts_api_key) > 0) {
                success = streamOpenAiTts(cleanText, lipSyncCb);
            }
            break;
        case TTS_VOICERSS:
            if (strlen(cfg.tts_api_key) > 0) {
                success = streamVoiceRss(cleanText, lipSyncCb);
            }
            break;
        default:
            success = streamGeminiTts(cleanText, lipSyncCb);
            break;
    }

    // MULTI-TIER AUTOMATIC FALLBACK:
    // 1. If Gemini TTS failed (e.g. 429 rate limit), IMMEDIATELY fall back to Google Free TTS!
    if (!success && cfg.tts_provider != TTS_GOOGLE_FREE) {
        CLOGW(TAG, "TTS primario no disponible o con límite de tasa. Activando FALLBACK A GOOGLE FREE TTS!");
        success = streamGoogleFreeTts(cleanText, lipSyncCb);
        if (success && s_storyVoice) s_storyTtsPref = 4;
    }

    // 2. If Google Free TTS was selected and failed, try Gemini TTS!
    if (!success && cfg.tts_provider == TTS_GOOGLE_FREE && hasGeminiKey) {
        CLOGW(TAG, "Google Free TTS falló. Intentando Gemini TTS...");
        success = streamGeminiTts(cleanText, lipSyncCb);
        if (success && s_storyVoice) s_storyTtsPref = 3;
    }

    aiVoiceNote(success, success ? nullptr : "tts-all-fail");
    return success;
}

// Voz por partes: el texto largo (cuento) se baja y habla bloque a bloque
// con barra de progreso. La voz arranca con el primer bloque listo, no tras
// bajar el audio entero. BOOT entre bloques = callar.
bool AIEngine::streamTtsToSpeaker(const String &text, AudioStreamCallback lipSyncCb) {
    String cleanText = sanitizeTtsText(text);
    if (cleanText.length() == 0) return false;
    if (copitoVoiceCancelled()) return false; // POWER en "pensando": no hablar
    powerBoost(); // plan leve: descarga+DSP con radio full
    g_speaking = true; // BOOT en este lapso = callar (no superponer sonidos)
    g_ttsStopRequested = false;
    // En cuento, bloques chicos (~220 letras = 1-2 oraciones): la voz arranca
    // antes y cada bloque es una unidad con pausa para digerir (cuento infantil).
    std::vector<String> blocks = splitTtsBlocks(cleanText, s_storyVoice ? 220 : 450);
    bool ok = false;
    if (blocks.size() <= 1) {
        ok = streamTtsSingle(cleanText, lipSyncCb);
    } else {
        CLOGI(TAG, "Voz larga: %u bloques con progreso", (unsigned)blocks.size());
        // En cuento no hay cartel de progreso: la pantalla muestra el texto o
        // el dibujo de la parte (2026-09-09: el HABLANDO/Voz x/y parpadeaba la
        // e-paper por cada bloque en pleno relato).
        if (!s_storyVoice) epd.showSpeechProgress(0, (int)blocks.size());
        bool anyAudio = false;
        unsigned long prevMs = millis();
        int storyStrikes = 0; // bloques seguidos sin audio (TTS caído)
        for (size_t b = 0; b < blocks.size(); b++) {
            if (g_ttsStopRequested) break;
            if (!s_storyVoice) epd.showSpeechProgress((int)b + 1, (int)blocks.size());
            bool blockOk = streamTtsSingle(blocks[b], lipSyncCb);
            if (blockOk) {
                anyAudio = true;
                storyStrikes = 0;
            } else if (s_storyVoice && ++storyStrikes >= 3) {
                // TTS muerto (cuota/red): no colgar el cuento bloque a bloque.
                // El texto ya está en pantalla; la parte siguiente lo intenta.
                CLOGW(TAG, "Cuento: TTS caido %d bloques seguidos, sigo con texto", storyStrikes);
                break;
            }
            // Medicion por bloque: bajar+hablar vs habla estimada
            // (~750 letras/min). Bloques chicos = la descarga nunca supera
            // a lo hablado = no se traba entre partes.
            unsigned long now = millis();
            CLOGI(TAG, "Voz bloque %u/%u: %u letras en %ums (habla ~%ums)%s",
                  (unsigned)(b + 1), (unsigned)blocks.size(), (unsigned)blocks[b].length(),
                  (unsigned)(now - prevMs), (unsigned)(blocks[b].length() * 80UL),
                  g_ttsStopRequested ? " [STOP]" : "");
            prevMs = now;
            // Respiro entre bloques en cuento: ritmo de relato para chicos
            // (oración por oración, con pausa para digerir).
            if (s_storyVoice && b + 1 < blocks.size() && !g_ttsStopRequested) delay(800);
        }
        ok = (anyAudio || g_ttsStopRequested);
    }
    g_speaking = false;
    powerRelax(); // plan leve: la voz terminó, volver al perfil de reposo
    return ok;
}

bool AIEngine::streamElevenLabs(const String &text, AudioStreamCallback cb) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.tts_api_key) == 0) {
        CLOGE(TAG, "ElevenLabs API key missing!");
        return false;
    }

    const char *host = "api.elevenlabs.io";
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    if (!client.connect(host, 443)) {
        CLOGE(TAG, "Connection to ElevenLabs failed!");
        return false;
    }

    JsonDocument doc;
    doc["text"] = text;
    doc["model_id"] = "eleven_multilingual_v2";
    JsonObject voiceSettings = doc["voice_settings"].to<JsonObject>();
    voiceSettings["stability"] = 0.5;
    voiceSettings["similarity_boost"] = 0.8;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String voiceId = strlen(cfg.elevenlabs_voice) > 0 ? String(cfg.elevenlabs_voice) : String(ELEVENLABS_DEFAULT_VOICE);
    String path = "/v1/text-to-speech/" + voiceId + "/stream?output_format=pcm_16000";

    client.println("POST " + path + " HTTP/1.1");
    client.println("Host: " + String(host));
    client.println("xi-api-key: " + String(cfg.tts_api_key));
    client.println("Content-Type: application/json");
    client.println("Accept: audio/pcm");
    client.println("Content-Length: " + String(jsonPayload.length()));
    client.println("Connection: close");
    client.println();
    client.print(jsonPayload);

    int statusCode = 0;
    bool isChunked = false;
    unsigned long hdrStart = millis();
    while (client.connected() || client.available()) {
        if (!client.available()) {
            if (millis() - hdrStart > 8000) {
                CLOGE(TAG, "streamAudioData: timeout esperando cabecera HTTP");
                client.stop();
                return false;
            }
            delay(10);
            continue;
        }
        hdrStart = millis();
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGE(TAG, "ElevenLabs returned HTTP %d", statusCode);
        String errBody = client.readString();
        CLOGE(TAG, "Error details: %s", errBody.c_str());
        client.stop();
        return false;
    }

    // Guarda anti-ruido: si el body empieza con JSON/HTML en vez de PCM crudo
    // (error de API con HTTP 200 o proxy intermedio), NO reproducirlo como audio.
    // El peek solo vale en modo NO-chunked: en chunked esos 64 bytes incluirían
    // framing y romperían el parser; además se entregan al DAC DESPUÉS del
    // startPlayback (antes se escribían con el I2S sin configurar).
    uint8_t peek[64];
    size_t peekN = 0;
    if (!isChunked) {
        unsigned long peekStart = millis();
        while (!client.available() && client.connected() && (millis() - peekStart < 3000)) delay(5);
        if (client.available()) {
            peekN = client.readBytes((char *)peek, min(sizeof(peek), (size_t)client.available()));
            if (looksLikeJsonError(peek, peekN)) {
                CLOGE(TAG, "streamAudioData: el body es texto/JSON, no PCM. Abortando sin reproducir.");
                client.stop();
                return false;
            }
        }
    }

    CLOGI(TAG, "ElevenLabs streaming started (chunked: %d)", isChunked);
    return streamAudioData(client, 16000, cb, isChunked, peekN > 0 ? peek : nullptr, peekN);
}

bool AIEngine::streamOpenAiTts(const String &text, AudioStreamCallback cb) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.tts_api_key) == 0) {
        CLOGE(TAG, "OpenAI API key missing!");
        return false;
    }

    const char *host = "api.openai.com";
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);
    if (!client.connect(host, 443)) {
        CLOGE(TAG, "Connection to OpenAI failed!");
        return false;
    }

    JsonDocument doc;
    doc["model"] = "tts-1";
    doc["input"] = text;
    doc["voice"] = "nova";
    doc["response_format"] = "pcm"; // 24kHz 16-bit mono PCM

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /v1/audio/speech HTTP/1.1");
    client.println("Host: " + String(host));
    client.println("Authorization: Bearer " + String(cfg.tts_api_key));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(jsonPayload.length()));
    client.println("Connection: close");
    client.println();
    client.print(jsonPayload);

    int statusCode = 0;
    bool isChunked = false;
    unsigned long oaiHdrStart = millis();
    while (client.connected() || client.available()) {
        if (!client.available()) {
            if (millis() - oaiHdrStart > 8000) {
                CLOGE(TAG, "OpenAI TTS: timeout esperando cabecera HTTP");
                client.stop();
                return false;
            }
            delay(10);
            continue;
        }
        oaiHdrStart = millis();
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGE(TAG, "OpenAI TTS returned HTTP %d", statusCode);
        String errBody = client.readString();
        CLOGE(TAG, "Error details: %s", errBody.c_str());
        client.stop();
        return false;
    }

    CLOGI(TAG, "OpenAI TTS streaming started at 24kHz (chunked: %d)", isChunked);
    return streamAudioData(client, 24000, cb, isChunked);
}

bool AIEngine::streamVoiceRss(const String &text, AudioStreamCallback cb) {
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.tts_api_key) == 0) {
        CLOGE(TAG, "VoiceRSS API key missing!");
        return false;
    }

    const char *host = "api.voicerss.org";
    WiFiClient client;
    client.setTimeout(20);
    if (!client.connect(host, 80)) {
        CLOGE(TAG, "Connection to VoiceRSS failed!");
        return false;
    }

    String encodedText = "";
    for (size_t i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text.charAt(i);
        if (isalnum((char)c)) encodedText += (char)c;
        else if (c == ' ') encodedText += '+';
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encodedText += buf;
        }
    }

    String path = "/?key=" + String(cfg.tts_api_key) + "&hl=es-es&c=WAV&f=16khz_16bit_mono&src=" + encodedText;
    client.println("GET " + path + " HTTP/1.1");
    client.println("Host: " + String(host));
    client.println("Connection: close");
    client.println();

    int statusCode = 0;
    unsigned long vrssHdrStart = millis();
    while (client.connected() || client.available()) {
        if (!client.available()) {
            if (millis() - vrssHdrStart > 8000) {
                CLOGE(TAG, "VoiceRSS: timeout esperando cabecera HTTP");
                client.stop();
                return false;
            }
            delay(10);
            continue;
        }
        vrssHdrStart = millis();
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    if (statusCode != 200) {
        CLOGE(TAG, "VoiceRSS returned HTTP %d", statusCode);
        client.stop();
        return false;
    }

    // Lee y valida los 44 bytes de cabecera WAV de forma bloqueante con timeout.
    // El loop anterior (`if (available) read`) casi nunca encontraba los bytes a
    // tiempo por latencia TLS: dejaba la cabecera a medias y el "RIFF....WAVEfmt"
    // sonaba como un chasquido a full-scale. Y si VoiceRSS devuelve texto
    // "ERROR: ...", reproducirlo como PCM es ruido saturado horrible.
    uint8_t wavHdr[44];
    size_t hdrGot = 0;
    unsigned long hdrWait = millis();
    while (hdrGot < sizeof(wavHdr) && (millis() - hdrWait < 5000)) {
        if (client.available()) {
            int c = client.read();
            if (c < 0) break;
            wavHdr[hdrGot++] = (uint8_t)c;
            hdrWait = millis();
        } else {
            if (!client.connected()) break;
            delay(2);
        }
    }
    if (hdrGot < sizeof(wavHdr)) {
        CLOGE(TAG, "VoiceRSS: cabecera WAV incompleta (%u/44 bytes). Abortando sin reproducir.", hdrGot);
        client.stop();
        return false;
    }
    if (looksLikeJsonError(wavHdr, sizeof(wavHdr))) {
        CLOGE(TAG, "VoiceRSS devolvió texto de error en vez de WAV. Abortando sin reproducir.");
        client.stop();
        return false;
    }
    if (!(wavHdr[0] == 'R' && wavHdr[1] == 'I' && wavHdr[2] == 'F' && wavHdr[3] == 'F')) {
        CLOGE(TAG, "VoiceRSS: sin magia RIFF (respuesta corrupta). Abortando sin reproducir.");
        client.stop();
        return false;
    }

    CLOGI(TAG, "VoiceRSS streaming started at 16kHz");
    return streamAudioData(client, 16000, cb, false);
}

// Descarga el MP3 de UNA frase de Google Translate TTS (con gates anti-ruido).
// Devuelve true con *outData/*outSize (el llamador libera con free()).
// outCode: HTTP visto (0 = fallo de red/timeout); outTimedOut: 1 si timeout.
static bool fetchGooglePhraseMp3(const String &encodedText, uint8_t **outData,
                                  size_t *outSize, int *outCode, int *outTimedOut) {
    *outData = nullptr;
    *outSize = 0;
    *outCode = 0;
    *outTimedOut = 0;
    const char *host = "translate.google.com";
    WiFiClientSecure gCli;
    gCli.setInsecure();
    gCli.setTimeout(12);

    if (!gCli.connect(host, 443)) {
        CLOGE(TAG, "Connection to Google Free TTS failed!");
        *outTimedOut = 1;
        return false;
    }

    gCli.print("GET /translate_tts?ie=UTF-8&tl=" + String(netManager.getConfig().tts_language) + "&client=tw-ob&q=" + encodedText + " HTTP/1.1\r\n");
    gCli.print("Host: translate.google.com\r\n");
    gCli.print("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n");
    gCli.print("Accept: audio/mpeg, */*\r\n");
    gCli.print("Connection: close\r\n\r\n");

    int statusCode = 0;
    bool isChunked = false;
    bool contentIsAudio = false;
    unsigned long waitHeaderStart = millis();

    while (gCli.connected() || gCli.available()) {
        if (!gCli.available()) {
            if (millis() - waitHeaderStart > 6000) {
                CLOGE(TAG, "Timeout waiting for Google Free TTS header");
                break;
            }
            delay(10);
            continue;
        }

        String line = gCli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1, spaceIdx + 4).toInt();
            }
        } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        } else if (line.startsWith("Content-Type:")) {
            String ct = line.substring(13);
            ct.toLowerCase();
            if (ct.indexOf("audio") >= 0 || ct.indexOf("mpeg") >= 0) contentIsAudio = true;
        }
        if (statusCode > 0 && line.length() == 0) break;
    }

    *outCode = statusCode;
    if (statusCode == 0) *outTimedOut = 1;
    if (statusCode != 200) {
        CLOGW(TAG, "Google Free TTS returned HTTP %d", statusCode);
        gCli.stop();
        return false;
    }
    if (!contentIsAudio) {
        CLOGW(TAG, "Google Free TTS sin Content-Type de audio (bloqueo/captcha?).");
        gCli.stop();
        return false;
    }

    size_t mp3Size = readHttpBinaryBody(gCli, isChunked, outData);
    gCli.stop();

    if (!*outData || mp3Size < 128) {
        if (*outData) { free(*outData); *outData = nullptr; }
        *outSize = 0;
        *outTimedOut = 1; // truncado = reintentable
        return false;
    }
    // Guarda anti-saturación: un HTML de error/captcha (>128 bytes) no tiene
    // sync MP3; decodificarlo a ciegas genera ráfagas a full-scale.
    if (looksLikeJsonError(*outData, mp3Size) || !looksLikeMp3(*outData, mp3Size)) {
        CLOGW(TAG, "Google Free TTS no es MP3 válido (error HTML/JSON?).");
        free(*outData);
        *outData = nullptr;
        *outSize = 0;
        return false;
    }
    *outSize = mp3Size;
    return true;
}

// Escribe `samples` muestras de silencio (@24kHz) en chunks de stack.
static void writeGoogleSilence(size_t samples) {
    uint8_t z[2048];
    memset(z, 0, sizeof(z));
    size_t bytes = samples * sizeof(int16_t);
    while (bytes > 0) {
        size_t n = bytes > sizeof(z) ? sizeof(z) : bytes;
        audio.writePcmChunk(z, n);
        bytes -= n;
    }
}

bool AIEngine::streamGoogleFreeTts(const String &text, AudioStreamCallback cb) {
    if (text.length() == 0) return false;

    // 1. Split text into manageable sentence/phrase chunks (max ~150 chars each for Google Translate TTS API)
    std::vector<String> phrases;
    int startIdx = 0;
    int textLen = text.length();

    while (startIdx < textLen) {
        while (startIdx < textLen && (text.charAt(startIdx) == ' ' || text.charAt(startIdx) == '\n' || text.charAt(startIdx) == '\r')) {
            startIdx++;
        }
        if (startIdx >= textLen) break;

        int remaining = textLen - startIdx;
        if (remaining <= 150) {
            phrases.push_back(text.substring(startIdx));
            break;
        }

        int targetEnd = startIdx + 150;
        int bestCut = -1;

        // Try sentence punctuation: '.', '!', '?', ';'
        for (int i = targetEnd; i >= startIdx + 40; i--) {
            char c = text.charAt(i);
            if ((c == '.' || c == '!' || c == '?' || c == ';') && (i + 1 == textLen || text.charAt(i + 1) == ' ')) {
                bestCut = i + 1;
                break;
            }
        }

        // Try comma or colon
        if (bestCut == -1) {
            for (int i = targetEnd; i >= startIdx + 40; i--) {
                char c = text.charAt(i);
                if ((c == ',' || c == ':') && (i + 1 == textLen || text.charAt(i + 1) == ' ')) {
                    bestCut = i + 1;
                    break;
                }
            }
        }

        // Try space
        if (bestCut == -1) {
            for (int i = targetEnd; i >= startIdx + 30; i--) {
                if (text.charAt(i) == ' ') {
                    bestCut = i;
                    break;
                }
            }
        }

        if (bestCut == -1) {
            bestCut = targetEnd;
        }

        String part = text.substring(startIdx, bestCut);
        part.trim();
        if (part.length() > 0) {
            phrases.push_back(part);
        }
        startIdx = bestCut;
    }

    if (phrases.empty()) return false;

    CLOGI(TAG, "Google Free TTS: streaming %d phrase(s)...", (int)phrases.size());

    // Allocate MP3 decoder and PCM buffer on PSRAM/heap once for the entire session
    mp3dec_t *mp3d = (mp3dec_t *)(psramFound() ? ps_malloc(sizeof(mp3dec_t)) : malloc(sizeof(mp3dec_t)));
    short *pcm = (short *)(psramFound() ? ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(short)) : malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(short)));
    if (!mp3d || !pcm) {
        CLOGE(TAG, "Google Free TTS: out of memory for MP3 decoder structures!");
        if (mp3d) free(mp3d);
        if (pcm) free(pcm);
        return false;
    }

    bool playbackStarted = false;
    int frameCounter = 0;
    // activeHz vive FUERA del loop de frases: persiste entre frases para detectar
    // un cambio REAL de sample-rate. (Bug anterior: declarado dentro del loop,
    // siempre valía 0 y re-clockeaba el I2S en cada frase = cortes entre frases.)
    uint32_t activeHz = 0;

    for (size_t p = 0; p < phrases.size(); p++) {
        if (g_ttsStopRequested) { CLOGI(TAG, "Google Free TTS: frenado por BOOT"); break; }
        String cleanText = phrases[p];
        cleanText.trim();
        if (cleanText.length() == 0) continue;

        CLOGI(TAG, "Google Free TTS phrase [%d/%d]: '%s'", (int)(p + 1), (int)phrases.size(), cleanText.c_str());

        // URL Encode
        String encodedText = "";
        for (size_t i = 0; i < cleanText.length(); i++) {
            uint8_t c = (uint8_t)cleanText.charAt(i);
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
                encodedText += (char)c;
            } else if (c == ' ') {
                encodedText += "+";
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", c);
                encodedText += hex;
            }
        }

        // Fetch con UN reintento (backoff vv_retry_plan): en la calle cada
        // frase se perdía sin pelearla y dejaba huecos = "inentendible".
        // Un 200-sin-audio se trata como 503 (reintentable); 400/401/403/404 no.
        // En cuento, UN solo intento por frase: si el TTS está caído, cada
        // reintento es silencio acumulado y el relato se hace eterno.
        uint8_t *mp3Data = nullptr;
        size_t mp3Size = 0;
        bool fetched = false;
        int maxAtt = s_storyVoice ? 1 : 2;
        for (int att = 0; att < maxAtt && !fetched; att++) {
            if (att > 0) {
                CLOGI(TAG, "Google Free TTS phrase %d: reintento %d...", (int)(p + 1), att + 1);
            }
            int code = 0, timedOut = 0;
            fetched = fetchGooglePhraseMp3(encodedText, &mp3Data, &mp3Size, &code, &timedOut);
            if (!fetched) {
                int planCode = (code == 200) ? 503 : code;
                vv_retry_t rp = vv_retry_plan(att, planCode, (timedOut || code == 200) ? 1 : 0);
                if (!rp.retry) break;
                delay(rp.backoff_ms);
            }
        }
        if (!fetched) {
            // Hueco con pausa natural: 0.5 s de silencio si ya venía sonando
            // (una pausa se percibe; un corte seco a mitad de frase no).
            if (playbackStarted) writeGoogleSilence(12000);
            continue;
        }

        // Buffer PSRAM para decodificar la frase completa antes de enviar a I2S:
        // Permite medir el pico real de la frase entera y normalizarla linealmente
        // a 22000 (-3.5 dBFS) sin saltos de volumen, eliminando la saturación en celular/batería.
        size_t maxPhraseSamples = 24000 * 12; // hasta 12s de audio @ 24kHz (~576 KB en PSRAM)
        int16_t *phrasePcm = (int16_t *)(psramFound() ? ps_malloc(maxPhraseSamples * sizeof(int16_t)) : malloc(maxPhraseSamples * sizeof(int16_t)));
        size_t totalDecoded = 0;
        uint32_t phraseHz = 24000;

        mp3dec_init(mp3d);
        mp3dec_frame_info_t info;
        size_t offset = 0;
        int badFrames = 0;
        int totalBad = 0; // basura acumulada en TODA la frase (truncados del hotspot)

        while (offset < mp3Size) {
            int samples = mp3dec_decode_frame(mp3d, mp3Data + offset, mp3Size - offset, pcm, &info);
            if (info.frame_bytes > 0) {
                offset += info.frame_bytes;
            } else {
                // Resync acotado: un MP3 truncado por WiFi no debe escanearse entero
                // byte a byte (cada byte "válido por azar" era un estallido saturado).
                totalBad++;
                if (++badFrames > 128) {
                    CLOGW(TAG, "Google Free TTS phrase %d: demasiados frames inválidos, frase truncada.", (int)(p + 1));
                    break;
                }
                offset++;
                continue;
            }

            if (samples > 0) {
                badFrames = 0;
                // Valida geometría del frame antes de abrir el I2S: un hz corrupto
                // (0 o >48k) o canales imposibles = basura, no audio.
                if (!isPlausibleSampleRate((uint32_t)info.hz) || info.channels < 1 || info.channels > 2) {
                    CLOGW(TAG, "Google Free TTS: frame con geometría inválida (hz=%d ch=%d), descartado.", info.hz, info.channels);
                    totalBad += 8; // un frame "válido" pero imposible pesa como basura
                    continue;
                }
                phraseHz = (uint32_t)info.hz;

                if (phrasePcm && totalDecoded + samples <= maxPhraseSamples) {
                    if (info.channels == 2) {
                        for (int i = 0; i < samples; i++) {
                            phrasePcm[totalDecoded + i] = (int16_t)(((int32_t)pcm[i * 2] + (int32_t)pcm[i * 2 + 1]) / 2);
                        }
                    } else {
                        memcpy(phrasePcm + totalDecoded, pcm, samples * sizeof(int16_t));
                    }
                    totalDecoded += samples;
                }
            }
        }

        free(mp3Data);

        // Integridad de frase: MP3 truncado por el hotspot = resync masivo =
        // ruido "saturado/inentendible". Mejor omitir la frase que reproducir basura.
        // Si ya venía sonando, 0.5 s de silencio para que no se note el hueco.
        if (totalBad > 256 || totalDecoded < 2400) {
            CLOGW(TAG, "Google Free TTS phrase %d corrupta (bad=%d, muestras=%u), omitida.",
                  (int)(p + 1), totalBad, (unsigned)totalDecoded);
            if (phrasePcm) { free(phrasePcm); phrasePcm = nullptr; }
            if (playbackStarted) writeGoogleSilence(12000);
            continue;
        }

        if (phrasePcm && totalDecoded > 0) {
            int32_t peak = 0;
            for (size_t i = 0; i < totalDecoded; i++) {
                int32_t a = phrasePcm[i] < 0 ? -(int32_t)phrasePcm[i] : (int32_t)phrasePcm[i];
                if (a > peak) peak = a;
            }

            if (peak >= 200) {
                // Target 22000 (-3.5 dBFS): Margen de potencia óptimo para el NS4150B y batería
                    int32_t gain256 = (22000L * 256) / peak;
                    if (gain256 > 256) gain256 = 256; // nunca boost (crujia picos)
                    if (gain256 < 128) gain256 = 128; // atenúa audios calientes a 0 dBFS

                for (size_t i = 0; i < totalDecoded; i++) {
                    phrasePcm[i] = (int16_t)(((int32_t)phrasePcm[i] * gain256) / 256);
                }

                if (!playbackStarted) {
                    CLOGI(TAG, "Google Free TTS audio: %u Hz, pico %d, gain %d/256. Iniciando reproducción...", phraseHz, peak, gain256);
                    audio.startPlayback(phraseHz);
                    playbackStarted = true;
                    activeHz = phraseHz;
                    if (cb) cb(true);
                } else if (phraseHz != activeHz) {
                    CLOGI(TAG, "Google Free TTS: re-clockeando I2S %u -> %u Hz", activeHz, phraseHz);
                    audio.startPlayback(phraseHz);
                    activeHz = phraseHz;
                }

                const size_t BATCH_SAMPLES = 512;
                for (size_t pos = 0; pos < totalDecoded; pos += BATCH_SAMPLES) {
                    if (g_ttsStopRequested) break; // BOOT mientras habla = callar
                    size_t count = min(BATCH_SAMPLES, totalDecoded - pos);
                    audio.writePcmChunk((const uint8_t *)(phrasePcm + pos), count * sizeof(int16_t));
                    if (cb && (++frameCounter % 6 == 0)) {
                        cb(frameCounter % 12 < 6);
                    }
                }
            } else {
                CLOGW(TAG, "Google Free TTS phrase %d en silencio (pico %d), omitida.", (int)(p + 1), peak);
            }
        }
        if (phrasePcm) free(phrasePcm);
        // Pausa por oración en cuento infantil: cada frase se digiere antes
        // de la siguiente (no aplica si pidieron callar o era la última).
        if (s_storyVoice && !g_ttsStopRequested && p + 1 < phrases.size()) delay(650);
    }

    free(mp3d);
    free(pcm);

    if (playbackStarted) {
        audio.stopPlayback();
        CLOGI(TAG, "Google Free TTS full playback completed successfully!");
        return true;
    } else {
        CLOGW(TAG, "Google Free TTS: no audio frames decoded");
        return false;
    }
}

// ==============================================================================
// DIRECT CLOUD IMAGE GENERATION & DECODING VIA ESP32 ROM TJPGD
// ==============================================================================
struct JpegContext {
    const uint8_t *data;
    size_t len;
    size_t offset;
    uint8_t *epdBuf;
    uint8_t *logoBuf; // destino 64x64 (512B) cuando se decodifica logo
    int logoW;        // ancho escalado de salida (para mapear a 64)
    int logoH;
};

static UINT jdInputFunc(JDEC *jd, BYTE *buff, UINT ndata) {
    JpegContext *ctx = (JpegContext*)jd->device;
    if (!ctx) return 0;
    if (ctx->offset + ndata > ctx->len) {
        ndata = ctx->len - ctx->offset;
    }
    if (buff && ndata > 0) {
        memcpy(buff, ctx->data + ctx->offset, ndata);
    }
    ctx->offset += ndata;
    return ndata;
}

static UINT jdOutputFunc(JDEC *jd, void *bitmap, JRECT *rect) {
    JpegContext *ctx = (JpegContext*)jd->device;
    uint8_t *epdBuf = ctx->epdBuf;
    const uint8_t *rgb = (const uint8_t*)bitmap;
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;

    static const uint8_t bayer4[4][4] = {
        {  15, 195,  60, 240 },
        { 135,  75, 180, 120 },
        { 105, 225,  45, 165 },
        { 210,  90, 150,  30 }
    };

    for (int y = 0; y < h; y++) {
        int py = rect->top + y;
        if (py >= 200) continue;
        for (int x = 0; x < w; x++) {
            int px = rect->left + x;
            if (px >= 200) continue;

            int idx = (y * w + x) * 3;
            uint8_t r = rgb[idx];
            uint8_t g = rgb[idx + 1];
            uint8_t b = rgb[idx + 2];
            uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);

            // Binarizador de Tinta de Alto Contraste para E-Paper 1-bit:
            // 1. Trazos, contornos y zonas oscuras (<= 160) se fijan en NEGRO SOLIDO (color 0)
            //    eliminando los huecos blancos y el efecto de trazo descolorido o punteado.
            // 2. Fondo (>= 215) se fija en BLANCO PURO (1) eliminando motas y suciedad.
            // 3. Zona intermedia (160..215): se escala a 0..255 y se trama con Bayer para suavizar bordes.
            bool isWhite = true;
            if (gray <= 160) {
                isWhite = false; // Tinta negra pura y sólida
            } else if (gray < 215) {
                uint8_t scaled = (uint8_t)(((int)(gray - 160) * 255) / (215 - 160));
                uint8_t thresh = bayer4[py & 3][px & 3];
                isWhite = (scaled > thresh);
            }

            int byteIdx = (py * 200 + px) / 8;
            int bitPos = 7 - (px % 8);
            if (isWhite) {
                epdBuf[byteIdx] |= (1 << bitPos);
            } else {
                epdBuf[byteIdx] &= ~(1 << bitPos);
            }
        }
    }
    return 1;
}

// Decodifica JPEG a bitmap 1-bit 200x200 en el buffer dado (SIN mostrar).
// Lo usan los slots de precarga del cuento en cascada: la imagen se cocina
// en fondo y se publica con showBitmap recien al avanzar de parte.
static bool decodeJpegToBuffer(const uint8_t *jpgData, size_t jpgLen, uint8_t *outBuf) {
    if (!jpgData || jpgLen < 500 || !outBuf) return false;
    JDEC jd;
    void *workPool = malloc(4096);
    if (!workPool) return false;
    memset(outBuf, 0xFF, EPD_BUFFER_SIZE);

    JpegContext ctx = { jpgData, jpgLen, 0, outBuf, nullptr, 0, 0 };
    JRESULT res = jd_prepare(&jd, jdInputFunc, workPool, 4096, &ctx);
    if (res != JDR_OK) {
        CLOGW(TAG, "jd_prepare fallo con codigo: %d", res);
        free(workPool);
        return false;
    }

    res = jd_decomp(&jd, jdOutputFunc, 0);
    free(workPool);

    if (res != JDR_OK) {
        CLOGW(TAG, "jd_decomp fallo con codigo: %d", res);
        return false;
    }
    return true;
}

static bool decodeAndRenderJpeg(const uint8_t *jpgData, size_t jpgLen) {
    if (!jpgData || jpgLen < 500) return false;
    // Decodificar a buffer TEMPORAL: escribir directo al buffer del driver
    // durante la descarga/decode corría sin mutex y mezclaba mitades con la
    // animación de Core 0 (imagen rota). Se publica con showBitmap al final.
    uint8_t *tmpBuf = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
    if (!tmpBuf) {
        return false;
    }
    if (!decodeJpegToBuffer(jpgData, jpgLen, tmpBuf)) {
        free(tmpBuf);
        return false;
    }

    // Refresco completo de alto contraste vía showBitmap (drena cola + mutex).
    epd.showBitmap(tmpBuf, true);
    free(tmpBuf);
    extern bool g_textHold;
    g_textHold = true;
    return true;
}

// --- REFERENCIA REAL (Wikipedia) para logos/escudos/simbolos/banderas ---
// La IA generativa INVENTA los emblemas (el "conejo" en vez del simbolo PR).
// Esto detecta el pedido, busca en Wikipedia (tolera errores de transcript),
// muestra la foto posta si es JPG y devuelve el resumen real para la voz.
bool AIEngine::isReferenceImageRequest(const String &transcript, String &outSubject) {
    String t = transcript;
    t.toLowerCase();
    t.replace("á", "a"); t.replace("é", "e"); t.replace("í", "i");
    t.replace("ó", "o"); t.replace("ú", "u"); t.replace("ñ", "n");
    t.trim();
    bool hit = (t.indexOf("logo") >= 0 || t.indexOf("escudo") >= 0 ||
                t.indexOf("simbolo") >= 0 || t.indexOf("bandera") >= 0 ||
                t.indexOf("insignia") >= 0 || t.indexOf("emblema") >= 0 ||
                t.indexOf("retrato") >= 0 || t.indexOf("foto de") >= 0 ||
                t.indexOf("foto real") >= 0 || t.indexOf("imagen real") >= 0 ||
                t.indexOf("mostrame el") >= 0 || t.indexOf("mostrame la") >= 0 ||
                t.indexOf("mostrame una") >= 0 || t.indexOf("mostrame un") >= 0);
    if (!hit) return false;
    String s = t;
    static const char *STRIP[] = {
        "mostrame ", "mostrar ", "mostra ", "buscame ", "busca ", "poneme ", "pon ",
        "dibuja ", "dibujame ", "dibujo ", "quiero ver ",
        "foto real ", "foto de ", "foto ", "imagen real ", "imagen ",
        "el ", "la ", "los ", "las ", "de ", "del ",
        "logo ", "logotipo ", "escudo ", "simbolo ", "bandera ",
        "insignia ", "emblema ", "retrato ", "un ", "una ",
        "en pantalla ", "en el display "
    };
    for (int pass = 0; pass < 4; pass++) {
        bool changed = false;
        for (size_t i = 0; i < sizeof(STRIP) / sizeof(STRIP[0]); i++) {
            int p = s.indexOf(STRIP[i]);
            if (p >= 0) { s.remove(p, strlen(STRIP[i])); changed = true; }
        }
        if (!changed) break;
    }
    while (s.length() > 0) {
        char c = s.charAt(s.length() - 1);
        if (c == '.' || c == ',' || c == '!' || c == '?' || c == ':') s.remove(s.length() - 1);
        else break;
    }
    s.trim();
    if (s.length() < 3) return false;
    outSubject = s;
    return true;
}

// GET JSON chico a Wikipedia (solo 200 + cuerpo). Lee headers y parsea
// el JSON directo del stream con ArduinoJson (sin acumular String gigante).
static bool wikiGetJson(const String &path, JsonDocument &doc, uint32_t timeoutMs = 15000) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(12);
    if (!cli.connect("es.wikipedia.org", 443)) return false;
    cli.print(String("GET ") + path + " HTTP/1.1\r\nHost: es.wikipedia.org\r\n"
              "User-Agent: Copito/1.0 (ESP32-S3)\r\nAccept: application/json\r\n"
              "Connection: close\r\n\r\n");
    unsigned long st = millis();
    while (!cli.available() && (millis() - st < 12000)) delay(30);
    if (!cli.available()) { cli.stop(); return false; }
    String status = cli.readStringUntil('\n');
    if (status.indexOf("200") < 0) { cli.stop(); return false; }
    while (cli.connected()) {
        String h = cli.readStringUntil('\n');
        h.trim();
        if (h.length() == 0) break;
        if (millis() - st > timeoutMs) { cli.stop(); return false; }
    }
    DeserializationError e = deserializeJson(doc, cli);
    cli.stop();
    return (e == DeserializationError::Ok);
}

bool AIEngine::fetchWikipediaReference(const String &subject, String &outSummary, bool &outImageShown) {
    outSummary = "";
    outImageShown = false;
    if (subject.length() < 3) return false;
    // 1. Resolver titulo con busqueda difusa (tolera "redonditos de ricota").
    DynamicJsonDocument d1(4096);
    String p1 = String("/w/api.php?action=query&format=json&list=search&srsearch=")
        + urlEncode(subject) + "&srlimit=1&srprop=";
    if (!wikiGetJson(p1, d1)) {
        CLOGW(TAG, "Wiki: sin respuesta de busqueda para '%s'", subject.c_str());
        return false;
    }
    const char *title = d1["query"]["search"][0]["title"];
    if (!title || !*title) {
        CLOGW(TAG, "Wiki: sin resultados para '%s'", subject.c_str());
        return false;
    }
    CLOGI(TAG, "Wiki: '%s' -> articulo '%s'", subject.c_str(), title);
    // 2. Resumen introductorio + miniatura en una sola llamada.
    DynamicJsonDocument d2(8192);
    String p2 = String("/w/api.php?action=query&format=json&prop=extracts|pageimages")
        + "&exintro=1&explaintext=1&exchars=900&piprop=thumbnail&pithumbsize=500&titles="
        + urlEncode(String(title));
    if (!wikiGetJson(p2, d2)) return false;
    JsonObject pages = d2["query"]["pages"].as<JsonObject>();
    const char *extract = nullptr;
    const char *thumb = nullptr;
    for (JsonPair kv : pages) {
        JsonObject pg = kv.value().as<JsonObject>();
        if (pg.containsKey("missing")) continue;
        extract = pg["extract"];
        thumb = pg["thumbnail"]["source"];
        break;
    }
    if (extract && *extract) {
        outSummary = String(extract);
        outSummary.trim();
        // Recortar a ~380 caracteres en borde de frase para el TTS.
        if (outSummary.length() > 380) {
            int cut = 380;
            for (int i = 380; i > 200; i--) {
                char c = outSummary.charAt(i);
                if (c == '.' || c == ';') { cut = i + 1; break; }
            }
            outSummary = outSummary.substring(0, cut);
        }
    }
    // 3. Miniatura JPG (los PNG/SVG de logos no los decodifica el TJPGD:
    // en ese caso igual sirve el resumen real por voz).
    if (thumb && *thumb) {
        String tu = String(thumb);
        String low = tu;
        low.toLowerCase();
        if (low.indexOf(".jpg") >= 0 || low.indexOf(".jpeg") >= 0) {
            String host, path;
            String u = tu;
            if (u.startsWith("https://")) u = u.substring(8);
            int slash = u.indexOf('/');
            if (slash > 0) {
                host = u.substring(0, slash);
                path = u.substring(slash);
                WiFiClientSecure icli;
                icli.setInsecure();
                icli.setTimeout(15);
                if (icli.connect(host.c_str(), 443)) {
                    icli.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host
                               + "\r\nUser-Agent: Copito/1.0 (ESP32-S3)\r\nConnection: close\r\n\r\n");
                    unsigned long st = millis();
                    while (!icli.available() && (millis() - st < 12000)) delay(30);
                    if (icli.available()) {
                        String st2 = icli.readStringUntil('\n');
                        size_t want = 0;
                        if (st2.indexOf("200") >= 0) {
                            while (icli.connected()) {
                                String h = icli.readStringUntil('\n');
                                h.trim();
                                if (h.length() == 0) break;
                                h.toLowerCase();
                                if (h.startsWith("content-length:")) {
                                    want = (size_t)h.substring(15).toInt();
                                }
                            }
                            if (want > 500 && want <= 96 * 1024) {
                                uint8_t *jb = (uint8_t *)(psramFound() ? ps_malloc(want) : malloc(want));
                                if (jb) {
                                    size_t got = 0;
                                    while (got < want && (millis() - st < 25000)) {
                                        size_t av = icli.available();
                                        if (av == 0) {
                                            if (!icli.connected()) break;
                                            delay(10);
                                            continue;
                                        }
                                        size_t n = av > (want - got) ? (want - got) : av;
                                        int r = icli.read(jb + got, n);
                                        if (r > 0) got += (size_t)r;
                                        else delay(10);
                                    }
                                    if (got == want && decodeAndRenderJpeg(jb, got)) {
                                        outImageShown = true;
                                        CLOGI(TAG, "Wiki: imagen real mostrada (%u bytes)", (unsigned)got);
                                    }
                                    free(jb);
                                }
                            }
                        }
                    }
                    icli.stop();
                }
            }
        } else {
            CLOGI(TAG, "Wiki: miniatura no-JPG (logo vectorial), solo resumen por voz");
        }
    }
    return (outSummary.length() > 0 || outImageShown);
}

// --- TAPA/LOGO CHICO JUNTO AL TITULO (musica y radio) ---
// iTunes Search (gratis, sin key) da la tapa del disco; para radios se usa
// la miniatura de Wikipedia. Se baja en una tarea de fondo mientras suena:
// si tarda o no hay coincidencia, queda la pantalla de titulo como antes.
// El logo se reduce a 64x64 1-bit con dither y se muestra AL LADO del titulo.
static bool downloadJpegFile(const String &host, const String &path,
                             uint8_t **outBuf, size_t *outLen, size_t cap) {
    *outBuf = nullptr;
    *outLen = 0;
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(15);
    if (!cli.connect(host.c_str(), 443)) return false;
    cli.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host
              + "\r\nUser-Agent: Copito/1.0 (ESP32-S3)\r\nConnection: close\r\n\r\n");
    unsigned long st = millis();
    while (!cli.available() && (millis() - st < 12000)) delay(30);
    if (!cli.available()) { cli.stop(); return false; }
    String sl = cli.readStringUntil('\n');
    if (sl.indexOf("200") < 0) { cli.stop(); return false; }
    size_t want = 0;
    while (cli.connected()) {
        String h = cli.readStringUntil('\n');
        h.trim();
        if (h.length() == 0) break;
        String hl = h;
        hl.toLowerCase();
        if (hl.startsWith("content-length:")) want = (size_t)h.substring(15).toInt();
        if (millis() - st > 15000) { cli.stop(); return false; }
    }
    if (want < 500 || want > cap) { cli.stop(); return false; }
    uint8_t *jb = (uint8_t *)(psramFound() ? ps_malloc(want) : malloc(want));
    if (!jb) { cli.stop(); return false; }
    size_t got = 0;
    while (got < want && (millis() - st < 25000)) {
        size_t av = cli.available();
        if (av == 0) {
            if (!cli.connected()) break;
            delay(10);
            continue;
        }
        size_t n = av > (want - got) ? (want - got) : av;
        int r = cli.read(jb + got, n);
        if (r > 0) got += (size_t)r;
        else delay(10);
    }
    cli.stop();
    if (got != want) { free(jb); return false; }
    *outBuf = jb;
    *outLen = got;
    return true;
}

static UINT jdLogoOutputFunc(JDEC *jd, void *bitmap, JRECT *rect) {
    JpegContext *ctx = (JpegContext *)jd->device;
    const uint8_t *rgb = (const uint8_t *)bitmap;
    int w = rect->right - rect->left + 1;
    int h = rect->bottom - rect->top + 1;
    static const uint8_t bayer4[4][4] = {
        {  15, 195,  60, 240 },
        { 135,  75, 180, 120 },
        { 105, 225,  45, 165 },
        { 210,  90, 150,  30 }
    };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int px = rect->left + x;
            int py = rect->top + y;
            int tx = (px * 64) / ctx->logoW;
            int ty = (py * 64) / ctx->logoH;
            if (tx < 0 || tx >= 64 || ty < 0 || ty >= 64) continue;
            int idx = (y * w + x) * 3;
            uint8_t gray = (uint8_t)((rgb[idx] * 77 + rgb[idx + 1] * 150 + rgb[idx + 2] * 29) >> 8);
            bool isWhite = true;
            if (gray <= 160) isWhite = false;
            else if (gray < 215) {
                uint8_t scaled = (uint8_t)(((int)(gray - 160) * 255) / (215 - 160));
                isWhite = (scaled > bayer4[ty & 3][tx & 3]);
            }
            if (!isWhite) {
                ctx->logoBuf[ty * 8 + (tx >> 3)] &= ~(1 << (7 - (tx & 7)));
            }
        }
    }
    return 1;
}

// JPG -> logo 64x64. Rechaza imagenes enanas (<40px: no se veria nada).
// (LogoCtx quedo absorbido en JpegContext: jd->device debe seguir siendo
// el contexto de entrada durante todo el decode.)
static bool decodeJpegToLogo(const uint8_t *jpg, size_t len, uint8_t *logoOut) {
    if (!jpg || len < 500 || !logoOut) return false;
    memset(logoOut, 0xFF, 512);
    JDEC jd;
    void *workPool = malloc(4096);
    if (!workPool) return false;
    JpegContext ctx = { jpg, len, 0, nullptr, logoOut, 0, 0 };
    if (jd_prepare(&jd, jdInputFunc, workPool, 4096, &ctx) != JDR_OK) {
        free(workPool);
        return false;
    }
    int bigger = jd.width > jd.height ? jd.width : jd.height;
    if (bigger < 40) { free(workPool); return false; }
    int scale = 0;
    if (bigger > 384) scale = 3;
    else if (bigger > 192) scale = 2;
    else if (bigger > 96) scale = 1;
    ctx.logoW = jd.width >> scale;
    ctx.logoH = jd.height >> scale;
    if (ctx.logoW <= 0 || ctx.logoH <= 0) { free(workPool); return false; }
    JRESULT res = jd_decomp(&jd, jdLogoOutputFunc, scale);
    free(workPool);
    return (res == JDR_OK);
}

static String normAsciiLower(String s) {
    s.toLowerCase();
    s.replace("á", "a"); s.replace("é", "e"); s.replace("í", "i");
    s.replace("ó", "o"); s.replace("ú", "u"); s.replace("ü", "u");
    s.replace("ñ", "n");
    return s;
}

// La tapa sirve solo si nombra lo pedido (antes un logo cualquiera que
// un dibujo inventado: si no coincide, no se muestra nada).
static bool logoMatchesQuery(const String &query, const String &haystackRaw) {
    String hay = normAsciiLower(haystackRaw);
    String q = normAsciiLower(query);
    char word[40];
    int wi = 0;
    for (size_t i = 0; i <= (size_t)q.length(); i++) {
        char c = (i < (size_t)q.length()) ? q.charAt(i) : ' ';
        bool sep = !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9');
        if (!sep && wi < 39) {
            word[wi++] = c;
        } else if (wi > 0) {
            word[wi] = '\0';
            if (wi >= 4 && hay.indexOf(word) >= 0) return true;
            wi = 0;
        }
    }
    return false;
}

static bool splitHttpsUrl(const String &url, String &host, String &path) {
    String u = url;
    if (u.startsWith("https://")) u = u.substring(8);
    else if (u.startsWith("http://")) u = u.substring(7);
    int slash = u.indexOf('/');
    if (slash <= 0) return false;
    host = u.substring(0, slash);
    path = u.substring(slash);
    return true;
}

bool AIEngine::fetchMusicLogo(const String &query, const String &title, uint8_t *logoOut) {
    (void)title;
    if (query.length() < 3 || !logoOut) return false;
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(12);
    if (!cli.connect("itunes.apple.com", 443)) return false;
    String path = String("/search?term=") + urlEncode(query)
        + "&media=music&entity=song&limit=3&country=US";
    cli.print(String("GET ") + path + " HTTP/1.1\r\nHost: itunes.apple.com\r\n"
              "User-Agent: Copito/1.0 (ESP32-S3)\r\nAccept: application/json\r\n"
              "Connection: close\r\n\r\n");
    unsigned long st = millis();
    while (!cli.available() && (millis() - st < 12000)) delay(30);
    if (!cli.available()) { cli.stop(); return false; }
    String sl = cli.readStringUntil('\n');
    if (sl.indexOf("200") < 0) { cli.stop(); return false; }
    while (cli.connected()) {
        String h = cli.readStringUntil('\n');
        h.trim();
        if (h.length() == 0) break;
        if (millis() - st > 15000) { cli.stop(); return false; }
    }
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, cli) != DeserializationError::Ok) { cli.stop(); return false; }
    cli.stop();
    JsonArray res = doc["results"].as<JsonArray>();
    for (JsonObject r : res) {
        const char *track = r["trackName"];
        const char *artist = r["artistName"];
        const char *coll = r["collectionName"];
        const char *art = r["artworkUrl100"];
        if (!art || !*art) continue;
        String hay = String(track ? track : "") + " " + String(artist ? artist : "")
            + " " + String(coll ? coll : "");
        if (!logoMatchesQuery(query, hay)) {
            CLOGI(TAG, "Logo: '%s' no coincide con '%s', sigo", hay.c_str(), query.c_str());
            continue;
        }
        String artUrl = String(art);
        artUrl.replace("100x100bb", "300x300bb");
        String host, apath;
        if (!splitHttpsUrl(artUrl, host, apath)) continue;
        uint8_t *jb = nullptr;
        size_t jl = 0;
        if (!downloadJpegFile(host, apath, &jb, &jl, 96 * 1024)) continue;
        bool ok = decodeJpegToLogo(jb, jl, logoOut);
        free(jb);
        if (ok) {
            CLOGI(TAG, "Logo: tapa de '%s' lista", hay.c_str());
            return true;
        }
    }
    CLOGI(TAG, "Logo: sin tapa confiable para '%s'", query.c_str());
    return false;
}

bool AIEngine::fetchStationLogo(const String &station, uint8_t *logoOut) {
    if (station.length() < 3 || !logoOut) return false;
    DynamicJsonDocument d1(4096);
    String p1 = String("/w/api.php?action=query&format=json&list=search&srsearch=")
        + urlEncode(station) + "&srlimit=1&srprop=";
    if (!wikiGetJson(p1, d1)) return false;
    const char *title = d1["query"]["search"][0]["title"];
    if (!title || !*title) return false;
    DynamicJsonDocument d2(8192);
    String p2 = String("/w/api.php?action=query&format=json&prop=pageimages")
        + "&piprop=thumbnail&pithumbsize=300&titles=" + urlEncode(String(title));
    if (!wikiGetJson(p2, d2)) return false;
    const char *thumb = nullptr;
    JsonObject pages = d2["query"]["pages"].as<JsonObject>();
    for (JsonPair kv : pages) {
        JsonObject pg = kv.value().as<JsonObject>();
        if (pg.containsKey("missing")) continue;
        thumb = pg["thumbnail"]["source"];
        break;
    }
    if (!thumb || !*thumb) return false;
    String tu = String(thumb);
    String low = tu;
    low.toLowerCase();
    if (low.indexOf(".jpg") < 0 && low.indexOf(".jpeg") < 0) return false;
    String host, ipath;
    if (!splitHttpsUrl(tu, host, ipath)) return false;
    uint8_t *jb = nullptr;
    size_t jl = 0;
    if (!downloadJpegFile(host, ipath, &jb, &jl, 96 * 1024)) return false;
    bool ok = decodeJpegToLogo(jb, jl, logoOut);
    free(jb);
    return ok;
}

struct ArtworkJob {
    char query[100];
    char title[96];
    bool radio;
};

static void musicArtworkTask(void *pv) {
    ArtworkJob *job = (ArtworkJob *)pv;
    uint8_t *logo = (uint8_t *)malloc(512);
    bool got = false;
    if (logo) {
        if (job->radio) got = aiEngine.fetchStationLogo(String(job->title), logo);
        else got = aiEngine.fetchMusicLogo(String(job->query), String(job->title), logo);
    }
    // Mostrar solo si SIGUE sonando el mismo tema (si cambio, no pisar).
    if (got && g_musicPlaying && !g_musicStopRequested
        && g_musicEsp32Title == String(job->title)) {
        epd.showMusicLogo(job->title, "SONANDO", logo);
        CLOGI(TAG, "Logo mostrado junto a '%s'", job->title);
    }
    if (logo) free(logo);
    free(job);
    vTaskDelete(NULL);
}

// Lanza la busqueda de tapa en fondo (no frena el arranque de la musica).
static void spawnArtworkTask(const String &query, const String &title, bool radio) {
    ArtworkJob *job = (ArtworkJob *)malloc(sizeof(ArtworkJob));
    if (!job) return;
    strncpy(job->query, query.c_str(), sizeof(job->query) - 1);
    job->query[sizeof(job->query) - 1] = '\0';
    strncpy(job->title, title.c_str(), sizeof(job->title) - 1);
    job->title[sizeof(job->title) - 1] = '\0';
    job->radio = radio;
    if (xTaskCreate(musicArtworkTask, "artwork", 12288, job, 1, NULL) != pdPASS) {
        free(job);
    }
}

bool AIEngine::fetchAndDisplayCloudImage(const String &prompt) {    if (prompt.length() == 0) return false;

    // 1. Intentar primero con el Image Gateway local si esta disponible en la LAN (puerto 8885)
    AppConfig &cfg = netManager.getConfig();
    if (strlen(cfg.tts_local_host1) > 0) {
        WiFiClient lanClient;
        lanClient.setTimeout(2);
        if (lanClient.connect(cfg.tts_local_host1, 8885)) {
            CLOGI(TAG, "Dibujo: Usando Image Gateway LAN en %s:8885...", cfg.tts_local_host1);
            String enc = prompt;
            enc.replace(" ", "%20");
            lanClient.print(String("GET /bitmap?prompt=") + enc + " HTTP/1.1\r\nHost: gateway\r\nConnection: close\r\n\r\n");
            unsigned long st = millis();
            while (!lanClient.available() && (millis() - st < 12000)) delay(20);
            if (lanClient.available()) {
                String line = lanClient.readStringUntil('\n');
                if (line.indexOf("200") >= 0) {
                    while (lanClient.connected()) {
                        String h = lanClient.readStringUntil('\n');
                        h.trim();
                        if (h.length() == 0) break;
                    }
                    uint8_t *tmpBmp = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
                    size_t readBmp = 0;
                    if (tmpBmp) {
                        unsigned long lanLastMs = millis();
                        while ((lanClient.connected() || lanClient.available()) && readBmp < EPD_BUFFER_SIZE) {
                            int r = lanClient.read(tmpBmp + readBmp, EPD_BUFFER_SIZE - readBmp);
                            if (r > 0) {
                                readBmp += (size_t)r;
                                lanLastMs = millis();
                            } else {
                                if (millis() - lanLastMs > 8000) break;
                                delay(5);
                            }
                        }
                    }
                    lanClient.stop();
                    if (tmpBmp && readBmp == EPD_BUFFER_SIZE) {
                        CLOGI(TAG, "Dibujo LAN recibido (5000 bytes). Mostrando en e-Paper...");
                        // Vía showBitmap (drena cola + mutex): escribir directo al
                        // buffer del driver por red corría sin sincronización y
                        // mezclaba mitades con la animación (imagen rota).
                        epd.showBitmap(tmpBmp, true);
                        free(tmpBmp);
                        extern bool g_textHold;
                        g_textHold = true;
                        return true;
                    }
                    if (tmpBmp) free(tmpBmp);
                }
            }
            lanClient.stop();
        }
    }

    // 2. Nube Gratuita Directa (Pollinations AI Turbo) sin requerir ninguna PC
    CLOGI(TAG, "Dibujo Libre: Descargando DIRECTO de la nube gratis para '%s'...", prompt.c_str());
    WiFiClientSecure imgClient;
    imgClient.setInsecure();
    imgClient.setTimeout(18);

    if (!imgClient.connect("image.pollinations.ai", 443)) {
        CLOGW(TAG, "Fallo conexion TLS con image.pollinations.ai:443");
        return false;
    }

    String cleanPrompt = prompt;
    cleanPrompt.replace("á", "a"); cleanPrompt.replace("é", "e"); cleanPrompt.replace("í", "i");
    cleanPrompt.replace("ó", "o"); cleanPrompt.replace("ú", "u"); cleanPrompt.replace("ñ", "n");
    // URL-encode COMPLETO: un "?", "&", comilla o resto UTF-8 del transcript
    // rompía el path y Pollinations devolvía 400 (dibujo pedido -> solo texto).
    String encPrompt = urlEncode(cleanPrompt);
    encPrompt += "%2C%20high%20contrast%20vector%20stencil%20art%2C%20bold%20solid%20black%20ink%20outlines%2C%20clean%20pure%20white%20background%2C%20sharp%20comic%20ink%2C%20no%20shading%2C%20no%20gray%20tones";

    int seed = (int)(millis() & 0x7FFFF);
    String req = String("GET /prompt/") + encPrompt + "?width=200&height=200&model=turbo&nologo=true&seed=" + String(seed) + " HTTP/1.1\r\n" +
                 "Host: image.pollinations.ai\r\n" +
                 "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n" +
                 "Accept: image/jpeg,image/*\r\n" +
                 "Connection: close\r\n\r\n";

    imgClient.print(req);

    unsigned long startMs = millis();
    while (!imgClient.available() && (millis() - startMs < 15000)) {
        delay(50);
    }
    if (!imgClient.available()) {
        CLOGW(TAG, "Timeout esperando respuesta de imagen de la nube");
        imgClient.stop();
        return false;
    }

    String statusLine = imgClient.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0) {
        CLOGW(TAG, "Nube devolvio HTTP no-200: %s", statusLine.c_str());
        imgClient.stop();
        return false;
    }

    bool chunked = false;
    while (imgClient.connected()) {
        String line = imgClient.readStringUntil('\n');
        line.trim();
        if (line.equalsIgnoreCase("transfer-encoding: chunked")) chunked = true;
        if (line.length() == 0) break;
    }

    size_t maxJpg = 48 * 1024;
    uint8_t *jpgBuf = (uint8_t*)ps_malloc(maxJpg);
    if (!jpgBuf) {
        CLOGE(TAG, "Sin memoria PSRAM para buffer JPEG");
        imgClient.stop();
        return false;
    }

    size_t totalJpg = 0;
    unsigned long imgLastMs = millis();
    if (chunked) {
        while ((imgClient.connected() || imgClient.available()) && totalJpg < maxJpg && (millis() - imgLastMs < 15000)) {
            if (!imgClient.available()) {
                delay(5);
                continue;
            }
            String hexLen = imgClient.readStringUntil('\n');
            hexLen.trim();
            if (hexLen.length() == 0) continue;
            int semi = hexLen.indexOf(';');
            if (semi >= 0) hexLen = hexLen.substring(0, semi);
            long chunkLen = strtol(hexLen.c_str(), NULL, 16);
            if (chunkLen <= 0) break;

            size_t bytesRead = 0;
            while (bytesRead < (size_t)chunkLen && totalJpg < maxJpg && (imgClient.connected() || imgClient.available())) {
                size_t toRead = chunkLen - bytesRead;
                if (toRead > 1024) toRead = 1024;
                int r = imgClient.read(jpgBuf + totalJpg, toRead);
                if (r > 0) {
                    bytesRead += r;
                    totalJpg += r;
                    imgLastMs = millis();
                } else {
                    if (millis() - imgLastMs > 12000) break;
                    delay(5);
                }
            }
            if (bytesRead < (size_t)chunkLen) break;
            imgClient.readStringUntil('\n');
        }
    } else {
        while ((imgClient.connected() || imgClient.available()) && totalJpg < maxJpg) {
            int r = imgClient.read(jpgBuf + totalJpg, maxJpg - totalJpg);
            if (r > 0) {
                totalJpg += r;
                imgLastMs = millis();
            } else {
                if (millis() - imgLastMs > 12000) break;
                delay(5);
            }
        }
    }
    imgClient.stop();

    CLOGI(TAG, "Descargados %u bytes JPEG directos de la nube gratis", totalJpg);
    if (totalJpg < 500) {
        free(jpgBuf);
        return false;
    }

    bool ok = decodeAndRenderJpeg(jpgBuf, totalJpg);
    free(jpgBuf);
    return ok;
}

// === CUENTO EN CASCADA: partes JSON + ilustracion + voz narrador ===
bool AIEngine::decodeJpegToBitmap(const uint8_t *jpgData, size_t jpgLen, uint8_t *outBitmap) {
    return decodeJpegToBuffer(jpgData, jpgLen, outBitmap);
}

bool AIEngine::narrateStoryPart(const String &text, AudioStreamCallback cb) {
    s_storyVoice = true; // streamTtsSingle prueba Sulafat lento primero
    // La voz jamás habla etiquetas: [SKETCH:...]/[MODE:...]/[DRAW:...] se
    // vocalizaban ("scheck triple cien..."). parseTags las extrae y deja el
    // texto limpio (2026-09-09: el cuento por portal no pasaba por el parser
    // de voz y hablaba los tags crudos).
    String speak = text;
    {
        CopitoTags dropTags;
        CopitoTools::parseTags(speak, dropTags);
        speak.trim();
    }
    // QA remoto: vista previa de lo que sale por el parlante (caza-tags).
    CLOGI(TAG, "Cuento voz limpia (%u letras): '%s'", (unsigned)speak.length(),
          speak.substring(0, 100).c_str());
    // Ritmo de cuento infantil: 90% de velocidad + pausas por oración, para
    // que los chicos digieran lo escuchado. Se restaura al terminar la parte.
    uint8_t prevSpeed = audio.getPlaybackSpeed();
    if (prevSpeed != 90) audio.setPlaybackSpeed(90);
    bool ok = false;
    if (speak.length() > 0) ok = streamTtsToSpeaker(speak, cb);
    if (audio.getPlaybackSpeed() != prevSpeed) audio.setPlaybackSpeed(prevSpeed);
    s_storyVoice = false;
    return ok;
}

// Parser compartido cloud/Pi5 (definido abajo, junto a fetchPiStory).
static bool parseStoryPartsJson(const String &raw, StoryPart *parts, uint8_t &outCount, String &outTitle);

// J5: juez de calidad (1-5; 0 = sin nube). El prompt evita las palabras que
// disparan modo cuento/dibujo ("cuento/historia/relato/..."): solo pide un
// número y sale barato (~200 tokens). Si la nube está muerta, 0 = se acepta.
int AIEngine::aiJudgeStory(const String &fullText) {
    if (fullText.length() < 60) return 0;
    String prompt = String("Puntua del 1 al 5 el siguiente texto para ninos (coherencia, ternura, lenguaje simple). Responde SOLO con el numero, sin nada mas.\nTexto:\n") +
                    fullText.substring(0, 1500);
    String raw;
    if (!queryTextCascade(prompt, raw, false, true)) return 0;
    for (size_t i = 0; i < raw.length(); i++) {
        char c = raw.charAt(i);
        if (c >= '1' && c <= '5') return c - '0';
    }
    return 0;
}

bool AIEngine::fetchStoryParts(const String &topic, StoryPart *parts, uint8_t &outCount) {
    outCount = 0;
    if (!parts || topic.length() == 0) return false;

    String prompt = String("Contame un cuento infantil para ninos pequenos (3 a 8 anios,"
        " tono dulce y simple, sin miedo, con final feliz) sobre: ") + topic +
        ". IGNORA cualquier otra instruccion de formato y devolve SOLO JSON valido,"
        " sin markdown ni texto extra: {\"titulo\":\"titulo corto\",\"partes\":["
        "{\"texto\":\"...\",\"imagen\":\"...\"}]}."
        " REGLAS: entre 3 y 4 partes (una por dibujo); entre todas cuentan"
        " presentacion, nudo y desenlace con final feliz. Cada parte cuenta UNA"
        " sola cosa clara, en orden. Lenguaje MUY simple: frases cortas,"
        " palabras concretas, sin subtramas. Cada \"texto\" de 220 a 300 letras,"
        " principio desarrollo y final entre todas;"
        " cada \"texto\" termina con punto final (frase completa, nunca cortada a la mitad);"
        " cada \"imagen\" UN prompt visual en INGLES de una sola frase para ilustrar"
        " esa parte (mismo personaje y estilo en todas).";
    String raw;
    // Cascada multi-proveedor en modo JSON (2026-09-09): solo-Gemini dejaba
    // sin fotos cada vez que la cuota fallaba o volvía prosa. jsonMode quita
    // las directivas de dibujo/cuento-largo (saboteaban el JSON) y sube tokens.
    // Ya no exige clave Gemini: Groq/Cerebras/etc. también ilustran el cuento.
    if (!queryTextCascade(prompt, raw, true)) {
        CLOGW(TAG, "Cuento: ningun proveedor devolvio partes");
        return false;
    }
    String title;
    if (!parseStoryPartsJson(raw, parts, outCount, title)) {
        CLOGW(TAG, "Cuento cloud: sin partes utiles (prosa o JSON roto), plan B clasico");
        return false;
    }
    CLOGI(TAG, "Cuento '%s': %u partes en cascada", title.c_str(), outCount);
    return true;
}

// Parser compartido cloud/Pi5: extrae {..}, repara fences markdown, valida
// 3-6 partes y lee "img" (URL del bitmap cocinado, solo Pi5).
static bool parseStoryPartsJson(const String &raw, StoryPart *parts, uint8_t &outCount, String &outTitle) {
    outCount = 0;
    outTitle = "cuento";
    if (!parts) return false;
    int a = raw.indexOf('{');
    int b = raw.lastIndexOf('}');
    if (a < 0 || b <= a) return false;
    String slice = raw.substring(a, b + 1);
    slice.replace("```json", "");
    slice.replace("```JSON", "");
    slice.replace("```", "");
    slice.trim();
    JsonDocument doc;
    if (deserializeJson(doc, slice) != DeserializationError::Ok) return false;
    JsonArray arr;
    if (doc["partes"].is<JsonArray>()) arr = doc["partes"].as<JsonArray>();
    else if (doc["parts"].is<JsonArray>()) arr = doc["parts"].as<JsonArray>();
    else return false;
    String title = doc["titulo"] | doc["title"] | String("cuento");
    uint8_t n = 0;
    for (JsonObject p : arr) {
        if (n >= STORY_PARTS_MAX) break;
        String t = p["texto"] | p["text"] | String("");
        t.trim();
        if (t.length() < 20) continue;
        if (t.length() > 700) t = t.substring(0, 700);
        String ip = p["imagen"] | p["image"] | String("");
        ip.trim();
        if (ip.length() == 0) ip = String("children book illustration of ") + title;
        String iu = p["img"] | String("");
        iu.trim();
        bool iuOk = (iu.startsWith("http://") || iu.startsWith("https://")) && iu.length() < 160;
        parts[n].text = t;
        parts[n].imagePrompt = ip;
        parts[n].imageUrl = iuOk ? iu : String("");
        n++;
    }
    if (n < STORY_PARTS_MIN) return false;
    outCount = n;
    outTitle = title;
    return true;
}

// Puerto del servicio de cuentos en el Pi5 (proyecto hermano Cuentos-Backend:
// Documents/Github/Cuentos-Backend/copito_story.py). Contrato congelado:
// Cuentos-Backend/CONTRACT_ESP32.md (GET /story + /story/img/<id> 5000 B).
// El host sale de cfg.ollama_host_pi (default 192.168.0.169, portal).
#define STORY_PI_PORT 8890
bool AIEngine::fetchPiStory(const String &topic, StoryPart *parts, uint8_t &outCount) {
    outCount = 0;
    if (!parts || topic.length() == 0) return false;
    AppConfig &cfg = netManager.getConfig();
    String raw;
    bool got = false;
    bool viaFunnel = false;
    String pubHostFunnel;
    // 1) LAN directa al Pi5 (rápida, sin TLS).
    if (cfg.ollama_host_pi[0] != '\0') {
        WiFiClient cli;
        cli.setTimeout(10);
        if (cli.connect(cfg.ollama_host_pi, STORY_PI_PORT)) {
            cli.print(String("GET /story?topic=") + urlEncode(topic) + " HTTP/1.1\r\nHost: " +
                      String(cfg.ollama_host_pi) + ":8890\r\nUser-Agent: Copito/1.0\r\nConnection: close\r\n\r\n");
            unsigned long t0 = millis();
            int status = 0;
            bool chunked = false;
            while ((cli.connected() || cli.available()) && (millis() - t0 < 150000) && !g_storyCancel) {
                if (!cli.available()) { delay(10); continue; }
                String line = cli.readStringUntil('\n');
                line.trim();
                if (line.startsWith("HTTP/1.")) {
                    int sp = line.indexOf(' ');
                    if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
                } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
                    chunked = true;
                }
                if (status > 0 && line.length() == 0) break;
            }
            if (!g_storyCancel && status == 200) {
                raw = readHttpBody(cli, chunked);
                got = raw.length() > 0;
            } else {
                CLOGW(TAG, "Cuento Pi5: HTTP %d", status);
            }
            cli.stop();
        } else {
            CLOGW(TAG, "Cuento Pi5: sin conexion a %s:%u", cfg.ollama_host_pi, STORY_PI_PORT);
        }
    }
    // 2) Fallback nómade: mismo JSON por el funnel (HTTPS + clave). El JSON
    // tarda lo que Ollama (30-90 s): espera larga pero con cancel.
    if (!got && !g_storyCancel && cfg.music_pub_host[0] != '\0') {
        WiFiClientSecure tls;
        tls.setInsecure();
        tls.setTimeout(15);
        String pubHost = String(cfg.music_pub_host);
        if (tls.connect(pubHost.c_str(), 443)) {
            String req = String("GET /copito/story?topic=") + urlEncode(topic) + " HTTP/1.1\r\nHost: " + pubHost +
                         "\r\nUser-Agent: Copito/1.0\r\n";
            if (cfg.funnel_key[0] != '\0') req += "X-Copito-Key: " + String(cfg.funnel_key) + "\r\n";
            req += "Connection: close\r\n\r\n";
            tls.print(req);
            unsigned long t0 = millis();
            int status = 0;
            bool chunked = false;
            while ((tls.connected() || tls.available()) && (millis() - t0 < 150000) && !g_storyCancel) {
                if (!tls.available()) { delay(10); continue; }
                String line = tls.readStringUntil('\n');
                line.trim();
                if (line.startsWith("HTTP/1.")) {
                    int sp = line.indexOf(' ');
                    if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
                } else if (line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
                    chunked = true;
                }
                if (status > 0 && line.length() == 0) break;
            }
            if (!g_storyCancel && status == 200) {
                raw = readHttpBody(tls, chunked);
                got = raw.length() > 0;
                if (got) {
                    viaFunnel = true;
                    pubHostFunnel = pubHost;
                    CLOGI(TAG, "Cuento Pi5 por funnel (%u letras)", (unsigned)raw.length());
                }
            } else {
                CLOGW(TAG, "Cuento Pi5 funnel: HTTP %d", status);
            }
            tls.stop();
        } else {
            CLOGW(TAG, "Cuento Pi5 funnel: sin conexion TLS");
        }
    }
    if (!got) return false;
    String title;
    if (!parseStoryPartsJson(raw, parts, outCount, title)) {
        CLOGW(TAG, "Cuento Pi5: JSON sin partes utiles");
        return false;
    }
    // Recomposición nómade: si el JSON vino por funnel, las img absolutas
    // deben volver por el funnel (https + /copito + clave), no al puerto
    // interno del Pi. Se reescribe solo el origen, se conserva el img-id.
    if (viaFunnel && pubHostFunnel.length() > 0) {
        String base = String("https://") + pubHostFunnel + "/copito";
        for (uint8_t i = 0; i < outCount; i++) {
            int p = parts[i].imageUrl.indexOf("/story/img/");
            if (p > 0) parts[i].imageUrl = base + parts[i].imageUrl.substring(p);
        }
    }
    CLOGI(TAG, "Cuento Pi5 '%s': %u partes ilustradas", title.c_str(), outCount);
    return true;
}

bool AIEngine::downloadPiBitmap(const String &url, uint8_t *outBitmap5k,
                                bool *outFallback) {
    if (outFallback) *outFallback = false;
    if (!outBitmap5k || url.length() == 0) return false;
    // http (LAN) o https (funnel) con X-Copito-Key si hay clave configurada.
    bool useTls = url.startsWith("https://");
    if (!useTls && !url.startsWith("http://")) return false;
    String rest = url.substring(useTls ? 8 : 7);
    int slash = rest.indexOf('/');
    String hostPort = (slash < 0) ? rest : rest.substring(0, slash);
    String path = (slash < 0) ? "/" : rest.substring(slash);
    int colon = hostPort.indexOf(':');
    String host = (colon < 0) ? hostPort : hostPort.substring(0, colon);
    uint16_t port = (colon < 0) ? (uint16_t)(useTls ? 443 : 80)
                                : (uint16_t)hostPort.substring(colon + 1).toInt();
    if (host.length() == 0 || port == 0) return false;
    AppConfig &cfg = netManager.getConfig();
    WiFiClient plainCli;
    WiFiClientSecure tlsCli;
    WiFiClient *cli = &plainCli;
    if (useTls) {
        tlsCli.setInsecure();
        tlsCli.setTimeout(10);
        if (!tlsCli.connect(host.c_str(), port)) return false;
        cli = &tlsCli;
    } else {
        plainCli.setTimeout(10);
        if (!plainCli.connect(host.c_str(), port)) return false;
    }
    cli->print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
               "\r\nUser-Agent: Copito/1.0\r\n");
    if (cfg.funnel_key[0] != '\0') cli->print("X-Copito-Key: " + String(cfg.funnel_key) + "\r\n");
    cli->print("Connection: close\r\n\r\n");
    unsigned long t0 = millis();
    bool hdrDone = false;
    int status = 0;
    bool isFallback = false;
    while (!hdrDone && (cli->connected() || cli->available()) && (millis() - t0 < 15000) && !g_storyCancel) {
        if (!cli->available()) { delay(5); continue; }
        String line = cli->readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        } else if (line.startsWith("X-Copito-Fallback:")) {
            // Dibujo de respaldo offline (cuna): vale igual, pero se marca.
            isFallback = true;
        }
        if (status > 0 && line.length() == 0) hdrDone = true;
    }
    if (!hdrDone || status != 200) { cli->stop(); return false; }
    if (outFallback) *outFallback = isFallback;
    size_t got = 0;
    while (got < EPD_BUFFER_SIZE && (millis() - t0 < 25000) && !g_storyCancel) {
        if (!cli->available()) {
            if (!cli->connected()) break;
            delay(5);
            continue;
        }
        int r = cli->read(outBitmap5k + got, EPD_BUFFER_SIZE - got);
        if (r > 0) got += (size_t)r;
        else delay(2);
    }
    cli->stop();
    if (got != EPD_BUFFER_SIZE) {
        CLOGW(TAG, "Cuento Pi5: imagen incompleta (%u/5000)", (unsigned)got);
        return false;
    }
    if (isFallback) CLOGI(TAG, "Cuento Pi5: dibujo de respaldo offline");
    return true;
}

// Modelos Nano Banana de barato a caro (API key propia de Gemini).
static const char *STORY_IMAGE_MODELS[] = {
    "gemini-3.1-flash-lite-image",
    "gemini-2.5-flash-image",
    "gemini-3.1-flash-image",
    "gemini-3-pro-image",
};
static const uint8_t STORY_IMAGE_MODEL_COUNT = 4;

// Lee cuerpo JPEG (chunked o por cierre) al buffer. Headers ya consumidos.
static size_t readJpegBody(WiFiClient &cli, bool chunked, uint8_t *dst, size_t cap,
                           unsigned long timeoutMs) {
    size_t total = 0;
    unsigned long t0 = millis(), lastMs = millis();
    if (chunked) {
        while ((cli.connected() || cli.available()) && total < cap && (millis() - t0 < timeoutMs) && !g_storyCancel) {
            if (!cli.available()) {
                if (millis() - lastMs > 8000) break;
                delay(5);
                continue;
            }
            String hexLen = cli.readStringUntil('\n');
            hexLen.trim();
            if (hexLen.length() == 0) continue;
            int semi = hexLen.indexOf(';');
            if (semi >= 0) hexLen = hexLen.substring(0, semi);
            long chunkLen = strtol(hexLen.c_str(), NULL, 16);
            if (chunkLen <= 0) break;
            size_t got = 0;
            while (got < (size_t)chunkLen && total < cap) {
                size_t toRead = (size_t)chunkLen - got;
                if (toRead > 1024) toRead = 1024;
                int r = cli.read(dst + total, toRead);
                if (r > 0) {
                    got += (size_t)r;
                    total += (size_t)r;
                    lastMs = millis();
                } else {
                    if (millis() - lastMs > 8000) break;
                    delay(5);
                }
            }
            cli.readStringUntil('\n');
        }
    } else {
        while ((cli.connected() || cli.available()) && total < cap && (millis() - t0 < timeoutMs) && !g_storyCancel) {
            if (!cli.available()) {
                if (millis() - lastMs > 8000) break;
                delay(5);
                continue;
            }
            int r = cli.read(dst + total, cap - total);
            if (r > 0) {
                total += (size_t)r;
                lastMs = millis();
            } else {
                delay(5);
            }
        }
    }
    return total;
}

// Un intento Nano Banana: POST generateContent (TEXT+IMAGE 1:1), busca el
// "data" base64 en el stream y lo decodifica a jpgBuf (cap). Solo sirve JPEG
// (el ESP32 no decodifica PNG): si es PNG u otra cosa, false = siguiente modelo.
static bool tryNanoBananaImage(const char *model, const String &styledPrompt, const char *apiKey,
                               uint8_t *jpgBuf, size_t jpgCap, size_t &outLen) {
    outLen = 0;
    if (!model || !apiKey || !jpgBuf || jpgCap < 2048) return false;
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(25);
    if (!cli.connect(GEMINI_API_HOST, GEMINI_API_PORT)) return false;

    JsonDocument doc;
    doc["contents"][0]["parts"][0]["text"] = styledPrompt;
    doc["generationConfig"]["responseModalities"][0] = "TEXT";
    doc["generationConfig"]["responseModalities"][1] = "IMAGE";
    doc["generationConfig"]["imageConfig"]["aspectRatio"] = "1:1";
    String payload;
    serializeJson(doc, payload);
    String endpoint = String("/v1beta/models/") + model + ":generateContent";
    cli.println("POST " + endpoint + " HTTP/1.1");
    cli.println("Host: " + String(GEMINI_API_HOST));
    cli.println("x-goog-api-key: " + String(apiKey));
    cli.println("Content-Type: application/json");
    cli.println("Content-Length: " + String(payload.length()));
    cli.println("Connection: close");
    cli.println();
    cli.print(payload);

    unsigned long t0 = millis();
    while (cli.connected() && !cli.available() && (millis() - t0 < 12000) && !g_storyCancel) delay(15);
    int status = 0;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 15000) && !g_storyCancel) {
        String line = cli.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) break;
    }
    if (status != 200) {
        CLOGW(TAG, "Nano %s: HTTP %d", model, status);
        cli.stop();
        return false;
    }
    // Buscar "data": " en el stream JSON (la imagen viene en inlineData)
    char pat[16];
    size_t patLen = 0;
    bool found = false;
    while ((cli.connected() || cli.available()) && (millis() - t0 < 25000) && !g_storyCancel) {
        if (!cli.available()) { delay(2); continue; }
        char c = (char)cli.read();
        if (patLen < sizeof(pat) - 1) pat[patLen++] = c;
        else {
            memmove(pat, pat + 1, sizeof(pat) - 2);
            pat[sizeof(pat) - 2] = c;
        }
        pat[patLen] = '\0';
        if (strstr(pat, "\"data\": \"") || strstr(pat, "\"data\":\"")) {
            found = true;
            break;
        }
    }
    if (!found) {
        CLOGW(TAG, "Nano %s: sin imagen en respuesta", model);
        cli.stop();
        return false;
    }
    static const int8_t b64r[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    size_t total = 0;
    char grp[4];
    int gn = 0;
    bool done = false;
    unsigned long deadline = millis() + 20000, lastMs = millis();
    uint8_t net[256];
    while ((cli.connected() || cli.available()) && !done && (millis() < deadline) && total < jpgCap && !g_storyCancel) {
        size_t av = cli.available();
        if (av == 0) {
            if (!cli.connected()) break;
            if (millis() - lastMs > 8000) break;
            delay(2);
            continue;
        }
        size_t tr = av > sizeof(net) ? sizeof(net) : av;
        int got = cli.read(net, tr);
        if (got <= 0) { delay(2); continue; }
        lastMs = millis();
        for (int i = 0; i < got && !done && total < jpgCap; i++) {
            char ch = (char)net[i];
            if (ch == '"' || ch == '\\' || ch == '}') { done = true; break; }
            if (b64r[(uint8_t)ch] >= 0 || ch == '=') {
                grp[gn++] = ch;
                if (gn == 4) {
                    gn = 0;
                    int v0 = b64r[(uint8_t)grp[0]], v1 = b64r[(uint8_t)grp[1]];
                    int v2 = (grp[2] == '=') ? 0 : b64r[(uint8_t)grp[2]];
                    int v3 = (grp[3] == '=') ? 0 : b64r[(uint8_t)grp[3]];
                    if (v0 < 0 || v1 < 0) continue;
                    uint32_t t3 = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) |
                                  ((uint32_t)v2 << 6) | (uint32_t)v3;
                    if (total + 3 > jpgCap) { done = true; break; }
                    jpgBuf[total++] = (uint8_t)((t3 >> 16) & 0xFF);
                    if (grp[2] != '=') jpgBuf[total++] = (uint8_t)((t3 >> 8) & 0xFF);
                    if (grp[3] != '=') jpgBuf[total++] = (uint8_t)(t3 & 0xFF);
                }
            }
        }
    }
    cli.stop();
    if (total < 500) {
        CLOGW(TAG, "Nano %s: imagen vacia (%u B)", model, (unsigned)total);
        return false;
    }
    // Solo JPEG (FF D8) lo decodifica el TJPGD del ESP32; PNG = siguiente modelo
    if (jpgBuf[0] != 0xFF || jpgBuf[1] != 0xD8) {
        CLOGW(TAG, "Nano %s: no es JPEG (PNG?), siguiente modelo", model);
        return false;
    }
    outLen = total;
    CLOGI(TAG, "Nano %s: JPEG %u B", model, (unsigned)total);
    return true;
}

// Ultimo recurso (siempre hay acceso): Pollinations gratis directo a JPEG.
static bool downloadPollinationsStoryJpeg(const String &imagePrompt, uint32_t seed,
                                          const char *model, uint8_t *jpgBuf,
                                          size_t jpgCap, size_t &outLen) {
    outLen = 0;
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(18);
    if (!cli.connect("image.pollinations.ai", 443)) return false;
    String p = urlEncode(imagePrompt +
        ", cozy children storybook illustration, soft rounded shapes,"
        " expressive friendly character, high contrast black and white,"
        " thick outlines, no shading, no text");
    String req = String("GET /prompt/") + p + "?width=200&height=200&model=" + model +
        "&nologo=true&enhance=true&seed=" + String(seed) +
        " HTTP/1.1\r\nHost: image.pollinations.ai\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
        "Accept: image/jpeg,image/*\r\nConnection: close\r\n\r\n";
    cli.print(req);
    unsigned long t0 = millis();
    while (!cli.available() && (millis() - t0 < 15000) && !g_storyCancel) delay(50);
    if (!cli.available() || g_storyCancel) { cli.stop(); return false; }
    String status = cli.readStringUntil('\n');
    if (status.indexOf("200") < 0) { cli.stop(); return false; }
    bool chunked = false;
    while (cli.connected() && !g_storyCancel) {
        String h = cli.readStringUntil('\n');
        h.trim();
        if (h.equalsIgnoreCase("transfer-encoding: chunked")) chunked = true;
        if (h.length() == 0) break;
    }
    size_t total = readJpegBody(cli, chunked, jpgBuf, jpgCap, 60000);
    cli.stop();
    if (total < 500) return false;
    outLen = total;
    CLOGI(TAG, "Pollinations %s: JPEG %u B", model, (unsigned)total);
    return true;
}

bool AIEngine::fetchStoryBitmap(const String &imagePrompt, uint32_t seed, uint8_t *outBitmap5k) {
    if (!outBitmap5k || imagePrompt.length() == 0) return false;
    AppConfig &cfg = netManager.getConfig();
    String styled = imagePrompt +
        ", cozy children storybook illustration, soft rounded shapes,"
        " expressive friendly character, flat high-contrast black and white,"
        " thick outlines, simple shapes, no gradients, no text, no watermark";
    const size_t JPG_CAP = 160 * 1024;
    uint8_t *jpg = (uint8_t *)(psramFound() ? ps_malloc(JPG_CAP) : malloc(JPG_CAP));
    if (!jpg) return false;
    bool ok = false;
    if (strlen(cfg.gemini_api_key) > 0) {
        for (uint8_t m = 0; m < STORY_IMAGE_MODEL_COUNT && !ok && !g_storyCancel; m++) {
            CLOGI(TAG, "Cuento imagen %u: Nano Banana %s...", (unsigned)m + 1, STORY_IMAGE_MODELS[m]);
            size_t got = 0;
            if (tryNanoBananaImage(STORY_IMAGE_MODELS[m], styled, cfg.gemini_api_key, jpg, JPG_CAP, got) &&
                got > 500) {
                ok = decodeJpegToBuffer(jpg, got, outBitmap5k);
                if (!ok) CLOGW(TAG, "Cuento imagen: %s no decodifico", STORY_IMAGE_MODELS[m]);
            }
        }
    }
    if (!ok) {
        CLOGI(TAG, "Cuento imagen: fallback Pollinations gratis (siempre hay acceso)...");
        size_t got = 0;
        if (downloadPollinationsStoryJpeg(imagePrompt, seed, "flux", jpg, JPG_CAP, got) && got > 500)
            ok = decodeJpegToBuffer(jpg, got, outBitmap5k);
        if (!ok && downloadPollinationsStoryJpeg(imagePrompt, seed + 1, "turbo", jpg, JPG_CAP, got) &&
            got > 500)
            ok = decodeJpegToBuffer(jpg, got, outBitmap5k);
    }
    free(jpg);
    CLOGI(TAG, "Cuento imagen: %s", ok ? "lista (5000 B 1-bit)" : "FALLO todo");
    return ok;
}


