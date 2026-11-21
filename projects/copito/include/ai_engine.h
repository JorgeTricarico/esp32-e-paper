#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "audio_es8311.h"

// Endpoint de chat estilo OpenAI (Groq, Cerebras, Pollinations, Ollama, OpenRouter).
// Un solo código POST/parse sirve a todos: solo cambian host/path/key/modelo.
struct ChatEndpoint {
    const char *label;
    const char *host;
    uint16_t port;
    bool tls;
    const char *path;
    const char *apiKey;  // "" = sin auth (Pollinations, Ollama LAN)
    const char *model;
    uint32_t timeoutMs;
};

// Cuento en cascada: cada parte trae texto para narrar + prompt visual en
// ingles para su ilustracion. La IA elige 3..6 partes segun el tema.
// imageUrl: bitmap 1-bit 5000 B ya cocinado por el Pi5 (plan B sin cuota);
// vacío = ilustrar con el pipeline cloud (Nano Banana -> Pollinations).
struct StoryPart {
    String text;
    String imagePrompt;
    String imageUrl;
};
static const uint8_t STORY_PARTS_MIN = 3;
static const uint8_t STORY_PARTS_MAX = 6;

class AIEngine {
public:
    AIEngine();
    ~AIEngine();

    // Direct Reasoning with Google Gemini (Audio WAV -> Text Answer).
    // preTranscript: transcript ya obtenido (ej. main lo transcribio para ruteo
    // local); si viene vacio se transcribe adentro como antes.
    bool queryGeminiAudio(const uint8_t *wavData, size_t wavSize, String &outResponseText,
                          const String &preTranscript = "");

    // Direct Reasoning with Google Gemini (Text -> Text Answer).
    // jsonMode: no agrega directivas de cuento/dibujo (sabotean el JSON) y
    // pide tokens de sobra para 3-6 partes. noDirectives: tampoco agrega
    // directivas pero mantiene tokens/sistema/aceptación (lo usa el juez).
    bool queryGeminiText(const String &promptText, String &outResponseText,
                         bool jsonMode = false, bool noDirectives = false);

    // Cascada multi-proveedor (texto): Gemini -> Groq -> Cerebras ->
    // Pollinations -> Ollama PC -> Ollama Pi. Cada 429/5xx salta al siguiente.
    // jsonMode: idem (lo usa el cuento en cascada para el JSON de partes).
    bool queryTextCascade(const String &promptText, String &outResponseText,
                          bool jsonMode = false, bool noDirectives = false);

    // Busqueda web propia: la IA emite [TOOL:WEB_SEARCH:consulta] cuando necesita    // datos actuales. Aqui se busca (SearXNG LAN primario -> DuckDuckGo fallback),
    // se inyectan los resultados y se relanza la pregunta para la respuesta final.
    // Devuelve true si el tag fue detectado (aunque la 2da ronda falle, deja texto limpio).
    bool resolveWebSearchRound(String &ioText, const String &originalUserPrompt);

    // STT via Groq Whisper (multipart WAV) para la voz cuando Gemini falla
    bool transcribeGroqWhisper(const uint8_t *wavData, size_t wavSize, String &outTranscript);

    // Comandos de voz SIN IA: musica en el Pi3 (PiMusic API: search/play/control)
    // o EN el ESP32 (stream PCM). Detecta "sona X / suena X" (-> suena en Kraken),
    // "musica X / pone X / para la musica / pausa / que suena" (Pi3) en el
    // transcript y lo ejecuta directo contra la LAN. Devuelve true si lo manejo
    // (no hace falta llamar a la IA); outEsp32Query trae la consulta cuando hay
    // que stremear en el ESP32 via streamMusicOnEsp32 (vacia = Pi3 u otro caso).
    bool tryLocalMusicCommand(const String &transcript, String &outSpoken, String &outScreen,
                             String &outEsp32Query);
    // Reproduce en el parlante del ESP32: Pi3 transcodifica YouTube -> PCM
    // s16le 22050Hz mono (/api/copito_stream) y aca se manda al I2S.
    // Stop manteniendo BOOT apretado. Devuelve true si sono algo.
    bool streamMusicOnEsp32(const String &query, String &outTitle);
    // Stream PCM de URL directa (radios) via Pi3 (/api/copito_stream?url=).
    bool streamPcmUrlOnEsp32(const String &url, const String &title);
    // Comandos de sistema SIN IA (volumen, radio, hora, repeticion, temporizador).
    // Todo local en el ESP32 (habla/pantalla propias, temporizador con tarea).
    // outStreamUrl/outStreamTitle: si no vacios, stremear esa URL via
    // streamPcmUrlOnEsp32 (radios). Devuelve true si lo manejo (sin IA).
    bool tryLocalSystemCommand(const String &transcript, String &outSpoken, String &outScreen,
                              String &outStreamUrl, String &outStreamTitle);
    // Eco ("dime X" / "decime X"): extrae X para repetirlo. true = es eco.
    // Lo usan el bloque DECIME (portal: habla en el acto) y main.cpp por voz
    // (staged: lo guarda y BOOT lo dice, nunca la hora ni la IA).
    bool extractEchoPhrase(const String &transcript, String &outPhrase);
    // Temporizador local con cuenta regresiva en e-paper (apto hotspot: 100% local).
    void startLocalTimer(uint32_t seconds, const String &label);
    void cancelLocalTimer();
    bool localTimerActive();
    String localTimerRemaining();

    // Direct Text-to-Speech Streaming (Text -> I2S Speaker + Lip Sync)
    bool streamTtsToSpeaker(const String &text, AudioStreamCallback lipSyncCb = nullptr);
    // Generacion directa de dibujo libre desde la nube (Pollinations / Gateway LAN)
    bool fetchAndDisplayCloudImage(const String &prompt);
    // Referencia REAL (Wikipedia) para logos/escudos/simbolos/banderas/fotos:
    // la IA generativa los INVENTA; esto baja la imagen posta + resumen.
    bool isReferenceImageRequest(const String &transcript, String &outSubject);
    bool fetchWikipediaReference(const String &subject, String &outSummary, bool &outImageShown);
    // Tapa del disco (iTunes) / logo de radio (Wikipedia) en 64x64 1-bit.
    // Solo si coincide con lo pedido; si no, false y queda el titulo solo.
    bool fetchMusicLogo(const String &query, const String &title, uint8_t *logoOut512);
    bool fetchStationLogo(const String &station, uint8_t *logoOut512);
    // Cuento en cascada: Gemini devuelve JSON con 3-6 partes (texto + imagen).
    // false = plan B (cuento largo clasico sin ilustrar).
    bool fetchStoryParts(const String &topic, StoryPart *parts, uint8_t &outCount);
    // J5: juez de calidad del cuento Pi5 (1-5; 0 = sin nube para juzgar).
    // Usa la cascada de texto (barata): si la nube está muerta, no juzga.
    int aiJudgeStory(const String &fullText);
    // Cuento desde el Pi5 local (plan B sin cuota): mismo JSON + "img" con URL
    // del bitmap 1-bit ya ditherado. false = seguir al clásico.
    bool fetchPiStory(const String &topic, StoryPart *parts, uint8_t &outCount);
    // Baja un bitmap 1-bit 5000 B exactos del Pi5 (URL absoluta http://...).
    // Si el back sirvió el dibujo de respaldo offline (header
    // X-Copito-Fallback, ver Cuentos-Backend), outFallback queda en true.
    bool downloadPiBitmap(const String &url, uint8_t *outBitmap5k,
                          bool *outFallback = nullptr);
    // Ilustra una parte a bitmap 1-bit 200x200 (5000 B): cascada Nano Banana
    // (lite -> 2.5 -> 3.1 -> pro) y Pollinations gratis como ultimo recurso.
    bool fetchStoryBitmap(const String &imagePrompt, uint32_t seed, uint8_t *outBitmap5k);
    // Decodifica JPEG a bitmap 1-bit 200x200 (slots de precarga, sin mostrar).
    bool decodeJpegToBitmap(const uint8_t *jpgData, size_t jpgLen, uint8_t *outBitmap);
    // Narra una parte del cuento con voz de narrador (Gemini Sulafat lento);
    // si falla, cadena normal de TTS. Respeta g_ttsStopRequested (BOOT = callar).
    bool narrateStoryPart(const String &text, AudioStreamCallback cb = nullptr);
    // La voz ganadora del bloque 1 abre los siguientes (pegajosa). Se resetea
    // al empezar cada cuento (las condiciones cambian entre cuentos).
    void storyTtsResetPref();

private:
    bool m_inWebSearchRound = false; // suprime la instruccion [TOOL:WEB_SEARCH] en la 2da ronda

    // Helpers
    void streamBase64(WiFiClientSecure &cli, const uint8_t *data, size_t len);
    String buildBasePrompt(); // system + memoria + instruccion busqueda web
    bool chatOpenAI(const ChatEndpoint &ep, const String &systemPrompt,
                    const String &userText, String &outReply);
    bool performWebSearch(const String &query, String &outContext);      // SearXNG -> DDG
    bool searchSearxng(const String &query, String &outResults);         // LAN, JSON
    bool searchDuckDuckGo(const String &query, String &outResults);      // TLS, HTML
    // Cliente PiMusic API (Pi3, HTTP plano en LAN)
    bool piMusicSearch(const String &query, String &outTitle, String &outUrl); // GET /api/search
    bool piMusicPlay(const String &url, const String &title, String &outPlayedTitle); // POST /api/play
    bool piMusicControl(const char *action);                             // POST /api/control
    bool piMusicStatus(String &outState, String &outTrack);              // GET /api/status
    bool streamAudioData(WiFiClient &cli, int sampleRate, AudioStreamCallback cb, bool isChunked,
                         const uint8_t *prelude = nullptr, size_t preludeLen = 0);
    bool streamLocalTts(const char *host, uint16_t port, const String &voice, const String &text, AudioStreamCallback cb, bool slow = false);
    // Piper del Pi5 por el funnel (llavero nómade): HTTPS + X-Copito-Key.
    bool streamFunnelTts(const String &voice, const String &text, AudioStreamCallback cb, bool slow = false);
    bool streamGeminiTts(const String &text, AudioStreamCallback cb, const char *voice = "Aoede", const char *stylePrefix = "", int gain256 = 256);
    bool streamGoogleFreeTts(const String &text, AudioStreamCallback cb);
    bool streamVoiceRss(const String &text, AudioStreamCallback cb);
    bool streamElevenLabs(const String &text, AudioStreamCallback cb);
    bool streamOpenAiTts(const String &text, AudioStreamCallback cb);
    bool streamTtsSingle(const String &cleanText, AudioStreamCallback cb);
};

extern AIEngine aiEngine;
extern String g_lastVoiceTranscript;
extern String g_lastSpoken;              // ultimo texto hablado (para "repeti")
extern volatile bool g_musicStopRequested; // "para" frena stream ESP32 + Pi3
extern String g_musicEsp32Title;         // titulo sonando EN Kraken ("" = nada)
extern volatile bool g_musicPlaying;     // true mientras suena musica/radio
extern volatile bool g_speaking;         // true mientras el TTS habla
extern volatile bool g_ttsStopRequested; // BOOT mientras habla = callar
// Mantiene despierto (resetea el timeout de sleep). Definido en main.cpp.
void copitoPokeActivity();
// true si el usuario canceló con POWER (definido en main.cpp): el TTS no debe
// arrancar (sin limpiar g_ttsStopRequested, que ya frena los loops en curso).
bool copitoVoiceCancelled();
// Estadísticas del pipeline de voz (para /api/selftest y diagnóstico).
// ok/fail: frases TTS que sonaron vs fallaron; lastErr: último motivo corto.
void aiVoiceStats(uint32_t &ok, uint32_t &fail, char *lastErr, size_t lastErrCap);
// Pedido explícito de cuento/historia/relato (normaliza tildes/mayúsculas).
// Lo usa main.cpp para forzar modo CUENTO (el cuento nunca dibuja).
bool aiIsStoryRequest(const String &text);

#endif // AI_ENGINE_H
