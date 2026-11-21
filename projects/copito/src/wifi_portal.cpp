#include "wifi_portal.h"
#include "audio_es8311.h"
#include "dsp_audio.h"
#include "ai_engine.h"
#include "epaper_driver.h"
#include "sensor_shtc3.h"
#include "copito_tools.h"
#include "copito_memory.h"
#include "text_paginator.h"
#include "copito_log.h"
#include "voice_validate.h"
#include "battery_monitor.h"
#include "story_cascade.h"
#include <Update.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

extern TextPaginator paginator;
extern void changeState(AssistantState newState);
extern void powerBoost(); // plan leve: radio full (main.cpp)
extern void powerRelax(); // plan leve: modem-sleep si nadie lo impide (main.cpp)
extern bool copitoVoiceBusy(); // true si voz/música en curso (main.cpp)
extern bool g_textHold;
extern uint32_t g_bootCount;
extern const char *g_resetReason;

// Guardia anti-solape de audio (definida abajo): 409 si hay audio en curso.
static bool speechBusy();
static bool musicBusy();

// === Validación pre-flash (2026-09-09: flashear por /update con música
// sonando brickió el equipo: ping vivo, HTTP/serie/botones muertos).
// El push se rechaza con 409 y el pull-OTA se difiere; nada escribe flash
// con el I2S/poste de audio en uso ni con batería baja sin cargador.
static bool s_otaBlocked = false;
static String s_otaBlockReason = "";

static String otaBusyReason() {
    if (g_musicPlaying) return "musica sonando";
    if (copitoVoiceBusy()) return "voz en curso";
    if (storyCascadeActive()) return "cuento en curso";
    if (speechBusy()) return "habla API en curso";
    if (musicBusy()) return "musica API en curso";
    BatteryStatus st = BatteryMonitor::getStatus();
    if (st.percentage <= 15 && !st.isCharging) return "bateria baja sin cargador";
    return "";
}

static const char *TAG = "NET";

static String htmlEscape(const char *src) {
    if (!src) return String("");
    String out;
    out.reserve(strlen(src) + 64);
    while (*src) {
        char c = *src++;
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

NetworkManager netManager;

NetworkManager::NetworkManager() : server(nullptr), portalActive(false) {
    memset(&config, 0, sizeof(AppConfig));
}

NetworkManager::~NetworkManager() {
    if (server) {
        delete server;
        server = nullptr;
    }
}

void NetworkManager::loadConfig() {
    prefs.begin("assistant", false);

    String s1 = prefs.getString("ssid1", "");
    if (s1.length() == 0) s1 = DEFAULT_WIFI_SSID1;
    String p1 = prefs.getString("pass1", "");
    if (p1.length() == 0) p1 = DEFAULT_WIFI_PASS1;

    String s2 = prefs.getString("ssid2", "");
    if (s2.length() == 0) s2 = DEFAULT_WIFI_SSID2;
    String p2 = prefs.getString("pass2", "");
    if (p2.length() == 0) p2 = DEFAULT_WIFI_PASS2;

    String gKey = prefs.getString("gemini_key", "");
    if (gKey.length() == 0) gKey = DEFAULT_GEMINI_KEY;

    String gModel = prefs.getString("gemini_model", "");
    // Migración 2026-09: el equilibrado es 3.1-flash-lite. Modelos
    // viejos/inexistentes (1.x, 2.0, 3.1-flash-lite, 3.5-flash-lite) migran al default actual.
    if (gModel.length() == 0 || gModel.startsWith("gemini-1.") || gModel.startsWith("gemini-2.0")
        || gModel == "gemini-3.5-flash-lite" || gModel == "gemini-3.1-flash-lite") gModel = GEMINI_DEFAULT_MODEL;
    String tKey = prefs.getString("tts_key", "");
    int ttsProv = prefs.getInt("tts_prov", DEFAULT_TTS_PROVIDER);
    if (ttsProv > TTS_OPENAI) {
        ttsProv = DEFAULT_TTS_PROVIDER;
    }
    String vId = prefs.getString("voice_id", ELEVENLABS_DEFAULT_VOICE);
    uint8_t vol = prefs.getUChar("volume", 100);
    vol = constrain(vol, 0, 100); // Full range now allowed
    String prompt = prefs.getString("prompt", DEFAULT_SYSTEM_PROMPT);
    bool lfsOk = LittleFS.begin(false);
    if (!lfsOk) {
        log_w("LittleFS partition not mounted. Formatting LittleFS for 4KB prompts...");
        LittleFS.format();
        lfsOk = LittleFS.begin(true);
    }
    if (lfsOk && LittleFS.exists("/prompt.txt")) {
        File f = LittleFS.open("/prompt.txt", "r");
        if (f) {
            String lfsPrompt = f.readString();
            f.close();
            if (lfsPrompt.length() > 0) {
                prompt = lfsPrompt;
            }
        }
    }
    if (prompt.indexOf("niño") >= 0 || prompt.indexOf("nino") >= 0 || prompt.indexOf("80 letras") >= 0 || 
        prompt.indexOf("Garrahan") >= 0 || prompt.indexOf("garrahan") >= 0 || 
        prompt.indexOf("[DRAW:") < 0 || prompt.indexOf("sabio de Jorge. Eres Copito") >= 0 ||
        prompt.indexOf("Eres Copito,") >= 0 || // migracion Copito -> Kraken (una vez)
        prompt.indexOf("[SKETCH:") < 0 || prompt.indexOf("Screen Drawing") < 0 || prompt.indexOf("SKETCH_V3") < 0) {
        log_i("Prompt obsoleto o sin SKETCH_V3 detectado. Restaurando DEFAULT_SYSTEM_PROMPT...");
        prompt = DEFAULT_SYSTEM_PROMPT;
        prefs.putString("prompt", DEFAULT_SYSTEM_PROMPT);
        if (lfsOk) {
            File f = LittleFS.open("/prompt.txt", "w");
            if (f) {
                f.print(DEFAULT_SYSTEM_PROMPT);
                f.close();
            }
        }
    }

    strncpy(config.wifi_ssid1, s1.c_str(), sizeof(config.wifi_ssid1) - 1);
    strncpy(config.wifi_pass1, p1.c_str(), sizeof(config.wifi_pass1) - 1);
    strncpy(config.wifi_ssid2, s2.c_str(), sizeof(config.wifi_ssid2) - 1);
    strncpy(config.wifi_pass2, p2.c_str(), sizeof(config.wifi_pass2) - 1);
    strncpy(config.gemini_api_key, gKey.c_str(), sizeof(config.gemini_api_key) - 1);
    strncpy(config.gemini_model, gModel.c_str(), sizeof(config.gemini_model) - 1);
    strncpy(config.tts_api_key, tKey.c_str(), sizeof(config.tts_api_key) - 1);
    config.tts_provider = ttsProv;
    strncpy(config.elevenlabs_voice, vId.c_str(), sizeof(config.elevenlabs_voice) - 1);
    config.volume = vol;
    strncpy(config.system_prompt, prompt.c_str(), sizeof(config.system_prompt) - 1);

    // === v2 extended fields (new keys, safe defaults if not found in NVS) ===
    config.max_rec_seconds   = (uint16_t)prefs.getUShort("max_rec_s",   DEFAULT_MAX_REC_SECONDS);
    config.silence_start_ms  = (uint16_t)prefs.getUShort("sil_start",   DEFAULT_SILENCE_START_MS);
    config.silence_end_ms    = (uint16_t)prefs.getUShort("sil_end",     DEFAULT_SILENCE_END_MS);
    config.sleep_timeout_min = (uint16_t)prefs.getUShort("sleep_min",   DEFAULT_SLEEP_TIMEOUT_MIN);
    config.max_tokens        = (uint16_t)prefs.getUShort("max_tokens",  DEFAULT_MAX_TOKENS);
    config.temperature10     = (uint8_t) prefs.getUChar( "temp10",      DEFAULT_TEMPERATURE10);
    String ttsLang           = prefs.getString("tts_lang", DEFAULT_TTS_LANGUAGE);
    strncpy(config.tts_language, ttsLang.c_str(), sizeof(config.tts_language) - 1);
    config.tts_speed         = (uint8_t) prefs.getUChar( "tts_speed",   DEFAULT_TTS_SPEED);
    config.wake_on_voice     = 0; // reserved
    config.face_pack         = (uint8_t) prefs.getUChar( "face_pack",   DEFAULT_FACE_PACK);
    config.checkin_min       = (uint16_t)prefs.getUShort("chk_min",   DEFAULT_CHECKIN_MIN);
    config.checkin_window_s  = (uint16_t)prefs.getUShort("chk_win",   DEFAULT_CHECKIN_WINDOW_S);
    config.ota_hold_min      = (uint16_t)prefs.getUShort("ota_hold",  DEFAULT_OTA_HOLD_MIN);
    config.web_search        = (uint8_t) prefs.getUChar( "web_search",  DEFAULT_WEB_SEARCH);
    String sxh = prefs.getString("sx_h", DEFAULT_SEARXNG_HOST);
    strncpy(config.searxng_host, sxh.c_str(), sizeof(config.searxng_host) - 1);
    config.searxng_port      = (uint16_t)prefs.getUShort("sx_p",  DEFAULT_SEARXNG_PORT);
    String muh = prefs.getString("mu_h", DEFAULT_MUSIC_HOST);
    strncpy(config.music_host, muh.c_str(), sizeof(config.music_host) - 1);
    config.music_port        = (uint16_t)prefs.getUShort("mu_p",  DEFAULT_MUSIC_PORT);
    String muph = prefs.getString("mu_ph", DEFAULT_MUSIC_PUB_HOST);
    strncpy(config.music_pub_host, muph.c_str(), sizeof(config.music_pub_host) - 1);
    String fkey = prefs.getString("fk", "");
    strncpy(config.funnel_key, fkey.c_str(), sizeof(config.funnel_key) - 1);
    checkinQuiet             = prefs.getUShort("chk_quiet", 0);
    if (checkinQuiet > 10) checkinQuiet = 10;
    String lh1 = prefs.getString("tts_lh1", DEFAULT_TTS_LOCAL_HOST1);
    strncpy(config.tts_local_host1, lh1.c_str(), sizeof(config.tts_local_host1) - 1);
    config.tts_local_port1 = (uint16_t)prefs.getUShort("tts_lp1", DEFAULT_TTS_LOCAL_PORT1);
    String lh2 = prefs.getString("tts_lh2", DEFAULT_TTS_LOCAL_HOST2);
    strncpy(config.tts_local_host2, lh2.c_str(), sizeof(config.tts_local_host2) - 1);
    config.tts_local_port2 = (uint16_t)prefs.getUShort("tts_lp2", DEFAULT_TTS_LOCAL_PORT2);
    String lv = prefs.getString("tts_lv", DEFAULT_TTS_LOCAL_VOICE);
    strncpy(config.tts_local_voice, lv.c_str(), sizeof(config.tts_local_voice) - 1);
    String agk = prefs.getString("ai_gk", DEFAULT_GROQ_KEY);
    strncpy(config.ai_groq_key, agk.c_str(), sizeof(config.ai_groq_key) - 1);
    String ack = prefs.getString("ai_ck", DEFAULT_CEREBRAS_KEY);
    strncpy(config.ai_cerebras_key, ack.c_str(), sizeof(config.ai_cerebras_key) - 1);
    String apk = prefs.getString("ai_pk", "");
    strncpy(config.ai_pollinations_key, apk.c_str(), sizeof(config.ai_pollinations_key) - 1);
    String sak = prefs.getString("ai_sn", DEFAULT_SAMBANOVA_KEY);
    strncpy(config.ai_sambanova_key, sak.c_str(), sizeof(config.ai_sambanova_key) - 1);
    String mak = prefs.getString("ai_mi", DEFAULT_MISTRAL_KEY);
    strncpy(config.ai_mistral_key, mak.c_str(), sizeof(config.ai_mistral_key) - 1);
    String xak = prefs.getString("ai_xa", DEFAULT_XAI_KEY);
    strncpy(config.ai_xai_key, xak.c_str(), sizeof(config.ai_xai_key) - 1);
    String oh1 = prefs.getString("ol_h1", DEFAULT_OLLAMA_HOST_PC);
    strncpy(config.ollama_host_pc, oh1.c_str(), sizeof(config.ollama_host_pc) - 1);
    config.ollama_port_pc = (uint16_t)prefs.getUShort("ol_p1", DEFAULT_OLLAMA_PORT);
    String om1 = prefs.getString("ol_m1", DEFAULT_OLLAMA_MODEL_PC);
    strncpy(config.ollama_model_pc, om1.c_str(), sizeof(config.ollama_model_pc) - 1);
    String oh2 = prefs.getString("ol_h2", DEFAULT_OLLAMA_HOST_PI);
    strncpy(config.ollama_host_pi, oh2.c_str(), sizeof(config.ollama_host_pi) - 1);
    config.ollama_port_pi = (uint16_t)prefs.getUShort("ol_p2", DEFAULT_OLLAMA_PORT);
    String om2 = prefs.getString("ol_m2", DEFAULT_OLLAMA_MODEL_PI);
    strncpy(config.ollama_model_pi, om2.c_str(), sizeof(config.ollama_model_pi) - 1);
    String eip = prefs.getString("esp_ip", "");
    strncpy(config.esp_static_ip, eip.c_str(), sizeof(config.esp_static_ip) - 1);
    String egw = prefs.getString("esp_gw", DEFAULT_ESP_GATEWAY);
    strncpy(config.esp_gateway, egw.c_str(), sizeof(config.esp_gateway) - 1);
    String esn = prefs.getString("esp_sn", DEFAULT_ESP_SUBNET);
    strncpy(config.esp_subnet, esn.c_str(), sizeof(config.esp_subnet) - 1);

    // Validate ranges
    if (config.max_rec_seconds   < 3  || config.max_rec_seconds   > 30) config.max_rec_seconds   = DEFAULT_MAX_REC_SECONDS;
    if (config.silence_start_ms  < 500 || config.silence_start_ms > 5000) config.silence_start_ms = DEFAULT_SILENCE_START_MS;
    if (config.silence_end_ms    < 400 || config.silence_end_ms   > 4000) config.silence_end_ms   = DEFAULT_SILENCE_END_MS;
    if (config.sleep_timeout_min > 120) config.sleep_timeout_min  = DEFAULT_SLEEP_TIMEOUT_MIN;
    if (config.max_tokens        < 80  || config.max_tokens        > 2048) config.max_tokens       = DEFAULT_MAX_TOKENS;
    if (config.temperature10     > 20)  config.temperature10       = DEFAULT_TEMPERATURE10;
    if (config.tts_speed < 70 || config.tts_speed > 160)                config.tts_speed           = DEFAULT_TTS_SPEED;
    if (config.tts_language[0]  == 0)  strncpy(config.tts_language, DEFAULT_TTS_LANGUAGE, sizeof(config.tts_language) - 1);
    // Migracion automatica: Copito (Clasico) es el default pegajoso. RUDO,
    // KRAKEN o fuera de rango en NVS -> Clasico (un "cara kraken" mal oido no
    // debe sobrevivir al reboot; por voz vale por la sesion, por portal persiste).
    if (config.face_pack == FACE_PACK_RUDO || config.face_pack == FACE_PACK_KRAKEN ||
        config.face_pack > FACE_PACK_KRAKEN) {
        config.face_pack = FACE_PACK_CLASICO;
        prefs.putUChar("face_pack", FACE_PACK_CLASICO);
    }
    if (config.checkin_min != 0 && (config.checkin_min < 5 || config.checkin_min > 720)) config.checkin_min = DEFAULT_CHECKIN_MIN;
    if (config.checkin_window_s < 60 || config.checkin_window_s > 900) config.checkin_window_s = DEFAULT_CHECKIN_WINDOW_S;
    if (config.ota_hold_min > 480)                                    config.ota_hold_min       = DEFAULT_OTA_HOLD_MIN;
    if (config.web_search > 1)                                        config.web_search         = DEFAULT_WEB_SEARCH;
    if (config.searxng_port == 0) config.searxng_port = DEFAULT_SEARXNG_PORT;
    if (config.music_port == 0) config.music_port = DEFAULT_MUSIC_PORT;
    if (config.tts_local_port1 == 0) config.tts_local_port1 = DEFAULT_TTS_LOCAL_PORT1;
    if (config.tts_local_port2 == 0) config.tts_local_port2 = DEFAULT_TTS_LOCAL_PORT2;
    if (config.tts_local_voice[0] == 0) strncpy(config.tts_local_voice, DEFAULT_TTS_LOCAL_VOICE, sizeof(config.tts_local_voice) - 1);
    if (config.ai_groq_key[0] == 0) strncpy(config.ai_groq_key, DEFAULT_GROQ_KEY, sizeof(config.ai_groq_key) - 1);
    if (config.ai_cerebras_key[0] == 0) strncpy(config.ai_cerebras_key, DEFAULT_CEREBRAS_KEY, sizeof(config.ai_cerebras_key) - 1);
    if (config.ollama_port_pc == 0) config.ollama_port_pc = DEFAULT_OLLAMA_PORT;
    if (config.ollama_model_pc[0] == 0) strncpy(config.ollama_model_pc, DEFAULT_OLLAMA_MODEL_PC, sizeof(config.ollama_model_pc) - 1);
    if (config.ollama_host_pi[0] == 0) strncpy(config.ollama_host_pi, DEFAULT_OLLAMA_HOST_PI, sizeof(config.ollama_host_pi) - 1);
    if (config.ollama_port_pi == 0) config.ollama_port_pi = DEFAULT_OLLAMA_PORT;
    if (config.ollama_model_pi[0] == 0) strncpy(config.ollama_model_pi, DEFAULT_OLLAMA_MODEL_PI, sizeof(config.ollama_model_pi) - 1);
    if (config.esp_gateway[0] == 0) strncpy(config.esp_gateway, DEFAULT_ESP_GATEWAY, sizeof(config.esp_gateway) - 1);
    if (config.esp_subnet[0] == 0) strncpy(config.esp_subnet, DEFAULT_ESP_SUBNET, sizeof(config.esp_subnet) - 1);

    FaceRenderer::setFacePack(config.face_pack);

    prefs.end();
    log_i("Configuration loaded (SSID1: %s, SSID2: %s, FacePack: %d)", config.wifi_ssid1, config.wifi_ssid2, config.face_pack);
}

void NetworkManager::saveConfig() {
    prefs.begin("assistant", false);

    prefs.putString("ssid1", config.wifi_ssid1);
    prefs.putString("pass1", config.wifi_pass1);
    prefs.putString("ssid2", config.wifi_ssid2);
    prefs.putString("pass2", config.wifi_pass2);
    prefs.putString("gemini_key", config.gemini_api_key);
    prefs.putString("gemini_model", config.gemini_model);
    prefs.putString("tts_key", config.tts_api_key);
    prefs.putInt("tts_prov", config.tts_provider);
    prefs.putString("voice_id", config.elevenlabs_voice);
    prefs.putUChar("volume", config.volume);
    prefs.putString("prompt", config.system_prompt);

    // Save to LittleFS /prompt.txt (supports full 4KB without NVS page limits)
    if (LittleFS.begin(true)) {
        File f = LittleFS.open("/prompt.txt", "w");
        if (f) {
            f.print(config.system_prompt);
            f.close();
        }
    }

    // v2 extended fields
    prefs.putUShort("max_rec_s",  config.max_rec_seconds);
    prefs.putUShort("sil_start",  config.silence_start_ms);
    prefs.putUShort("sil_end",    config.silence_end_ms);
    prefs.putUShort("sleep_min",  config.sleep_timeout_min);
    prefs.putUShort("max_tokens", config.max_tokens);
    prefs.putUChar( "temp10",     config.temperature10);
    prefs.putString("tts_lang",   config.tts_language);
    prefs.putUChar( "tts_speed",  config.tts_speed);
    prefs.putUChar( "face_pack",  config.face_pack);
    prefs.putUShort("chk_min",    config.checkin_min);
    prefs.putUShort("chk_win",    config.checkin_window_s);
    prefs.putUShort("ota_hold",   config.ota_hold_min);
    prefs.putUShort("chk_quiet",  checkinQuiet);
    prefs.putUChar( "web_search", config.web_search);
    prefs.putString("sx_h",      config.searxng_host);
    prefs.putUShort("sx_p",      config.searxng_port);
    prefs.putString("mu_h",      config.music_host);
    prefs.putUShort("mu_p",      config.music_port);
    prefs.putString("mu_ph",     config.music_pub_host);
    prefs.putString("fk",        config.funnel_key);
    prefs.putString("ai_gk",      config.ai_groq_key);
    prefs.putString("ai_ck",      config.ai_cerebras_key);
    prefs.putString("ai_pk",      config.ai_pollinations_key);
    prefs.putString("ai_sn",      config.ai_sambanova_key);
    prefs.putString("ai_mi",      config.ai_mistral_key);
    prefs.putString("ai_xa",      config.ai_xai_key);
    prefs.putString("ol_h1",      config.ollama_host_pc);
    prefs.putUShort("ol_p1",      config.ollama_port_pc);
    prefs.putString("ol_m1",      config.ollama_model_pc);
    prefs.putString("ol_h2",      config.ollama_host_pi);
    prefs.putUShort("ol_p2",      config.ollama_port_pi);
    prefs.putString("ol_m2",      config.ollama_model_pi);
    prefs.putString("tts_lh1",    config.tts_local_host1);
    prefs.putUShort("tts_lp1",    config.tts_local_port1);
    prefs.putString("tts_lh2",    config.tts_local_host2);
    prefs.putUShort("tts_lp2",    config.tts_local_port2);
    prefs.putString("tts_lv",     config.tts_local_voice);
    prefs.putString("esp_ip",     config.esp_static_ip);
    prefs.putString("esp_gw",     config.esp_gateway);
    prefs.putString("esp_sn",     config.esp_subnet);

    prefs.end();
    log_i("Configuration successfully committed to NVS (FacePack: %d)", config.face_pack);
    writeConfigBackupFile(); // copia automática a LittleFS por si el NVS se corrompe
}

// === Backup/restore de configuración ( robustness ante NVS corrupto ) ===
// GET /api/config_backup -> JSON con TODA la config (incluye API keys: solo
// exponer en tu LAN de confianza y guardarlo fuera del llavero).
// POST /api/config_restore (plain JSON) -> restaura y persiste. Acepta
// {"from":"flash"} para recuperar la copia automática de LittleFS.
// El WiFi/IP fija requieren reboot para reconectar; el resto aplica en vivo.
static void cfgPutStr(JsonDocument &doc, const char *key, const char *val) {
    doc[key] = val ? val : "";
}

static bool cfgGetStr(const JsonObjectConst &o, const char *key, char *dst, size_t cap) {
    const char *s = o[key] | (const char *)nullptr;
    if (!s) return false;
    strncpy(dst, s, cap - 1);
    dst[cap - 1] = '\0';
    return true;
}

void NetworkManager::configToJson(JsonDocument &doc) {
    doc["kraken_backup"] = 1;
    doc["backup_ver"] = 1;
    cfgPutStr(doc, "ssid1", config.wifi_ssid1);
    cfgPutStr(doc, "pass1", config.wifi_pass1);
    cfgPutStr(doc, "ssid2", config.wifi_ssid2);
    cfgPutStr(doc, "pass2", config.wifi_pass2);
    cfgPutStr(doc, "gemini_key", config.gemini_api_key);
    cfgPutStr(doc, "gemini_model", config.gemini_model);
    cfgPutStr(doc, "tts_key", config.tts_api_key);
    doc["tts_prov"] = config.tts_provider;
    cfgPutStr(doc, "voice_id", config.elevenlabs_voice);
    doc["volume"] = config.volume;
    cfgPutStr(doc, "prompt", config.system_prompt);
    doc["max_rec_s"] = config.max_rec_seconds;
    doc["sil_start"] = config.silence_start_ms;
    doc["sil_end"] = config.silence_end_ms;
    doc["sleep_min"] = config.sleep_timeout_min;
    doc["max_tokens"] = config.max_tokens;
    doc["temp10"] = config.temperature10;
    cfgPutStr(doc, "tts_lang", config.tts_language);
    doc["tts_speed"] = config.tts_speed;
    doc["face_pack"] = config.face_pack;
    doc["chk_min"] = config.checkin_min;
    doc["chk_win"] = config.checkin_window_s;
    doc["chk_quiet"] = checkinQuiet;
    doc["web_search"] = config.web_search;
    cfgPutStr(doc, "sx_h", config.searxng_host);
    doc["sx_p"] = config.searxng_port;
    cfgPutStr(doc, "mu_h", config.music_host);
    doc["mu_p"] = config.music_port;
    cfgPutStr(doc, "mu_ph", config.music_pub_host);
    cfgPutStr(doc, "fk", config.funnel_key);
    cfgPutStr(doc, "ai_gk", config.ai_groq_key);
    cfgPutStr(doc, "ai_ck", config.ai_cerebras_key);
    cfgPutStr(doc, "ai_pk", config.ai_pollinations_key);
    cfgPutStr(doc, "ai_sn", config.ai_sambanova_key);
    cfgPutStr(doc, "ai_mi", config.ai_mistral_key);
    cfgPutStr(doc, "ai_xa", config.ai_xai_key);
    cfgPutStr(doc, "ol_h1", config.ollama_host_pc);
    doc["ol_p1"] = config.ollama_port_pc;
    cfgPutStr(doc, "ol_m1", config.ollama_model_pc);
    cfgPutStr(doc, "ol_h2", config.ollama_host_pi);
    doc["ol_p2"] = config.ollama_port_pi;
    cfgPutStr(doc, "ol_m2", config.ollama_model_pi);
    cfgPutStr(doc, "tts_lh1", config.tts_local_host1);
    doc["tts_lp1"] = config.tts_local_port1;
    cfgPutStr(doc, "tts_lh2", config.tts_local_host2);
    doc["tts_lp2"] = config.tts_local_port2;
    cfgPutStr(doc, "tts_lv", config.tts_local_voice);
    cfgPutStr(doc, "esp_ip", config.esp_static_ip);
    cfgPutStr(doc, "esp_gw", config.esp_gateway);
    cfgPutStr(doc, "esp_sn", config.esp_subnet);
}

static int cfgGetInt(const JsonObjectConst &o, const char *key, int cur, int lo, int hi) {
    if (!o.containsKey(key)) return cur;
    int v = o[key] | cur;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int NetworkManager::configFromJson(const JsonObjectConst &o) {
    if ((o["kraken_backup"] | 0) != 1) return -1; // no es un backup válido
    int n = 0;
    if (cfgGetStr(o, "ssid1", config.wifi_ssid1, sizeof(config.wifi_ssid1))) n++;
    if (cfgGetStr(o, "pass1", config.wifi_pass1, sizeof(config.wifi_pass1))) n++;
    if (cfgGetStr(o, "ssid2", config.wifi_ssid2, sizeof(config.wifi_ssid2))) n++;
    if (cfgGetStr(o, "pass2", config.wifi_pass2, sizeof(config.wifi_pass2))) n++;
    if (cfgGetStr(o, "gemini_key", config.gemini_api_key, sizeof(config.gemini_api_key))) n++;
    if (cfgGetStr(o, "gemini_model", config.gemini_model, sizeof(config.gemini_model))) n++;
    if (cfgGetStr(o, "tts_key", config.tts_api_key, sizeof(config.tts_api_key))) n++;
    if (cfgGetStr(o, "voice_id", config.elevenlabs_voice, sizeof(config.elevenlabs_voice))) n++;
    if (cfgGetStr(o, "prompt", config.system_prompt, sizeof(config.system_prompt))) n++;
    if (cfgGetStr(o, "tts_lang", config.tts_language, sizeof(config.tts_language))) n++;
    if (cfgGetStr(o, "sx_h", config.searxng_host, sizeof(config.searxng_host))) n++;
    if (cfgGetStr(o, "mu_h", config.music_host, sizeof(config.music_host))) n++;
    if (cfgGetStr(o, "mu_ph", config.music_pub_host, sizeof(config.music_pub_host))) n++;
    if (cfgGetStr(o, "fk", config.funnel_key, sizeof(config.funnel_key))) n++;
    if (cfgGetStr(o, "ai_gk", config.ai_groq_key, sizeof(config.ai_groq_key))) n++;
    if (cfgGetStr(o, "ai_ck", config.ai_cerebras_key, sizeof(config.ai_cerebras_key))) n++;
    if (cfgGetStr(o, "ai_pk", config.ai_pollinations_key, sizeof(config.ai_pollinations_key))) n++;
    if (cfgGetStr(o, "ai_sn", config.ai_sambanova_key, sizeof(config.ai_sambanova_key))) n++;
    if (cfgGetStr(o, "ai_mi", config.ai_mistral_key, sizeof(config.ai_mistral_key))) n++;
    if (cfgGetStr(o, "ai_xa", config.ai_xai_key, sizeof(config.ai_xai_key))) n++;
    if (cfgGetStr(o, "ol_h1", config.ollama_host_pc, sizeof(config.ollama_host_pc))) n++;
    if (cfgGetStr(o, "ol_m1", config.ollama_model_pc, sizeof(config.ollama_model_pc))) n++;
    if (cfgGetStr(o, "ol_h2", config.ollama_host_pi, sizeof(config.ollama_host_pi))) n++;
    if (cfgGetStr(o, "ol_m2", config.ollama_model_pi, sizeof(config.ollama_model_pi))) n++;
    if (cfgGetStr(o, "tts_lh1", config.tts_local_host1, sizeof(config.tts_local_host1))) n++;
    if (cfgGetStr(o, "tts_lh2", config.tts_local_host2, sizeof(config.tts_local_host2))) n++;
    if (cfgGetStr(o, "tts_lv", config.tts_local_voice, sizeof(config.tts_local_voice))) n++;
    if (cfgGetStr(o, "esp_ip", config.esp_static_ip, sizeof(config.esp_static_ip))) n++;
    if (cfgGetStr(o, "esp_gw", config.esp_gateway, sizeof(config.esp_gateway))) n++;
    if (cfgGetStr(o, "esp_sn", config.esp_subnet, sizeof(config.esp_subnet))) n++;
    config.tts_provider = cfgGetInt(o, "tts_prov", config.tts_provider, 0, 5);
    config.volume = (uint8_t)cfgGetInt(o, "volume", config.volume, 0, 100);
    config.max_rec_seconds = (uint16_t)cfgGetInt(o, "max_rec_s", config.max_rec_seconds, 3, 30);
    config.silence_start_ms = (uint16_t)cfgGetInt(o, "sil_start", config.silence_start_ms, 500, 5000);
    config.silence_end_ms = (uint16_t)cfgGetInt(o, "sil_end", config.silence_end_ms, 400, 4000);
    config.sleep_timeout_min = (uint16_t)cfgGetInt(o, "sleep_min", config.sleep_timeout_min, 0, 120);
    config.max_tokens = (uint16_t)cfgGetInt(o, "max_tokens", config.max_tokens, 80, 2048);
    config.temperature10 = (uint8_t)cfgGetInt(o, "temp10", config.temperature10, 0, 20);
    config.tts_speed = (uint8_t)cfgGetInt(o, "tts_speed", config.tts_speed, 80, 125);
    config.face_pack = (uint8_t)cfgGetInt(o, "face_pack", config.face_pack, 0, 4);
    config.checkin_min = (uint16_t)cfgGetInt(o, "chk_min", config.checkin_min, 0, 720);
    config.checkin_window_s = (uint16_t)cfgGetInt(o, "chk_win", config.checkin_window_s, 60, 900);
    config.web_search = (uint8_t)cfgGetInt(o, "web_search", config.web_search, 0, 1);
    config.searxng_port = (uint16_t)cfgGetInt(o, "sx_p", config.searxng_port, 1, 65535);
    config.music_port = (uint16_t)cfgGetInt(o, "mu_p", config.music_port, 1, 65535);
    config.ollama_port_pc = (uint16_t)cfgGetInt(o, "ol_p1", config.ollama_port_pc, 1, 65535);
    config.ollama_port_pi = (uint16_t)cfgGetInt(o, "ol_p2", config.ollama_port_pi, 1, 65535);
    config.tts_local_port1 = (uint16_t)cfgGetInt(o, "tts_lp1", config.tts_local_port1, 1, 65535);
    config.tts_local_port2 = (uint16_t)cfgGetInt(o, "tts_lp2", config.tts_local_port2, 1, 65535);
    if (o.containsKey("chk_quiet")) { checkinQuiet = (uint16_t)cfgGetInt(o, "chk_quiet", checkinQuiet, 0, 720); }
    return n;
}

void NetworkManager::writeConfigBackupFile() {
    if (!LittleFS.begin(true)) return;
    JsonDocument doc;
    configToJson(doc);
    File f = LittleFS.open("/config_backup.json", "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
    }
}

void NetworkManager::handleApiConfigBackup() {
    JsonDocument doc;
    configToJson(doc);
    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

void NetworkManager::handleApiConfigRestore() {
    String body = server->hasArg("plain") ? server->arg("plain") : "";
    if (body.length() == 0) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Falta JSON (plain) o {\\\"from\\\":\\\"flash\\\"}\"}");
        return;
    }
    JsonDocument inDoc;
    JsonDocument fileDoc;
    if (deserializeJson(inDoc, body) != DeserializationError::Ok) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"JSON inválido\"}");
        return;
    }
    JsonObjectConst o = inDoc.as<JsonObjectConst>();
    const char *from = o["from"] | "";
    if (strcmp(from, "flash") == 0) {
        // Recuperar copia automática de LittleFS (superviviente a NVS corrupto)
        if (!LittleFS.begin(true)) {
            server->send(500, "application/json", "{\"success\":false,\"error\":\"LittleFS no disponible\"}");
            return;
        }
        File f = LittleFS.open("/config_backup.json", "r");
        if (!f) {
            server->send(404, "application/json", "{\"success\":false,\"error\":\"Sin copia en flash (hacé un Guardar primero)\"}");
            return;
        }
        DeserializationError e = deserializeJson(fileDoc, f);
        f.close();
        if (e) {
            server->send(500, "application/json", "{\"success\":false,\"error\":\"Copia en flash corrupta\"}");
            return;
        }
        o = fileDoc.as<JsonObjectConst>();
    }
    int n = configFromJson(o);
    if (n < 0) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"No es un backup Kraken válido\"}");
        return;
    }
    saveConfig(); // persiste en NVS + reescribe la copia flash
    audio.setVolume(config.volume);
    audio.setPlaybackSpeed(config.tts_speed);
    String out = String("{\"success\":true,\"applied\":") + n + ",\"msg\":\"Config restaurada (WiFi/IP fija piden reboot)\"}";
    server->send(200, "application/json", out);
}

const WifiCredential* NetworkManager::getNetwork(int index) const {
    if (index >= 0 && index < networkCount) {
        return &networks[index];
    }
    return nullptr;
}

bool NetworkManager::addOrUpdateNetwork(const char *ssid, const char *pass, bool persist) {
    if (!ssid || strlen(ssid) == 0) return false;

    // Verificar si ya existe
    for (int i = 0; i < networkCount; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            if (pass && strlen(pass) > 0) {
                strncpy(networks[i].pass, pass, sizeof(networks[i].pass) - 1);
                networks[i].pass[sizeof(networks[i].pass) - 1] = '\0';
            }
            if (persist) saveNetworks();
            return true;
        }
    }

    // Agregar nueva red si hay espacio
    if (networkCount < MAX_WIFI_NETWORKS) {
        strncpy(networks[networkCount].ssid, ssid, sizeof(networks[networkCount].ssid) - 1);
        networks[networkCount].ssid[sizeof(networks[networkCount].ssid) - 1] = '\0';
        if (pass) {
            strncpy(networks[networkCount].pass, pass, sizeof(networks[networkCount].pass) - 1);
            networks[networkCount].pass[sizeof(networks[networkCount].pass) - 1] = '\0';
        } else {
            networks[networkCount].pass[0] = '\0';
        }
        networkCount++;
        if (persist) saveNetworks();
        return true;
    }
    return false;
}

bool NetworkManager::removeNetwork(const char *ssid) {
    if (!ssid || strlen(ssid) == 0) return false;
    for (int i = 0; i < networkCount; i++) {
        if (strcmp(networks[i].ssid, ssid) == 0) {
            for (int j = i; j < networkCount - 1; j++) {
                networks[j] = networks[j + 1];
            }
            networkCount--;
            saveNetworks();
            return true;
        }
    }
    return false;
}

void NetworkManager::loadNetworks() {
    networkCount = 0;
    bool loadedFromLfs = false;

    if (LittleFS.begin(true) && LittleFS.exists("/networks.json")) {
        File f = LittleFS.open("/networks.json", "r");
        if (f) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err && doc.is<JsonArray>()) {
                JsonArray arr = doc.as<JsonArray>();
                for (JsonObject item : arr) {
                    const char *s = item["ssid"] | "";
                    const char *p = item["pass"] | "";
                    if (strlen(s) > 0 && networkCount < MAX_WIFI_NETWORKS) {
                        strncpy(networks[networkCount].ssid, s, sizeof(networks[networkCount].ssid) - 1);
                        networks[networkCount].ssid[sizeof(networks[networkCount].ssid) - 1] = '\0';
                        strncpy(networks[networkCount].pass, p, sizeof(networks[networkCount].pass) - 1);
                        networks[networkCount].pass[sizeof(networks[networkCount].pass) - 1] = '\0';
                        networkCount++;
                    }
                }
                loadedFromLfs = (networkCount > 0);
            }
        }
    }

    // Garantizar que las redes esenciales y recuperadas del aparato SIEMPRE existan y esten actualizadas:
    // 1. Red actual activa en la PC: TeleCentro-5618 / RMZ5GDZKRDM3
    // 2. Red secundaria de la casa: Telecentro-d1fc / MMLNTJFTY2W5
    // 3. Hotspot de rescate movil (guion): Jorge-Moto_G32 / asdfghjk
    // 4. Red recuperada de NVS del aparato: MOVISTAR-WIFI6-0E70 / Lacarra3424
    // 5. Hotspot de rescate movil (guion bajo): Jorge_Moto_G32 / asdfghjk
    addOrUpdateNetwork(DEFAULT_WIFI_SSID1, DEFAULT_WIFI_PASS1, false);
    addOrUpdateNetwork(DEFAULT_WIFI_SSID2, DEFAULT_WIFI_PASS2, false);
    addOrUpdateNetwork(DEFAULT_WIFI_SSID3, DEFAULT_WIFI_PASS3, false);
    addOrUpdateNetwork(DEFAULT_WIFI_SSID4, DEFAULT_WIFI_PASS4, false);
    addOrUpdateNetwork(DEFAULT_WIFI_SSID5, DEFAULT_WIFI_PASS5, false);
    if (!loadedFromLfs) {
        if (strlen(config.wifi_ssid1) > 0) addOrUpdateNetwork(config.wifi_ssid1, config.wifi_pass1, false);
        if (strlen(config.wifi_ssid2) > 0) addOrUpdateNetwork(config.wifi_ssid2, config.wifi_pass2, false);
    }
    saveNetworks();

    // Sincronizar campos retrocompatibles
    if (networkCount > 0) {
        strncpy(config.wifi_ssid1, networks[0].ssid, sizeof(config.wifi_ssid1) - 1);
        strncpy(config.wifi_pass1, networks[0].pass, sizeof(config.wifi_pass1) - 1);
    }
    if (networkCount > 1) {
        strncpy(config.wifi_ssid2, networks[1].ssid, sizeof(config.wifi_ssid2) - 1);
        strncpy(config.wifi_pass2, networks[1].pass, sizeof(config.wifi_pass2) - 1);
    }
}

void NetworkManager::saveNetworks() {
    if (!LittleFS.begin(true)) return;
    File f = LittleFS.open("/networks.json", "w");
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < networkCount; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["ssid"] = networks[i].ssid;
        item["pass"] = networks[i].pass;
    }
    serializeJson(doc, f);
    f.close();
    log_i("Persistidas %d redes WiFi en /networks.json", networkCount);
}

bool NetworkManager::begin() {
    loadConfig();
    loadNetworks();
    return true;
}

bool NetworkManager::connectKnownNetworks(uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Full durante la conexión; powerRelax() aplica
    // modem-sleep al entrar en idle/reloj (plan leve de energía).
    // Plan leve: tope de potencia TX hogareña (11 dBm sobra con RSSI -60s
    // y recorta el pico de corriente del radio en cada transmisión).
    WiFi.setTxPower(WIFI_POWER_11dBm);

    // IP fija opcional
    if (strlen(config.esp_static_ip) > 0) {
        IPAddress ip, gw, sn;
        if (ip.fromString(config.esp_static_ip) && gw.fromString(config.esp_gateway) && sn.fromString(config.esp_subnet)) {
            WiFi.config(ip, gw, sn, gw);
            log_i("IP fija configurada: %s", config.esp_static_ip);
        } else {
            log_w("IP fija inválida, usando DHCP");
        }
    }

    if (networkCount == 0) {
        loadNetworks();
    }

    // 1. Algoritmo de Itinerancia (Roaming Inteligente):
    // Escaneo rápido para encontrar cuál de nuestras redes conocidas tiene mejor señal aquí
    log_i("Escaneando espectro Wi-Fi para itinerancia inteligente...");
    int nScan = WiFi.scanNetworks(false, false, false, 250);
    int bestMatchIdx = -1;
    int bestRssi = -999;

    if (nScan > 0) {
        for (int i = 0; i < nScan; i++) {
            String detectedSsid = WiFi.SSID(i);
            int detectedRssi = WiFi.RSSI(i);
            for (int k = 0; k < networkCount; k++) {
                if (detectedSsid.equals(networks[k].ssid)) {
                    if (detectedRssi > bestRssi) {
                        bestRssi = detectedRssi;
                        bestMatchIdx = k;
                    }
                    break;
                }
            }
        }
    }

    // Si encontramos una red visible conocida con buen RSSI, nos conectamos prioritariamente a ella
    if (bestMatchIdx >= 0) {
        log_i("Itinerancia: Red óptima detectada '%s' (RSSI: %d dBm). Asociando...",
              networks[bestMatchIdx].ssid, bestRssi);
        WiFi.begin(networks[bestMatchIdx].ssid, networks[bestMatchIdx].pass);
        unsigned long start = millis();
        while ((millis() - start) < 6000) {
            if (WiFi.status() == WL_CONNECTED) {
                log_i("¡Conectado exitosamente por Roaming! IP: %s (RSSI: %d dBm)",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
                audio.playSuccessSound();
                return true;
            }
            delay(200);
        }
        log_w("Fallo asociación prioritaria con '%s', probando cascada WiFiMulti...", networks[bestMatchIdx].ssid);
    }

    // 2. Fallback con WiFiMulti para todas las redes conocidas registradas
    for (int i = 0; i < networkCount; i++) {
        wifiMulti.addAP(networks[i].ssid, networks[i].pass);
    }
    // Red de rescate móvil garantizada
    wifiMulti.addAP(DEFAULT_WIFI_SSID3, DEFAULT_WIFI_PASS3);

    log_i("Conectando con respaldo WiFiMulti a %d redes conocidas...", networkCount);
    unsigned long startMulti = millis();
    while ((millis() - startMulti) < timeoutMs) {
        if (wifiMulti.run() == WL_CONNECTED) {
            log_i("Connected via WiFiMulti! IP: %s (RSSI: %d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            audio.playSuccessSound();
            return true;
        }
        delay(250);
    }

    log_w("WiFi connection timed out");
    audio.playErrorSound();
    return false;
}

bool NetworkManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

void NetworkManager::startCaptivePortal() {
    portalActive = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    WiFi.softAPConfig(AP_PORTAL_IP, AP_PORTAL_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID_DEFAULT, AP_PASS_DEFAULT);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", AP_PORTAL_IP);

    if (server) delete server;
    server = new WebServer(80);
    setupWebServer();
    server->begin();

    log_i("Captive Portal AP started: '%s' IP: 192.168.4.1", AP_SSID_DEFAULT);
}

void NetworkManager::startWebServerSTA() {
    portalActive = false;
    if (server) delete server;
    server = new WebServer(80);
    setupWebServer();
    server->begin();
    log_i("Web server running in STA mode at http://%s", WiFi.localIP().toString().c_str());
}

void NetworkManager::handlePortal() {
    if (portalActive) {
        dnsServer.processNextRequest();
    }
    if (server) {
        server->handleClient();
    }
}

void NetworkManager::setupWebServer() {
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/save", HTTP_POST, [this]() { handleSave(); });
    server->on("/scan", HTTP_GET, [this]() { handleScan(); });
    server->on("/test_chime", HTTP_GET, [this]() {
        audio.playSuccessSound();
        server->send(200, "text/plain", "OK");
    });
    server->on("/status", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["uptime_s"] = millis() / 1000;
        doc["rssi"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["free_psram"] = ESP.getFreePsram();
        doc["volume"] = config.volume;
        doc["gemini_model"] = config.gemini_model;
        doc["face_pack"] = config.face_pack;
        doc["fw_md5"] = ESP.getSketchMD5();
        doc["fw_tag"] = "fb-fallback-1"; // marcador de build (verificar OTA)
        doc["boot_count"] = g_bootCount;
        doc["reset_reason"] = g_resetReason;
        doc["ota_hold_min"] = stayAwakeLeftMin();
        doc["checkin_min"] = config.checkin_min;
        doc["checkin_mode"] = checkinMode;
        doc["checkin_next_min"] = nextCheckinMin();
        doc["checkin_quiet"] = checkinQuiet;
        // Pre-vuelo de flash: el que flashea mira busy antes del push.
        // (2026-09-09: push con audio en curso = brick.)
        String busyReason = otaBusyReason();
        doc["busy"] = busyReason.length() > 0;
        doc["busy_reason"] = busyReason;
        BatteryStatus bst = BatteryMonitor::getStatus();
        doc["batt_pct"] = bst.percentage;
        doc["charging"] = bst.isCharging;
        String out;
        serializeJson(doc, out);
        server->send(200, "application/json", out);
    });
    server->on("/save_prompt", HTTP_POST, [this]() { handleSavePrompt(); });
    server->on("/api/face_pack", HTTP_POST, [this]() {
        if (server->hasArg("pack")) {
            int p = server->arg("pack").toInt();
            if (p >= 0 && p <= FACE_PACK_KRAKEN) {
                config.face_pack = (uint8_t)p;
                prefs.begin("assistant", false);
                prefs.putUChar("face_pack", config.face_pack);
                prefs.end();
                FaceRenderer::setFacePack(config.face_pack);
                epd.showFace(FACE_IDLE, 0, true);
                epd.showFace(FACE_IDLE, 0, true); // doble FULL: los packs de
                // negros pesados (Rudo) dejan fantasma con un solo barrido
                server->send(200, "application/json", "{\"success\":true,\"pack\":" + String(config.face_pack) + "}");
                return;
            }
        }
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Parámetro pack inválido (0..3)\"}");
    });
    server->on("/api/skills", HTTP_GET, [this]() { handleSkills(); });
    server->on("/api/chat", HTTP_POST, [this]() { handleApiChat(); });
    server->on("/api/say", HTTP_POST, [this]() { handleApiSay(); });
    server->on("/api/story", HTTP_POST, [this]() { handleApiStory(); });
    server->on("/api/sensor", HTTP_GET, [this]() { handleApiSensor(); });
    server->on("/api/selftest", HTTP_GET, [this]() { handleApiSelftest(); });
    server->on("/api/audio_probe", HTTP_GET, [this]() { handleApiAudioProbe(); });
    server->on("/api/config_backup", HTTP_GET, [this]() { handleApiConfigBackup(); });
    server->on("/api/config_restore", HTTP_POST, [this]() { handleApiConfigRestore(); });
    server->on("/api/action", HTTP_POST, [this]() { handleApiAction(); });
    server->on("/api/logs", HTTP_GET, [this]() { handleApiLogs(); });
    server->on("/api/logs_clear", HTTP_POST, [this]() { handleApiLogsClear(); });
    server->on("/api/ota_hold", HTTP_POST, [this]() { handleApiOtaHold(); });
    server->on("/api/bitmap", HTTP_POST, [this]() { handleApiBitmap(); });
    server->on("/api/screen", HTTP_GET, [this]() { handleApiScreen(); });
    server->on("/api/memory", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["facts"] = copitoMem.getFacts();
        doc["success"] = true;
        String out;
        serializeJson(doc, out);
        server->send(200, "application/json", out);
    });
    server->on("/api/memory_clear", HTTP_POST, [this]() {
        copitoMem.clearAll();
        server->send(200, "application/json", "{\"success\":true,\"msg\":\"Memoria borrada\"}");
    });
    server->on("/api/bank", HTTP_GET, [this]() { handleApiBankList(); });

    // Banco de voz: subida de MP3 a LittleFS /bank (sin borrar nada existente)
    server->on("/bank", HTTP_POST,
        [this]() { handleBankDone(); },
        [this]() { handleBankUpload(); }
    );
    
    // OTA Firmware Update Endpoints
    server->on("/update", HTTP_POST, 
        [this]() { handleOtaUpdate(); },
        [this]() { handleOtaUpload(); }
    );

    // WiFi Multi-Network Endpoints
    server->on("/api/wifi/list", HTTP_GET, [this]() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < networkCount; i++) {
            JsonObject item = arr.add<JsonObject>();
            item["ssid"] = networks[i].ssid;
            item["active"] = (WiFi.status() == WL_CONNECTED && WiFi.SSID().equals(networks[i].ssid));
        }
        String out;
        serializeJson(doc, out);
        server->send(200, "application/json", out);
    });

    server->on("/api/wifi/add", HTTP_POST, [this]() {
        String s = server->arg("ssid");
        String p = server->hasArg("pass") ? server->arg("pass") : "";
        s.trim();
        if (s.length() > 0) {
            bool ok = addOrUpdateNetwork(s.c_str(), p.c_str(), true);
            if (ok) {
                server->send(200, "application/json", "{\"success\":true,\"msg\":\"Red agregada con exito\"}");
                return;
            } else {
                server->send(400, "application/json", "{\"success\":false,\"error\":\"Limite de redes alcanzado (max 10)\"}");
                return;
            }
        }
        server->send(400, "application/json", "{\"success\":false,\"error\":\"SSID vacio o invalido\"}");
    });

    server->on("/api/wifi/remove", HTTP_POST, [this]() {
        String s = server->arg("ssid");
        s.trim();
        if (s.length() > 0 && removeNetwork(s.c_str())) {
            server->send(200, "application/json", "{\"success\":true,\"msg\":\"Red eliminada\"}");
            return;
        }
        server->send(400, "application/json", "{\"success\":false,\"error\":\"No se encontro la red\"}");
    });

    // Captive Portal Detection Redirection (Must return HTTP 302 Found)
    server->on("/generate_204", HTTP_GET, [this]() { handleNotFound(); }); // Android
    server->on("/hotspot-detect.html", HTTP_GET, [this]() { handleNotFound(); }); // iOS / Apple
    server->on("/connecttest.txt", HTTP_GET, [this]() { handleNotFound(); }); // Windows
    server->on("/ncsi.txt", HTTP_GET, [this]() { handleNotFound(); }); // Windows NCSI
    server->onNotFound([this]() { handleNotFound(); });
}

void NetworkManager::handleNotFound() {
    if (portalActive) {
        // Redirect all requests to portal home in AP mode
        String url = "http://" + AP_PORTAL_IP.toString() + "/";
        server->sendHeader("Location", url, true);
        server->send(302, "text/plain", "");
    } else {
        server->send(404, "text/plain", "Not Found");
    }
}

void NetworkManager::handleScan() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject net = arr.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["auth"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    server->send(200, "application/json", jsonStr);
}

void NetworkManager::handleRoot() {
    String html = F(
        "<!DOCTYPE html><html lang='es'><head>"
        "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Kraken AI - Centro de Control</title>"
        "<style>"
        ":root{--bg:#0d1117;--card:#161b22;--border:#30363d;--accent:#58a6ff;--accent2:#238636;--text:#c9d1d9;--sub:#8b949e;}"
        "*{box-sizing:border-box;margin:0;padding:0;}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--text);padding:16px;line-height:1.5;}"
        ".wrap{max-width:520px;margin:0 auto;}"
        ".card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:22px;margin-bottom:18px;box-shadow:0 8px 24px rgba(0,0,0,0.4);}"
        ".header{display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;}"
        "h1{font-size:20px;color:#fff;display:flex;align-items:center;gap:8px;}"
        ".badges{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:16px;}"
        ".badge{background:#21262d;border:1px solid var(--border);padding:4px 9px;border-radius:20px;font-size:11px;color:var(--sub);font-weight:600;}"
        ".badge.active{color:#3fb950;border-color:rgba(63,185,80,0.3);background:rgba(63,185,80,0.1);}"
        ".section-title{font-size:14px;font-weight:700;color:var(--accent);text-transform:uppercase;letter-spacing:0.5px;margin:20px 0 10px 0;display:flex;align-items:center;gap:6px;}"
        "label{display:block;margin-top:12px;font-size:12px;font-weight:600;color:var(--sub);}"
        "input,select,textarea{width:100%;padding:10px 12px;margin-top:5px;border-radius:8px;border:1px solid var(--border);background:#0d1117;color:#fff;font-size:13px;transition:0.15s;}"
        "input:focus,select:focus,textarea:focus{border-color:var(--accent);outline:none;box-shadow:0 0 0 2px rgba(88,166,255,0.2);}"
        "button{width:100%;padding:12px;border-radius:8px;border:none;background:var(--accent2);color:#fff;font-weight:700;font-size:14px;cursor:pointer;margin-top:18px;transition:0.15s;}"
        "button:hover{background:#2ea043;}"
        ".btn-sec{background:#21262d;border:1px solid var(--border);color:var(--text);margin-top:8px;font-size:12px;padding:9px;}"
        ".btn-sec:hover{background:#30363d;}"
        ".btn-sound{background:rgba(88,166,255,0.15);border:1px solid rgba(88,166,255,0.4);color:var(--accent);margin-top:10px;}"
        ".btn-sound:hover{background:rgba(88,166,255,0.25);}"
        ".vol-row{display:flex;align-items:center;gap:12px;margin-top:6px;}"
        ".vol-row input{flex:1;}"
        ".vol-val{font-size:13px;font-weight:700;color:var(--accent);min-width:40px;text-align:right;}"
        ".ota-box{background:#161b22;border:1px dashed #d29922;border-radius:12px;padding:16px;margin-top:18px;}"
        ".ota-title{font-size:13px;font-weight:700;color:#f0883e;margin-bottom:8px;display:flex;align-items:center;gap:6px;}"
        ".tabbar{position:sticky;top:0;z-index:50;display:flex;gap:6px;overflow-x:auto;background:var(--bg);padding:10px 2px;margin-bottom:6px;}"
        ".tabbtn{flex:0 0 auto;width:auto;margin-top:0;padding:9px 13px;font-size:12px;background:#21262d;border:1px solid var(--border);color:var(--sub);border-radius:20px;font-weight:700;}"
        ".tabbtn.active{background:rgba(88,166,255,0.15);border-color:var(--accent);color:var(--accent);}"
        ".tabhidden{display:none!important;}"
        "@media(min-width:900px){.wrap{max-width:980px;}.tabbar{overflow:visible;flex-wrap:wrap;}}"
        "</style></head><body><div class='wrap'><div class='card'>"
        "<div class='header'><h1>🐙 Kraken AI</h1><span class='badge active'>Online</span></div>"
        "<div class='badges'>"
        "<span class='badge'>IP: "
    );
    html += WiFi.localIP().toString();
    html += F("</span>"
        "<span class='badge'>WiFi: ");
    html += String(WiFi.RSSI()) + F(" dBm</span>"
        "<span class='badge'>Waveshare 1.54 E-Paper</span>"
        "<span class='badge'>ESP32-S3 8MB</span>"
        "</div>"

        "<button type='button' class='btn-sound' onclick='testSound()'>🔊 Probar Altavoz (Chime Amigable)</button>"
        "<div id='sndMsg' style='font-size:11px;color:#3fb950;text-align:center;margin-top:4px;display:none;'>¡Reproduciendo sonido en el parlante!</div>"
        "<div class='tabbar'>"
        "<button type='button' class='tabbtn active' data-tab='control' onclick='showTab(\"control\")'>🎛 Control</button>"
        "<button type='button' class='tabbtn' data-tab='foto' onclick='showTab(\"foto\")'>📷 Foto</button>"
        "<button type='button' class='tabbtn' data-tab='audio' onclick='showTab(\"audio\")'>🔊 Audio</button>"
        "<button type='button' class='tabbtn' data-tab='pantalla' onclick='showTab(\"pantalla\")'>🎭 Pantalla</button>"
        "<button type='button' class='tabbtn' data-tab='ia' onclick='showTab(\"ia\")'>🧠 IA</button>"
        "<button type='button' class='tabbtn' data-tab='red' onclick='showTab(\"red\")'>📡 Red</button>"
        "<button type='button' class='tabbtn' data-tab='registros' onclick='showTab(\"registros\")'>📋 Registros</button>"
        "<button type='button' class='tabbtn' data-tab='sistema' onclick='showTab(\"sistema\")'>⚙️ Sistema</button>"
        "</div>"

        "<div class='section-title' data-tab='control'>🎛 Control directo</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Hablar, música, pantalla y audio sin tocar configuración.</p>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='text' id='sayInput' placeholder='Decile algo (ej: hola copito)...' style='margin-top:0;font-size:12px;'>"
        "<button type='button' class='btn-sec' onclick='sendSay()' style='margin-top:0;width:auto;white-space:nowrap;padding:8px 14px;background:#238636;color:#fff;'>🔊 Decir</button>"
        "</div>"
        "<div id='sayStatus' style='font-size:11px;margin-top:6px;display:none;'></div>"
        "<div style='display:flex;gap:8px;margin-top:10px;margin-bottom:8px;'>"
        "<input type='text' id='musInput' placeholder='Suena en Kraken (ej: soda stereo)...' style='margin-top:0;font-size:12px;'>"
        "<button type='button' class='btn-sec' onclick='sonaQuery()' style='margin-top:0;width:auto;white-space:nowrap;padding:8px 14px;'>🎵 Sonar</button>"
        "</div>"
        "<div style='display:flex;gap:6px;flex-wrap:wrap;'>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"que hora es\")' style='margin-top:0;width:auto;'>🕐 Hora</button>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"sintoniza mega\")' style='margin-top:0;width:auto;'>📻 Mega</button>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"sintoniza aspen\")' style='margin-top:0;width:auto;'>📻 Aspen</button>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"para la musica\")' style='margin-top:0;width:auto;'>⏹ Parar</button>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"volumen 85\")' style='margin-top:0;width:auto;'>🔊 Vol 85</button>"
        "<button type='button' class='btn-sec' onclick='sendPreset(\"pon el clima\")' style='margin-top:0;width:auto;'>🌡️ Clima</button>"
        "</div>"
        "<div id='presetStatus' style='font-size:11px;margin-top:6px;display:none;'></div>"
        "<div style='display:flex;gap:8px;margin-top:10px;'>"
        "<button type='button' class='btn-sec' onclick='runAction(\"idle\")' style='margin-top:0;'>🖼 Sacar foto / Cara</button>"
        "<button type='button' class='btn-sec' onclick='probeRun(\"synth\")' style='margin-top:0;'>🔬 Sonda audio</button>"
        "</div>"
        "<div id='probeStatus' style='font-size:11px;margin-top:6px;display:none;font-family:monospace;'></div>"
        "</div>"

        "<div class='section-title' data-tab='foto'>📷 Foto a la pantalla</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Subí una foto: se convierte a blanco y negro 200x200 en tu celu/PC y queda <b>fija</b> hasta que toques Sacar (no gasta batería).</p>"
        "<input type='file' id='photoFile' accept='image/*' onchange='previewPhoto(this)' style='padding:6px;border:none;background:#0d1117;'>"
        "<div style='display:flex;gap:8px;margin-top:8px;'>"
        "<select id='ditherSel' style='margin-top:0;flex:1;' onchange='if(window._photoImg)previewPhoto(document.getElementById(\"photoFile\"))'>"
        "<option value='fs'>Sombreado suave (Floyd-Steinberg)</option>"
        "<option value='bw'>Alto contraste (blanco y negro puro)</option>"
        "</select>"
        "</div>"
        "<div style='text-align:center;margin-top:8px;'><canvas id='photoCanvas' width='200' height='200' style='width:200px;height:200px;border:1px solid var(--border);border-radius:8px;background:#fff;'></canvas></div>"
        "<div id='dropZone' style='margin-top:8px;border:1px dashed var(--border);border-radius:8px;padding:14px;text-align:center;font-size:12px;color:var(--sub);'>O arrastrá la foto hasta acá 📥</div>"
        "<div style='display:flex;gap:8px;'>"
        "<button type='button' id='photoSend' class='btn-sec' onclick='sendPhoto()' style='margin-top:8px;background:#238636;color:#fff;' disabled>📤 Mandar a pantalla</button>"
        "<button type='button' class='btn-sec' onclick='runAction(\"idle\")' style='margin-top:8px;'>🖼 Sacar foto</button>"
        "</div>"
        "<div id='photoStatus' style='font-size:11px;margin-top:6px;display:none;'></div>"
        "</div>"

        "<div class='section-title' data-tab='red'>📡 Redes WiFi Guardadas (Itinerancia Inteligente)</div>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Kraken escanea y se conecta automáticamente a la red con MEJOR señal (mayor RSSI). No sobreescribe tus otras redes.</p>"
        "<div id='knownWifiList' style='display:flex;flex-direction:column;gap:6px;margin-bottom:12px;'>"
    );
    for (int i = 0; i < networkCount; i++) {
        html += F("<div style='display:flex;align-items:center;justify-content:space-between;background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:8px 12px;'>");
        html += F("<div><strong style='color:#fff;font-size:13px;'>");
        html += htmlEscape(networks[i].ssid);
        html += F("</strong>");
        if (WiFi.status() == WL_CONNECTED && WiFi.SSID().equals(networks[i].ssid)) {
            html += F(" <span class='badge active' style='margin-left:6px;padding:2px 6px;font-size:10px;'>Conectado (");
            html += String(WiFi.RSSI());
            html += F(" dBm)</span>");
        }
        html += F("</div>");
        html += F("<button type='button' class='btn-sec' style='width:auto;margin:0;padding:4px 8px;font-size:11px;background:#21262d;' onclick='deleteWifi(\"");
        html += htmlEscape(networks[i].ssid);
        html += F("\")'>🗑 Quitar</button></div>");
    }
    html += F(
        "</div>"
        "<form action='/save' method='POST'>"
        "<label>Agregar / Editar WiFi (SSID):</label>"
        "<input type='text' id='s1' name='ssid1' value='");
    html += (networkCount > 0 ? networks[0].ssid : config.wifi_ssid1);
    html += F("' required>"
        "<label>Contraseña WiFi:</label>"
        "<input type='password' name='pass1' value='");
    html += (networkCount > 0 ? networks[0].pass : config.wifi_pass1);
    html += F("'>"
        "<button type='button' class='btn-sec' onclick='scanWifi()'>🔍 Escanear Redes Cercanas</button>"
        "<div id='scanList' style='margin-top:6px;display:none;'>"
        "<select onchange='document.getElementById(\"s1\").value=this.value;' id='scanSel'></select>"
        "</div>"

        "<label>WiFi Secundaria / Alternativa (SSID opcional):</label>"
        "<input type='text' name='ssid2' value='");
    html += (networkCount > 1 ? networks[1].ssid : config.wifi_ssid2);
    html += F("'>"
        "<label>Contraseña WiFi Secundaria:</label>"
        "<input type='password' name='pass2' value='");
    html += (networkCount > 1 ? networks[1].pass : config.wifi_pass2);
    html += F("'>"
        "<div class='section-title' data-tab='red'>📌 IP Fija de la Placa (opcional)</div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Vacío = DHCP. Fijarla evita perderla si el módem se reinicia (sugerida: 192.168.0.180).</p>"
        "<label>IP Fija (vacío = DHCP):</label>"
        "<input type='text' name='esp_ip' value='");
    html += config.esp_static_ip;
    html += F("' placeholder='192.168.0.180'>"
        "<label>Puerta de Enlace:</label>"
        "<input type='text' name='esp_gw' value='");
    html += config.esp_gateway;
    html += F("'>"
        "<label>Máscara de Subred:</label>"
        "<input type='text' name='esp_sn' value='");
    html += config.esp_subnet;
    html += F("'>"

        "<div class='section-title' data-tab='ia'>🧠 Inteligencia Artificial (Direct HTTPS)</div>"
        "<label>Google Gemini API Key (Google AI Studio):</label>"
        "<input type='password' name='gemini_key' placeholder='Clave Gemini...' value='");
    html += config.gemini_api_key;
    html += F("' required>"

        "<label>Modelo de Razonamiento Gemini:</label>"
        "<select name='gemini_model'>"
        "<option value='gemini-3.1-flash-lite'");
    if (String(config.gemini_model) == "gemini-3.1-flash-lite" || strlen(config.gemini_model) == 0) html += F(" selected");
    html += F(">Gemini 3.1 Flash Lite (Equilibrado: base Pro, barato - Recomendado)</option>"
        "<option value='gemini-2.5-flash'");
    if (String(config.gemini_model) == "gemini-2.5-flash") html += F(" selected");
    html += F(">Gemini 2.5 Flash (Fallback con grounding gratis)</option>"
        "<option value='gemini-3.5-flash-lite'");
    if (String(config.gemini_model) == "gemini-3.5-flash-lite") html += F(" selected");
    html += F(">Gemini 3.5 Flash Lite (Nuevo default de Google, mas caro)</option>"
        "<option value='gemini-2.5-flash-lite'");
    if (String(config.gemini_model) == "gemini-2.5-flash-lite") html += F(" selected");
    html += F(">Gemini 2.5 Flash Lite (Ultra barato, menor calidad)</option>"
        "</select>");
    html += String("<label style='margin-top:8px;font-weight:400;'><input type='checkbox' name='web_search' value='1' style='width:auto;'")
        + String(config.web_search ? " checked" : "")
        + String("> Busqueda web propia ([TOOL:WEB_SEARCH] datos actuales)</label>");
    html += F("<label>SearXNG (servidor de busqueda en tu red):</label>"
        "<input type='text' name='sx_h' placeholder='192.168.0.x (vacio = solo DuckDuckGo)' value='");
    html += config.searxng_host;
    html += F("'>"
        "<label>SearXNG puerto:</label>"
        "<input type='number' name='sx_p' min='1' max='65535' value='");
    html += String(config.searxng_port);
    html += F("'>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Sirve SearXNG (Docker) en tu PC/Pi5. Si no responde, Kraken cae a DuckDuckGo sin API key.</p>"
        "<label>PiMusic API (servidor de musica en tu red, Pi3):</label>"
        "<input type='text' name='mu_h' placeholder='192.168.0.x (vacio = comandos de musica off)' value='");
    html += config.music_host;
    html += F("'>"
        "<label>PiMusic puerto:</label>"
        "<input type='number' name='mu_p' min='1' max='65535' value='");
    html += String(config.music_port);
    html += F("'>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Comandos de voz sin IA: 'sona ...' / 'pon ...' / 'pone ...' suenan en Kraken, 'musica ...' en el parlante Pi3, 'para la musica' frena, 'pausa', 'que esta sonando'. Cara: 'cara kraken' / 'cara copito'.</p>"
        "<label>PiMusic publico (Tailscale Funnel, para usar fuera de casa):</label>"
        "<input type='text' name='mu_ph' placeholder='xxx.tailXXX.ts.net (vacio = solo LAN)' value='");
    html += config.music_pub_host;
    html += F("'>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Si Kraken no alcanza la LAN (ej. hotspot en la calle), usa este host por HTTPS.</p>"
        "<label>Clave del funnel (X-Copito-Key, voz y cuentos fuera de casa):</label>"
        "<input type='password' name='fk' placeholder='hex de funnel_key (vacio = sin clave)' value='");
    html += config.funnel_key;
    html += F("'>"
        "<div class='section-title' data-tab='ia'>Cascada IA (si Gemini falla: Groq, Cerebras, Pollinations, Ollama PC, Ollama Pi)</div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Keys gratuitas sin tarjeta: console.groq.com, cloud.cerebras.ai y enter.pollinations.ai. Ollama: host vacio = saltear. Ya vienen preconfiguradas.</p>"
        "<label>Groq API Key (gratis):</label>"
        "<input type='password' name='ai_gk' placeholder='gsk_...' value='");
    html += config.ai_groq_key;
    html += F("'>"
        "<label>Cerebras API Key (gratis):</label>"
        "<input type='password' name='ai_ck' placeholder='csk-...' value='");
    html += config.ai_cerebras_key;
    html += F("'>"
        "<label>Pollinations Key (opcional, gratis en enter.pollinations.ai):</label>"
        "<input type='password' name='ai_pk' placeholder='anonimo suele dar 402' value='");
    html += config.ai_pollinations_key;
    html += F("'>"
        "<label>SambaNova Key (respaldos, requiere tarjeta):</label>"
        "<input type='password' name='ai_sn' placeholder='UUID' value='");
    html += config.ai_sambanova_key;
    html += F("'>"
        "<label>Mistral Key (respaldos, límite free):</label>"
        "<input type='password' name='ai_mi' placeholder='mW...' value='");
    html += config.ai_mistral_key;
    html += F("'>"
        "<label>xAI Grok Key (respaldos, requiere créditos):</label>"
        "<input type='password' name='ai_xa' placeholder='xai-...' value='");
    html += config.ai_xai_key;
    html += F("'>"
        "<label>Ollama PC (potente, ej IP de tu PC):</label>"
        "<input type='text' name='ol_h1' placeholder='192.168.0.x (vacio = saltear)' value='");
    html += config.ollama_host_pc;
    html += F("'>"
        "<label>Ollama PC puerto / modelo:</label>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='number' name='ol_p1' min='1' max='65535' value='");
    html += String(config.ollama_port_pc);
    html += F("' style='margin-top:5px;'>"
        "<input type='text' name='ol_m1' value='");
    html += config.ollama_model_pc;
    html += F("' style='margin-top:5px;'>"
        "</div>"
        "<label>Ollama Pi5 (siempre viva):</label>"
        "<input type='text' name='ol_h2' value='");
    html += config.ollama_host_pi;
    html += F("'>"
        "<label>Ollama Pi5 puerto / modelo liviano:</label>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='number' name='ol_p2' min='1' max='65535' value='");
    html += String(config.ollama_port_pi);
    html += F("' style='margin-top:5px;'>"
        "<input type='text' name='ol_m2' value='");
    html += config.ollama_model_pi;
    html += F("' style='margin-top:5px;'>"
        "</div>"

        "<div class='section-title' data-tab='audio'>🔊 Voz (TTS) y Servidores Locales</div>"
        "<label>Proveedor de Voz (TTS):</label>");
    html += F("<select name='tts_prov'>"
        "<option value='5'");
    if (config.tts_provider == TTS_LOCAL) html += F(" selected");
    html += F(">🏠 Servidores Locales LAN (PC Kokoro + Pi5 Piper, $0 - Recomendado)</option>"
        "<option value='1'");
    if (config.tts_provider == TTS_GOOGLE_FREE) html += F(" selected");
    html += F(">Google Voz Gratuita (Sin API Key, MP3 24kHz - Rápido, Ilimitado y Nítido)</option>"
        "<option value='0'");
    if (config.tts_provider == TTS_GEMINI) html += F(" selected");
    html += F(">Google Gemini TTS (gemini-3.1-flash-tts-preview - Voz Aoede)</option>"
        "<option value='3'");
    if (config.tts_provider == TTS_ELEVENLABS) html += F(" selected");
    html += F(">ElevenLabs (Audio Ultra Natural, Requiere Clave)</option>"
        "<option value='4'");
    if (config.tts_provider == TTS_OPENAI) html += F(" selected");
    html += F(">OpenAI TTS (tts-1, Requiere Clave)</option>"
        "<option value='2'");
    if (config.tts_provider == TTS_VOICERSS) html += F(" selected");
    html += F(">VoiceRSS (REST Gratuito WAV 16kHz)</option>"
        "</select>"

        "<label>Clave API de TTS (Solo para ElevenLabs / OpenAI):</label>"
        "<input type='password' name='tts_key' placeholder='Opcional con Google Voz Gratuita' value='");
    html += config.tts_api_key;
    html += F("'>"
        "<div class='section-title' data-tab='audio'>🏠 Servidores Locales de Voz (LAN, $0)</div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Se prueban primero (PC, luego Pi5). Si están apagados se salta solo a la nube en ~2s.</p>"
        "<label>Servidor Primario - IP (PC Kokoro):</label>"
        "<input type='text' name='tts_lh1' value='");
    html += config.tts_local_host1;
    html += F("'>"
        "<label>Puerto Primario:</label>"
        "<input type='number' name='tts_lp1' min='1' max='65535' value='");
    html += String(config.tts_local_port1);
    html += F("'>"
        "<label>Servidor Secundario - IP (Pi5 Piper):</label>"
        "<input type='text' name='tts_lh2' value='");
    html += config.tts_local_host2;
    html += F("'>"
        "<label>Puerto Secundario:</label>"
        "<input type='number' name='tts_lp2' min='1' max='65535' value='");
    html += String(config.tts_local_port2);
    html += F("'>"
        "<label>Voz Local (personalidad):</label>"
        "<select name='tts_lv'>"
        "<option value='davefx'");
    if (String(config.tts_local_voice) == "davefx") html += F(" selected");
    html += F(">Davefx - Masculina (Piper, España)</option>"
        "<option value='daniela'");
    if (String(config.tts_local_voice) == "daniela") html += F(" selected");
    html += F(">Daniela - Femenina (Piper, Argentina)</option>"
        "<option value='dora'");
    if (String(config.tts_local_voice) == "dora") html += F(" selected");
    html += F(">Dora - Femenina (Kokoro, Español neutro)</option>"
        "<option value='alex'");
    if (String(config.tts_local_voice) == "alex") html += F(" selected");
    html += F(">Alex - Masculina (Kokoro, Español neutro)</option>"
        "</select>");

    html += F("<div class='section-title' data-tab='audio'>🔊 Volumen del Parlante</div>"
        "<div class='vol-row'>"
        "<input type='range' name='volume' min='0' max='100' value='");
    html += String(config.volume);
    html += F("' oninput='document.getElementById(\"volDsp\").innerText=this.value+\"%\";'>"
        "<span class='vol-val' id='volDsp'>");
    html += String(config.volume) + F("%</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:4px;'>100% = máximo lineal ES8311 (0 dBFS). Auto-nivelador de voz con soft-limiter: fuerte y claro, sin saturar.</p>"

        "<div class='section-title' data-tab='pantalla'>🎭 Pack de Caras y Expresiones (E-Paper 1.54\")</div>"
        "<label>Estilo Visual de Kraken:</label>"
        "<select name='face_pack' id='facePackSel'>"
        "<option value='4'");
    if (config.face_pack == FACE_PACK_KRAKEN) html += F(" selected");
    html += F(">🐙 Kraken (Pulpo enojado pero servicial - experimental)</option>"
        "<option value='0'");
    if (config.face_pack == FACE_PACK_RUDO) html += F(" selected");
    html += F(">🚬 Rudo / Badass (Habano encendido, humo animado y mirada canchera)</option>"
        "<option value='1'");
    if (config.face_pack == FACE_PACK_CLASICO) html += F(" selected");
    html += F(">😊 Copito Clásico (Pack alternativo: tierno con sonrisa alegre)</option>"
        "<option value='2'");
    if (config.face_pack == FACE_PACK_FEMENINO) html += F(" selected");
    html += F(">🎀 Femenino / Cyber Maiden (Pestañas estilizadas y lazo cyber)</option>"
        "<option value='3'");
    if (config.face_pack == FACE_PACK_GEEK) html += F(" selected");
    html += F(">🕶️ Geek / 8-Bit Gamer (Gafas pixeladas estilo deal with it)</option>"
        "</select>"
        "<button type='button' class='btn-sec' onclick='applyFacePackNow()' style='margin-top:8px;background:rgba(88,166,255,0.15);color:var(--accent);border:1px solid rgba(88,166,255,0.4);font-weight:600;'>🎨 Cambiar Pack al Vuelo (Sin Reiniciar)</button>"
        "<div id='facePackStatus' style='font-size:12px;text-align:center;margin-top:6px;display:none;'></div>"

        "<div class='section-title' data-tab='ia'>🧠 Personalidad & Agent Harness (NanoClaw)</div>"
        "<label>Instrucciones de Personalidad, Comportamiento y Reglas de Skills:</label>"
        "<textarea id='promptArea' name='prompt' rows='9' maxlength='4000' oninput='updatePromptCount()'>");
    html += htmlEscape(config.system_prompt);
    html += F("</textarea>"
        "<div style='display:flex;justify-content:space-between;font-size:11px;color:var(--sub);margin-top:4px;'>"
        "<span>Soporta hasta 4000 caracteres (LittleFS)</span>"
        "<span id='promptCount'>0 / 4000</span>"
        "</div>"
        "<button type='button' class='btn-sec' onclick='savePromptOnly()' style='margin-top:8px;background:rgba(88,166,255,0.15);color:var(--accent);border:1px solid rgba(88,166,255,0.4);font-weight:600;'>⚡ Guardar Prompt al Vuelo (Sin Reiniciar)</button>"
        "<div id='promptStatus' style='font-size:12px;text-align:center;margin-top:6px;display:none;'></div>"

        "<div class='section-title' data-tab='ia' style='margin-top:22px;'>⚡ Skills & Agent Tools (NanoClaw)</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:6px;'>"
        "<span style='font-size:12px;'>🌡️ <b>Sensor Ambiental SHTC3:</b> I2C 0x70</span><span class='badge active'>Activo</span>"
        "</div>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:10px;'>Mide temperatura y humedad en tiempo real, mostrándolas en la barra de estado del E-Paper.</p>"
        "<div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:6px;'>"
        "<span style='font-size:12px;'>🔋 <b>Monitor LiPo ADC:</b> GPIO 4</span><span class='badge active'>Activo</span>"
        "</div>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:10px;'>Lectura porcentual precisa de la batería integrada.</p>"
        "<div style='display:flex;align-items:center;justify-content:space-between;margin-bottom:6px;'>"
        "<span style='font-size:12px;'>🎭 <b>Tools de Dibujo en Pantalla:</b> E-Paper 1.54\"</span><span class='badge active'>Activo</span>"
        "</div>"
        "<p style='color:var(--sub);font-size:11px;'>Soporta [DRAW:CORAZON], [DRAW:ESTRELLA], [DRAW:SOL], [DRAW:NUBE], [DRAW:CASA], [DRAW:MUSICA], [DRAW:LUNA], [DRAW:LIBRO], [DRAW:FLOR] y [DRAW:RAYO]. Mas [FACE:...], [TEXT:...], [MODE:RELOJ|CUENTO|CLIMA|DORMIDO] y [REMEMBER:...].</p>"
        "</div>"

        "<div class='section-title' data-tab='ia' style='margin-top:22px;'>🌐 Servidor Local & API REST (Bajo Consumo)</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Interactúa directamente con Kraken desde curl, Home Assistant o scripts HTTP:</p>"
        "<div style='font-size:11px;color:#58a6ff;background:#161b22;padding:6px 10px;border-radius:6px;margin-bottom:8px;font-family:monospace;'>POST /api/chat &nbsp; | &nbsp; POST /api/say &nbsp; | &nbsp; GET /api/sensor</div>"
        "<div style='display:flex;gap:8px;'>"
        "<input type='text' id='apiMsgInput' placeholder='Escribe algo para Kraken (ej: ¿Qué hora es?)...' style='margin-top:0;font-size:12px;'>"
        "<button type='button' class='btn-sec' onclick='sendApiChat()' style='margin-top:0;width:auto;white-space:nowrap;padding:8px 14px;background:#238636;color:#fff;'>💬 Enviar</button>"
        "</div>"
        "<div id='apiChatStatus' style='font-size:11px;margin-top:6px;display:none;'></div>"
        "</div>"

        "<div class='section-title' data-tab='control' style='margin-top:22px;'>🎛 Modos Rápidos (Sin IA)</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Botones directos a la placa, sin gastar llamadas a Gemini:</p>"
        "<div style='display:flex;gap:8px;'>"
        "<button type='button' class='btn-sec' onclick='runAction(\"reloj\")' style='margin-top:0;'>🕐 Reloj</button>"
        "<button type='button' class='btn-sec' onclick='runAction(\"clima\")' style='margin-top:0;'>🌡️ Clima</button>"
        "<button type='button' class='btn-sec' onclick='runAction(\"sleep\")' style='margin-top:0;'>😴 Dormir</button>"
        "</div>"
        "<div id='modeStatus' style='font-size:11px;margin-top:6px;display:none;'></div>"
        "</div>"

        "<div class='section-title' data-tab='control' style='margin-top:22px;'>🧠 Memoria de Kraken</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Lo que recuerda de Jorge (se guarda cuando decís \"acordate que...\"):</p>"
        "<div id='memFacts' style='font-size:12px;background:#161b22;padding:8px 10px;border-radius:6px;margin-bottom:8px;'>Tocá \"Ver memoria\".</div>"
        "<div style='display:flex;gap:8px;'>"
        "<button type='button' class='btn-sec' onclick='loadMemory()' style='margin-top:0;'>↻ Ver memoria</button>"
        "<button type='button' class='btn-sec' onclick='clearMemory()' style='margin-top:0;'>🗑 Borrar</button>"
        "</div>"
        "</div>"

        "<div class='section-title' data-tab='registros' style='margin-top:22px;'>📋 Registros del Dispositivo (debug sin USB)</div>"
        "<div style='background:#0d1117;border:1px solid var(--border);border-radius:8px;padding:12px;margin-top:6px;'>"
        "<p style='color:var(--sub);font-size:11px;margin-bottom:8px;'>Bitácora en RAM (sesión actual) + WARN/ERROR persistidos en flash. Ideal para cazar bugs intermitentes.</p>"
        "<div style='display:flex;gap:8px;margin-bottom:8px;'>"
        "<select id='logLevel' style='margin-top:0;flex:1;'>"
        "<option value='I'>Todo (INFO+)</option>"
        "<option value='W' selected>Solo avisos y errores</option>"
        "<option value='E'>Solo errores</option>"
        "</select>"
        "<select id='logSrc' style='margin-top:0;flex:1;'>"
        "<option value='ram' selected>RAM (actual)</option>"
        "<option value='flash'>Flash (persistido)</option>"
        "</select>"
        "</div>"
        "<div id='logView' style='font-size:11px;background:#161b22;padding:8px 10px;border-radius:6px;margin-bottom:8px;font-family:monospace;white-space:pre-wrap;max-height:220px;overflow-y:auto;'>Tocá \"Ver registros\".</div>"
        "<div style='display:flex;gap:8px;'>"
        "<button type='button' class='btn-sec' onclick='loadLogs()' style='margin-top:0;'>↻ Ver registros</button>"
        "<button type='button' class='btn-sec' onclick='downloadLogs()' style='margin-top:0;'>⬇ Descargar</button>"
        "<button type='button' class='btn-sec' onclick='clearLogs()' style='margin-top:0;'>🗑 Borrar</button>"
        "</div>"
        "<label style='margin-top:8px;font-weight:400;'><input type='checkbox' id='logAuto' style='width:auto;' onchange='toggleLogAuto()'> Auto-actualizar cada 5s</label>"
        "</div>"

        "<div class='section-title' data-tab='ia'>⚙️ Parámetros Avanzados de IA</div>"

        "<label>Máximo de Tokens de Respuesta Gemini (80–2048):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='max_tokens' min='80' max='2048' step='20' value='");
    html += String(config.max_tokens);
    html += F("' oninput='document.getElementById(\"tokDsp\").innerText=this.value;'>"
        "<span class='vol-val' id='tokDsp'>");
    html += String(config.max_tokens) + F("</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Respuestas más largas = más tokens. Aumenta la latencia levemente.</p>"

        "<label>Temperatura / Creatividad Gemini (0–2.0):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='temp10' min='0' max='20' step='1' value='");
    html += String(config.temperature10);
    html += F("' oninput='document.getElementById(\"tmpDsp\").innerText=(this.value/10).toFixed(1);'>"
        "<span class='vol-val' id='tmpDsp'>");
    html += String(config.temperature10 / 10.0f, 1) + F("</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>0.0 = muy preciso/determinista. 1.0 = equilibrado. 2.0 = muy creativo/aleatorio.</p>"

        "<div class='section-title' data-tab='audio'>🎙️ Configuración del Micrófono y Grabación</div>"

        "<label>Tiempo Máximo de Grabación (segundos, 3–30):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='max_rec_s' min='3' max='30' step='1' value='");
    html += String(config.max_rec_seconds);
    html += F("' oninput='document.getElementById(\"recDsp\").innerText=this.value+\"s\";'>"
        "<span class='vol-val' id='recDsp'>");
    html += String(config.max_rec_seconds) + F("s</span></div>"

        "<label>Timeout Silencio Inicial - ms antes de cancelar si no hay voz (500–5000):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='sil_start' min='500' max='5000' step='100' value='");
    html += String(config.silence_start_ms);
    html += F("' oninput='document.getElementById(\"silStDsp\").innerText=(this.value/1000).toFixed(1)+\"s\";'>"
        "<span class='vol-val' id='silStDsp'>");
    html += String(config.silence_start_ms / 1000.0f, 1) + F("s</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Si pulsás y no hablás en este tiempo, se cancela automáticamente.</p>"

        "<label>Silencio de Fin de Habla - ms de silencio para cortar (400–4000):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='sil_end' min='400' max='4000' step='100' value='");
    html += String(config.silence_end_ms);
    html += F("' oninput='document.getElementById(\"silEnDsp\").innerText=(this.value/1000).toFixed(1)+\"s\";'>"
        "<span class='vol-val' id='silEnDsp'>");
    html += String(config.silence_end_ms / 1000.0f, 1) + F("s</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Tiempo de pausa tras hablar antes de enviar al servidor. Más bajo = más rápido.</p>"

        "<div class='section-title' data-tab='sistema'>🔇 Reposo Automático</div>"

        "<label>Tiempo de Inactividad antes de Dormir (minutos, 0=nunca):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='sleep_min' min='0' max='120' step='1' value='");
    html += String(config.sleep_timeout_min);
    html += F("' oninput='document.getElementById(\"slpDsp\").innerText=this.value==0?\"Off\":this.value+\" min\";'>"
        "<span class='vol-val' id='slpDsp'>");
    html += (config.sleep_timeout_min == 0 ? String("Off") : String(config.sleep_timeout_min) + " min");
    html += F("</span></div>");
    html += F("<div class='section-title' data-tab='sistema'>Guardia Autonoma (check-in OTA)</div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>La placa se despierta sola cada N minutos, abre WiFi breve por si hay un flash pendiente y vuelve a dormir. Gasto aprox: 2 mAh/dia.</p>"
        "<label>Despertar cada N minutos (0=apagado, 5-720):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='checkin_min' min='0' max='120' step='5' value='");
    html += String(config.checkin_min);
    html += F("' oninput='document.getElementById(\"chkDsp\").innerText=this.value==0?\"Off\":this.value+\" min\";'>"
        "<span class='vol-val' id='chkDsp'>");
    html += (config.checkin_min == 0 ? String("Off") : String(config.checkin_min) + " min");
    html += F("</span></div>"
        "<label>Ventana WiFi tras despertar (segundos, 60-900):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='checkin_window_s' min='60' max='900' step='30' value='");
    html += String(config.checkin_window_s);
    html += F("' oninput='document.getElementById(\"chwDsp\").innerText=this.value+\"s\";'>"
        "<span class='vol-val' id='chwDsp'>");
    html += String(config.checkin_window_s) + F("s</span></div>"
        "<button type='button' class='btn-sec' onclick='stayAwake()' style='margin-top:8px;background:rgba(88,166,255,0.15);color:var(--accent);border:1px solid rgba(88,166,255,0.4);font-weight:600;'>Mantener despierto 60 min (para flashear OTA)</button>"
        "<div id='holdStatus' style='font-size:12px;text-align:center;margin-top:6px;display:none;'></div>");
    html += F("<div class='section-title' data-tab='audio'>🗣️ Voz y Síntesis de Habla</div>"

        "<label>Idioma TTS (ej: es, en, pt):</label>"
        "<input type='text' name='tts_lang' maxlength='6' value='");
    html += config.tts_language;
    html += F("' placeholder='es'>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>Código de idioma para Google Free TTS: es, en-US, pt-BR, fr, etc.</p>"

        "<label>Velocidad de Habla / TTS (0.75x a 1.50x):</label>"
        "<div class='vol-row'>"
        "<input type='range' name='tts_speed' min='75' max='150' step='5' value='");
    html += String(config.tts_speed);
    html += F("' oninput='document.getElementById(\"spdDsp\").innerText=(this.value/100).toFixed(2)+\"x\";'>"
        "<span class='vol-val' id='spdDsp'>");
    html += String(config.tts_speed / 100.0f, 2) + F("x</span></div>"
        "<p style='color:var(--sub);font-size:11px;margin-top:2px;'>1.00x = normal. 1.15x a 1.30x acelera la velocidad para respuestas ágiles.</p>"

        "<label>ElevenLabs Voice ID (solo si usás ElevenLabs TTS):</label>"
        "<input type='text' name='el_voice' value='");
    html += config.elevenlabs_voice;
    html += F("' placeholder='ID de voz de ElevenLabs'>");
    html += F(
        "<div class='tabgroup' data-tab='sistema'>"
        "<button type='submit'>💾 Guardar y Aplicar Cambios</button>"
        "<p style='color:var(--sub);font-size:11px;margin-top:6px;'>Guarda todo lo de las pestañas Audio, IA y Red.</p>"
        "</div>"
        "</form>"

        "<div class='ota-box tabgroup' data-tab='sistema'>"
        "<div class='ota-title'>📦 Actualización Inalámbrica de Firmware (OTA)</div>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='update' accept='.bin' required style='padding:6px;border:none;background:#0d1117;'>"
        "<button type='submit' style='background:#f0883e;margin-top:10px;'>🚀 Flashear Firmware .bin</button>"
        "</form></div>"

        "</div></div>"

        "<script>"
        "function updatePromptCount(){"
        "  var a=document.getElementById('promptArea');"
        "  var c=document.getElementById('promptCount');"
        "  if(a&&c){"
        "    c.innerText=a.value.length+' / 4000';"
        "    c.style.color=a.value.length>3800?'#f85149':'var(--sub)';"
        "  }"
        "}"
        "function savePromptOnly(){"
        "  var a=document.getElementById('promptArea');"
        "  var s=document.getElementById('promptStatus');"
        "  s.style.display='block';"
        "  s.style.color='var(--accent)';"
        "  s.innerText='Guardando prompt en memoria LittleFS...';"
        "  var p=new URLSearchParams();"
        "  p.append('prompt', a.value);"
        "  fetch('/save_prompt',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      if(res.success){"
        "        s.style.color='#3fb950';"
        "        s.innerText='✅ '+res.msg+' ('+res.len+' caracteres guardados)';"
        "      }else{"
        "        s.style.color='#f85149';"
        "        s.innerText='❌ Error: '+(res.error||'desconocido');"
        "      }"
        "      setTimeout(()=>{s.style.display='none';},4000);"
        "    })"
        "    .catch(e=>{"
        "      s.style.color='#f85149';"
        "      s.innerText='❌ Error de red al comunicar con Kraken';"
        "      setTimeout(()=>{s.style.display='none';},4000);"
        "    });"
        "}"
        "function sendApiChat(){"
        "  var inp=document.getElementById('apiMsgInput');"
        "  var st=document.getElementById('apiChatStatus');"
        "  if(!inp.value.trim()) return;"
        "  st.style.display='block';st.style.color='var(--accent)';"
        "  st.innerText='Enviando mensaje al agente...';"
        "  var p=new URLSearchParams();p.append('message',inp.value);"
        "  fetch('/api/chat',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      if(res.success){"
        "        st.style.color='#3fb950';"
        "        st.innerText='🤖 '+res.reply+(res.tool_executed!=='none'?' ['+res.tool_executed+']':'');"
        "      }else{"
        "        st.style.color='#f85149';st.innerText='❌ '+res.error;"
        "      }"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';});"
        "}"
        "function testSound(){"
        "  var msg=document.getElementById('sndMsg');"
        "  fetch('/test_chime').then(r=>{msg.style.display='block';setTimeout(()=>{msg.style.display='none';},3000);});"
        "}"
        "function showTab(name){"
        "  try{localStorage.setItem('krakenTab',name);}catch(e){}"
        "  var btns=document.querySelectorAll('.tabbtn');"
        "  for(var i=0;i<btns.length;i++){btns[i].classList.toggle('active',btns[i].getAttribute('data-tab')===name);}"
        "  var titles=document.querySelectorAll('.section-title[data-tab]');"
        "  for(var t=0;t<titles.length;t++){"
        "    (function(title){"
        "      var show=title.getAttribute('data-tab')===name;"
        "      var el=title;"
        "      while(el){"
        "        var nx=el.nextElementSibling;"
        "        var tag=el.tagName;"
        "        var managed=el.classList&&el.classList.contains('tabgroup');"
        "        if(tag!=='FORM'&&tag!=='SCRIPT'&&!managed){el.classList.toggle('tabhidden',!show);}"
        "        el=nx;"
        "        if(el&&el.classList&&el.classList.contains('section-title'))break;"
        "      }"
        "    })(titles[t]);"
        "  }"
        "  var groups=document.querySelectorAll('.tabgroup[data-tab]');"
        "  for(var g=0;g<groups.length;g++){groups[g].classList.toggle('tabhidden',groups[g].getAttribute('data-tab')!==name);}"
        "  window.scrollTo(0,0);"
        "}"
        "try{showTab(localStorage.getItem('krakenTab')||'control');}catch(e){showTab('control');}"
        "function applyFacePackNow(){"
        "  var sel=document.getElementById('facePackSel');"
        "  var st=document.getElementById('facePackStatus');"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Cambiando pack en pantalla...';"
        "  var p=new URLSearchParams();p.append('pack',sel.value);"
        "  fetch('/api/face_pack',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      if(res.success){"
        "        st.style.color='#3fb950';st.innerText='✅ Pack actualizado al instante en pantalla!';"
        "      }else{"
        "        st.style.color='#f85149';st.innerText='❌ Error: '+(res.error||'desconocido');"
        "      }"
        "      setTimeout(()=>{st.style.display='none';},3500);"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';setTimeout(()=>{st.style.display='none';},3500);});"
        "}"
        "function runAction(a){"
        "  var st=document.getElementById('modeStatus');"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Enviando...';"
        "  var p=new URLSearchParams();p.append('action',a);"
        "  fetch('/api/action',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      st.style.color=res.success?'#3fb950':'#f85149';"
        "      st.innerText=res.success?'✅ Listo':'❌ '+(res.error||'error');"
        "      setTimeout(()=>{st.style.display='none';},3000);"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';});"
        "}"
        "function sendSay(){"
        "  var inp=document.getElementById('sayInput');"
        "  var st=document.getElementById('sayStatus');"
        "  if(!inp.value.trim())return;"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Hablando...';"
        "  var p=new URLSearchParams();p.append('text',inp.value);"
        "  fetch('/api/say',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      st.style.color=res.success?'#3fb950':'#f85149';"
        "      st.innerText=res.success?'✅ Hablando':'❌ '+(res.error||'error');"
        "      setTimeout(()=>{st.style.display='none';},3000);"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';});"
        "}"
        "function sendPreset(msg){"
        "  var st=document.getElementById('presetStatus');"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='⏳ '+msg+'...';"
        "  var p=new URLSearchParams();p.append('message',msg);"
        "  fetch('/api/chat',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      st.style.color=res.success?'#3fb950':'#f85149';"
        "      st.innerText=res.success?('🤖 '+(res.reply||'ok')):('❌ '+(res.error||'error'));"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';});"
        "}"
        "function sonaQuery(){"
        "  var inp=document.getElementById('musInput');"
        "  if(!inp.value.trim())return;"
        "  sendPreset('sona '+inp.value.trim());"
        "}"
        "function probeRun(mode){"
        "  var st=document.getElementById('probeStatus');"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Midiendo...';"
        "  fetch('/api/audio_probe?mode='+mode).then(r=>r.json()).then(res=>{"
        "    st.style.color=(res.verdict==='OK')?'#3fb950':'#f85149';"
        "    st.innerText='veredicto='+res.verdict+' pico='+res.peak_pre+'->'+res.peak_post"
        "      +' rms='+res.rms+' limitadas='+res.limited+' recorte='+res.clipped;"
        "  }).catch(e=>{st.style.color='#f85149';st.innerText='❌ Error de red';});"
        "}"
        "var _photoB64=null;"
        "function previewPhotoFile(f){"
        "  if(!f)return;"
        "  if(!f.type||f.type.indexOf('image/')!==0){"
        "    var st0=document.getElementById('photoStatus');"
        "    st0.style.display='block';st0.style.color='#f85149';"
        "    st0.innerText='❌ Eso no es imagen. Arrastrá un JPG/PNG.';"
        "    return;"
        "  }"
        "  var fake={files:[f]};"
        "  previewPhoto(fake);"
        "}"
        "function previewPhoto(inp){"
        "  var f=inp.files[0];if(!f)return;"
        "  var st0=document.getElementById('photoStatus');"
        "  st0.style.display='block';st0.style.color='var(--accent)';"
        "  st0.innerText='⏳ Elegida: '+f.name+' ('+Math.round(f.size/1024)+'KB). Leyendo...';"
        "  inp.value='';"
        "  document.getElementById('photoSend').disabled=true;"
        "  _photoB64=null;"
        "  if(f.size>20000000){st0.style.display='block';st0.style.color='#f85149';st0.innerText='❌ Foto muy pesada (>20MB). Sacale captura y probá con esa.';return;}"
        "  st0.style.display='block';st0.style.color='var(--accent)';st0.innerText='⏳ Leyendo foto...';"
        "  var img=new Image();"
        "  var _photoWatch=setTimeout(function(){"
        "    if(!_photoB64){st0.style.color='#f85149';st0.innerText='❌ Se colgó leyendo (ni cargó ni falló). Recargá la página o probá con captura JPG.';}"
        "  },10000);"
        "  img.onerror=function(){"
        "    clearTimeout(_photoWatch);"
        "    st0.style.color='#f85149';"
        "    st0.innerText='❌ El navegador no pudo leerla (¿HEIC de iPhone?). Pasala a JPG/PNG o sacale captura y probá con esa.';"
        "  };"
        "  img.onload=function(){"
        "    clearTimeout(_photoWatch);"
        "    try{"
        "    window._photoImg=img;"
        "    var c=document.getElementById('photoCanvas');"
        "    var x=c.getContext('2d');"
        "    var s=Math.min(img.width,img.height);"
        "    var sx=(img.width-s)/2,sy=(img.height-s)/2;"
        "    var tmp=document.createElement('canvas');tmp.width=200;tmp.height=200;"
        "    var tx=tmp.getContext('2d');"
        "    tx.drawImage(img,sx,sy,s,s,0,0,200,200);"
        "    var d=tx.getImageData(0,0,200,200);"
        "    var px=d.data;"
        "    var g=new Float32Array(40000);"
        "    for(var i=0;i<40000;i++){g[i]=0.299*px[i*4]+0.587*px[i*4+1]+0.114*px[i*4+2];}"
        "    var mode=document.getElementById('ditherSel').value;"
        "    var out=new Uint8Array(5000);"
        "    for(var i=0;i<5000;i++)out[i]=255;"
        "    for(var y=0;y<200;y++){"
        "      for(var xx=0;xx<200;xx++){"
        "        var o=g[y*200+xx];"
        "        var nv=(mode==='bw')?(o<128?0:255):(o<128?0:255);"
        "        var err=o-nv;"
        "        if(mode!=='bw'){"
        "          if(xx<199)g[y*200+xx+1]+=err*7/16;"
        "          if(y<199){"
        "            if(xx>0)g[(y+1)*200+xx-1]+=err*3/16;"
        "            g[(y+1)*200+xx]+=err*5/16;"
        "            if(xx<199)g[(y+1)*200+xx+1]+=err*1/16;"
        "          }"
        "        }"
        "        if(nv===0){out[(y*200+xx)>>3]&=~(128>>((xx)&7));}"
        "      }"
        "    }"
        "    var bin='';"
        "    for(var i=0;i<5000;i++)bin+=String.fromCharCode(out[i]);"
        "    _photoB64=btoa(bin);"
        "    var id=x.createImageData(200,200);"
        "    for(var i=0;i<40000;i++){var v=((out[i>>3]>>(7-((i)&7)))&1)?255:0;id.data[i*4]=v;id.data[i*4+1]=v;id.data[i*4+2]=v;id.data[i*4+3]=255;}"
        "    x.putImageData(id,0,0);"
        "    var st=document.getElementById('photoStatus');"
        "    st.style.display='block';st.style.color='#3fb950';st.innerText='✅ Vista previa lista ('+f.name+', '+_photoB64.length+' B). Tocá Mandar.';"
        "    document.getElementById('photoSend').disabled=false;"
        "    }catch(err){st0.style.color='#f85149';st0.innerText='❌ Se rompió procesando: '+err.message;}"
        "  };"
        "  img.src=URL.createObjectURL(f);"
        "}"
        "function sendPhoto(){"
        "  var st=document.getElementById('photoStatus');"
        "  if(!_photoB64){st.style.display='block';st.style.color='#f85149';st.innerText='❌ Elegí una foto primero (esperá la vista previa)';return;}"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Enviando 5000 bytes...';"
        "  fetch('/api/bitmap',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'plain='+encodeURIComponent(JSON.stringify({bitmap_b64:_photoB64}))})"
        "    .then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json();})"
        "    .then(res=>{"
        "      st.style.color=res.success?'#3fb950':'#f85149';"
        "      st.innerText=res.success?'✅ Foto fija en pantalla (Sacar la quita)':'❌ '+(res.error||'error');"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='❌ No llegó ('+e.message+'). Revisá WiFi y reintentá.';});"
        "}"
        "function initDropZone(){"
        "  var z=document.getElementById('dropZone');"
        "  if(!z||z._wired)return;"
        "  z._wired=true;"
        "  ['dragenter','dragover'].forEach(function(ev){"
        "    z.addEventListener(ev,function(e){e.preventDefault();z.style.borderColor='var(--accent)';z.style.color='var(--accent)';});"
        "  });"
        "  ['dragleave','drop'].forEach(function(ev){"
        "    z.addEventListener(ev,function(e){e.preventDefault();z.style.borderColor='var(--border)';z.style.color='var(--sub)';});"
        "  });"
        "  z.addEventListener('drop',function(e){"
        "    var f=e.dataTransfer&&e.dataTransfer.files&&e.dataTransfer.files[0];"
        "    if(f)previewPhotoFile(f);"
        "  });"
        "  window.addEventListener('dragover',function(e){e.preventDefault();});"
        "  window.addEventListener('drop',function(e){e.preventDefault();});"
        "}"
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',initDropZone);}else{initDropZone();}"
        "function loadMemory(){"
        "  var d=document.getElementById('memFacts');"
        "  d.innerText='Cargando...';"
        "  fetch('/api/memory').then(r=>r.json()).then(res=>{"
        "    d.innerText=(res.facts&&res.facts.length>0)?res.facts:'(vacía: Kraken aún no recuerda nada)';"
        "  }).catch(e=>{d.innerText='❌ Error de red';});"
        "}"
        "function clearMemory(){"
        "  var d=document.getElementById('memFacts');"
        "  if(!confirm('¿Borrar todo lo que Kraken recuerda de Jorge?'))return;"
        "  fetch('/api/memory_clear',{method:'POST'})"
        "    .then(r=>r.json())"
        "    .then(res=>{d.innerText=res.success?'(memoria borrada)':'❌ Error';})"
        "    .catch(e=>{d.innerText='❌ Error de red';});"
        "}"
        "var logTimer=null;"
        "function fmtLogs(res){"
        "  if(!res.success||!res.entries||res.entries.length==0)return '(sin registros para ese filtro)';"
        "  return res.entries.map(function(e){"
        "    if(e.raw)return e.raw;"
        "    var s=Math.floor(e.t/1000);"
        "    return '['+s+'s]['+e.lvl+']['+e.tag+'] '+e.msg;"
        "  }).join('\\n');"
        "}"
        "function loadLogs(){"
        "  var d=document.getElementById('logView');"
        "  var lv=document.getElementById('logLevel').value;"
        "  var src=document.getElementById('logSrc').value;"
        "  d.innerText='Cargando...';"
        "  fetch('/api/logs?n=120&level='+lv+'&src='+src).then(r=>r.json()).then(res=>{"
        "    d.innerText=fmtLogs(res);"
        "    d.scrollTop=d.scrollHeight;"
        "  }).catch(e=>{d.innerText='❌ Error de red';});"
        "}"
        "function downloadLogs(){"
        "  var lv=document.getElementById('logLevel').value;"
        "  var src=document.getElementById('logSrc').value;"
        "  fetch('/api/logs?n=120&level='+lv+'&src='+src).then(r=>r.json()).then(res=>{"
        "    var blob=new Blob([fmtLogs(res)],{type:'text/plain'});"
        "    var a=document.createElement('a');"
        "    a.href=URL.createObjectURL(blob);"
        "    a.download='kraken_logs_'+src+'.txt';"
        "    a.click();"
        "  });"
        "}"
        "function clearLogs(){"
        "  if(!confirm('¿Borrar la bitácora (RAM + flash)?'))return;"
        "  fetch('/api/logs_clear',{method:'POST'}).then(r=>r.json()).then(res=>{"
        "    document.getElementById('logView').innerText=res.success?'(bitácora borrada)':'❌ Error';"
        "  });"
        "}"
        "function toggleLogAuto(){"
        "  var on=document.getElementById('logAuto').checked;"
        "  if(on){loadLogs();logTimer=setInterval(loadLogs,5000);}"
        "  else{clearInterval(logTimer);logTimer=null;}"
        "}"
        "function stayAwake(){"
        "  var st=document.getElementById('holdStatus');"
        "  st.style.display='block';st.style.color='var(--accent)';st.innerText='Pidiendo 60 min despierto...';"
        "  var p=new URLSearchParams();p.append('minutes',60);"
        "  fetch('/api/ota_hold',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{"
        "      st.style.color=res.success?'#3fb950':'#f85149';"
        "      st.innerText=res.success?'Listo: despierto 60 min para OTA':'Error';"
        "      setTimeout(()=>{st.style.display='none';},4000);"
        "    }).catch(e=>{st.style.color='#f85149';st.innerText='Error de red';});"
        "}"
        "function scanWifi(){"
        "  var list=document.getElementById('scanList');"
        "  var sel=document.getElementById('scanSel');"
        "  sel.innerHTML='<option>Escaneando...</option>';list.style.display='block';"
        "  fetch('/scan').then(r=>r.json()).then(data=>{"
        "    sel.innerHTML='<option value=\"\">-- Selecciona una red detectada --</option>';"
        "    data.forEach(n=>{"
        "      var opt=document.createElement('option');"
        "      opt.value=n.ssid;"
        "      opt.text=n.ssid+' ('+n.rssi+' dBm)'+(n.auth?' 🔒':'');"
        "      sel.appendChild(opt);"
        "    });"
        "  });"
        "}"
        "function deleteWifi(ssid){"
        "  if(!confirm('¿Eliminar red WiFi '+ssid+'?'))return;"
        "  var p=new URLSearchParams();p.append('ssid',ssid);"
        "  fetch('/api/wifi/remove',{method:'POST',body:p})"
        "    .then(r=>r.json())"
        "    .then(res=>{if(res.success)location.reload();else alert('Error: '+res.error);})"
        "    .catch(e=>{alert('Error de red');});"
        "}"
        "setTimeout(updatePromptCount, 100);"
        "</script></body></html>"
    );
    server->send(200, "text/html; charset=utf-8", html);
}

void NetworkManager::handleSave() {
    if (server->hasArg("ssid1")) {
        strncpy(config.wifi_ssid1, server->arg("ssid1").c_str(), sizeof(config.wifi_ssid1) - 1);
        config.wifi_ssid1[sizeof(config.wifi_ssid1) - 1] = '\0';
        if (strlen(config.wifi_ssid1) > 0) {
            addOrUpdateNetwork(config.wifi_ssid1, server->hasArg("pass1") ? server->arg("pass1").c_str() : "", false);
        }
    }
    if (server->hasArg("pass1") && strlen(config.wifi_ssid1) > 0) {
        strncpy(config.wifi_pass1, server->arg("pass1").c_str(), sizeof(config.wifi_pass1) - 1);
        config.wifi_pass1[sizeof(config.wifi_pass1) - 1] = '\0';
    }
    if (server->hasArg("ssid2")) {
        strncpy(config.wifi_ssid2, server->arg("ssid2").c_str(), sizeof(config.wifi_ssid2) - 1);
        config.wifi_ssid2[sizeof(config.wifi_ssid2) - 1] = '\0';
        if (strlen(config.wifi_ssid2) > 0) {
            addOrUpdateNetwork(config.wifi_ssid2, server->hasArg("pass2") ? server->arg("pass2").c_str() : "", false);
        }
    }
    if (server->hasArg("pass2") && strlen(config.wifi_ssid2) > 0) {
        strncpy(config.wifi_pass2, server->arg("pass2").c_str(), sizeof(config.wifi_pass2) - 1);
        config.wifi_pass2[sizeof(config.wifi_pass2) - 1] = '\0';
    }
    saveNetworks();
    if (server->hasArg("gemini_key")) {
        strncpy(config.gemini_api_key, server->arg("gemini_key").c_str(), sizeof(config.gemini_api_key) - 1);
        config.gemini_api_key[sizeof(config.gemini_api_key) - 1] = '\0';
    }
    if (server->hasArg("gemini_model")) {
        strncpy(config.gemini_model, server->arg("gemini_model").c_str(), sizeof(config.gemini_model) - 1);
        config.gemini_model[sizeof(config.gemini_model) - 1] = '\0';
    }
    if (server->hasArg("tts_key")) {
        strncpy(config.tts_api_key, server->arg("tts_key").c_str(), sizeof(config.tts_api_key) - 1);
        config.tts_api_key[sizeof(config.tts_api_key) - 1] = '\0';
    }
    if (server->hasArg("tts_prov")) config.tts_provider = server->arg("tts_prov").toInt();
    if (server->hasArg("volume")) config.volume = constrain(server->arg("volume").toInt(), 0, 100);
    if (server->hasArg("prompt")) {
        strncpy(config.system_prompt, server->arg("prompt").c_str(), sizeof(config.system_prompt) - 1);
        config.system_prompt[sizeof(config.system_prompt) - 1] = '\0';
    }

    // === v2 extended fields ===
    if (server->hasArg("max_rec_s"))  config.max_rec_seconds   = (uint16_t)constrain(server->arg("max_rec_s").toInt(),  3,  30);
    if (server->hasArg("sil_start"))  config.silence_start_ms  = (uint16_t)constrain(server->arg("sil_start").toInt(),  500, 5000);
    if (server->hasArg("sil_end"))    config.silence_end_ms    = (uint16_t)constrain(server->arg("sil_end").toInt(),    400, 4000);
    if (server->hasArg("sleep_min"))  config.sleep_timeout_min = (uint16_t)constrain(server->arg("sleep_min").toInt(),  0,  120);
    if (server->hasArg("max_tokens")) config.max_tokens        = (uint16_t)constrain(server->arg("max_tokens").toInt(), 80, 2048);
    if (server->hasArg("temp10"))     config.temperature10     = (uint8_t) constrain(server->arg("temp10").toInt(),     0,   20);
    if (server->hasArg("tts_lang")) {
        strncpy(config.tts_language, server->arg("tts_lang").c_str(), sizeof(config.tts_language) - 1);
        config.tts_language[sizeof(config.tts_language) - 1] = '\0';
    }
    if (server->hasArg("el_voice")) {
        strncpy(config.elevenlabs_voice, server->arg("el_voice").c_str(), sizeof(config.elevenlabs_voice) - 1);
        config.elevenlabs_voice[sizeof(config.elevenlabs_voice) - 1] = '\0';
    }
    if (server->hasArg("tts_speed")) config.tts_speed = (uint8_t)constrain(server->arg("tts_speed").toInt(), 70, 160);
    if (server->hasArg("tts_lh1")) {
        strncpy(config.tts_local_host1, server->arg("tts_lh1").c_str(), sizeof(config.tts_local_host1) - 1);
        config.tts_local_host1[sizeof(config.tts_local_host1) - 1] = '\0';
    }
    if (server->hasArg("tts_lp1")) config.tts_local_port1 = (uint16_t)constrain(server->arg("tts_lp1").toInt(), 1, 65535);
    if (server->hasArg("tts_lh2")) {
        strncpy(config.tts_local_host2, server->arg("tts_lh2").c_str(), sizeof(config.tts_local_host2) - 1);
        config.tts_local_host2[sizeof(config.tts_local_host2) - 1] = '\0';
    }
    if (server->hasArg("tts_lp2")) config.tts_local_port2 = (uint16_t)constrain(server->arg("tts_lp2").toInt(), 1, 65535);
    if (server->hasArg("tts_lv")) {
        strncpy(config.tts_local_voice, server->arg("tts_lv").c_str(), sizeof(config.tts_local_voice) - 1);
        config.tts_local_voice[sizeof(config.tts_local_voice) - 1] = '\0';
    }
    // === Cascada IA: keys cloud + Ollamas LAN ===
    if (server->hasArg("ai_gk")) {
        strncpy(config.ai_groq_key, server->arg("ai_gk").c_str(), sizeof(config.ai_groq_key) - 1);
        config.ai_groq_key[sizeof(config.ai_groq_key) - 1] = '\0';
    }
    if (server->hasArg("ai_ck")) {
        strncpy(config.ai_cerebras_key, server->arg("ai_ck").c_str(), sizeof(config.ai_cerebras_key) - 1);
        config.ai_cerebras_key[sizeof(config.ai_cerebras_key) - 1] = '\0';
    }
    if (server->hasArg("ai_pk")) {
        strncpy(config.ai_pollinations_key, server->arg("ai_pk").c_str(), sizeof(config.ai_pollinations_key) - 1);
        config.ai_pollinations_key[sizeof(config.ai_pollinations_key) - 1] = '\0';
    }
    if (server->hasArg("ai_sn")) {
        strncpy(config.ai_sambanova_key, server->arg("ai_sn").c_str(), sizeof(config.ai_sambanova_key) - 1);
        config.ai_sambanova_key[sizeof(config.ai_sambanova_key) - 1] = '\0';
    }
    if (server->hasArg("ai_mi")) {
        strncpy(config.ai_mistral_key, server->arg("ai_mi").c_str(), sizeof(config.ai_mistral_key) - 1);
        config.ai_mistral_key[sizeof(config.ai_mistral_key) - 1] = '\0';
    }
    if (server->hasArg("ai_xa")) {
        strncpy(config.ai_xai_key, server->arg("ai_xa").c_str(), sizeof(config.ai_xai_key) - 1);
        config.ai_xai_key[sizeof(config.ai_xai_key) - 1] = '\0';
    }
    if (server->hasArg("ol_h1")) {
        strncpy(config.ollama_host_pc, server->arg("ol_h1").c_str(), sizeof(config.ollama_host_pc) - 1);
        config.ollama_host_pc[sizeof(config.ollama_host_pc) - 1] = '\0';
    }
    if (server->hasArg("ol_p1")) config.ollama_port_pc = (uint16_t)constrain(server->arg("ol_p1").toInt(), 1, 65535);
    if (server->hasArg("ol_m1")) {
        strncpy(config.ollama_model_pc, server->arg("ol_m1").c_str(), sizeof(config.ollama_model_pc) - 1);
        config.ollama_model_pc[sizeof(config.ollama_model_pc) - 1] = '\0';
    }
    if (server->hasArg("ol_h2")) {
        strncpy(config.ollama_host_pi, server->arg("ol_h2").c_str(), sizeof(config.ollama_host_pi) - 1);
        config.ollama_host_pi[sizeof(config.ollama_host_pi) - 1] = '\0';
    }
    if (server->hasArg("ol_p2")) config.ollama_port_pi = (uint16_t)constrain(server->arg("ol_p2").toInt(), 1, 65535);
    if (server->hasArg("ol_m2")) {
        strncpy(config.ollama_model_pi, server->arg("ol_m2").c_str(), sizeof(config.ollama_model_pi) - 1);
        config.ollama_model_pi[sizeof(config.ollama_model_pi) - 1] = '\0';
    }
    if (server->hasArg("esp_ip")) {
        strncpy(config.esp_static_ip, server->arg("esp_ip").c_str(), sizeof(config.esp_static_ip) - 1);
        config.esp_static_ip[sizeof(config.esp_static_ip) - 1] = '\0';
    }
    if (server->hasArg("esp_gw")) {
        strncpy(config.esp_gateway, server->arg("esp_gw").c_str(), sizeof(config.esp_gateway) - 1);
        config.esp_gateway[sizeof(config.esp_gateway) - 1] = '\0';
    }
    if (server->hasArg("esp_sn")) {
        strncpy(config.esp_subnet, server->arg("esp_sn").c_str(), sizeof(config.esp_subnet) - 1);
        config.esp_subnet[sizeof(config.esp_subnet) - 1] = '\0';
    }
    if (server->hasArg("checkin_min")) {
        int cm = server->arg("checkin_min").toInt();
        config.checkin_min = (cm == 0) ? 0 : (uint16_t)constrain(cm, 5, 720);
    }
    if (server->hasArg("checkin_window_s")) config.checkin_window_s = (uint16_t)constrain(server->arg("checkin_window_s").toInt(), 60, 900);
    config.web_search = server->hasArg("web_search") ? 1 : 0; // checkbox: ausente = off
    if (server->hasArg("sx_h")) {
        strncpy(config.searxng_host, server->arg("sx_h").c_str(), sizeof(config.searxng_host) - 1);
        config.searxng_host[sizeof(config.searxng_host) - 1] = '\0';
    }
    if (server->hasArg("sx_p")) config.searxng_port = (uint16_t)constrain(server->arg("sx_p").toInt(), 1, 65535);
    if (server->hasArg("mu_h")) {
        strncpy(config.music_host, server->arg("mu_h").c_str(), sizeof(config.music_host) - 1);
        config.music_host[sizeof(config.music_host) - 1] = '\0';
    }
    if (server->hasArg("mu_p")) config.music_port = (uint16_t)constrain(server->arg("mu_p").toInt(), 1, 65535);
    if (server->hasArg("mu_ph")) {
        strncpy(config.music_pub_host, server->arg("mu_ph").c_str(), sizeof(config.music_pub_host) - 1);
        config.music_pub_host[sizeof(config.music_pub_host) - 1] = '\0';
    }
    if (server->hasArg("fk")) {
        String fk = server->arg("fk");
        fk.trim();
        strncpy(config.funnel_key, fk.c_str(), sizeof(config.funnel_key) - 1);
        config.funnel_key[sizeof(config.funnel_key) - 1] = '\0';
    }
    if (server->hasArg("face_pack")) {
        config.face_pack = (uint8_t)constrain(server->arg("face_pack").toInt(), 0, 4);
        FaceRenderer::setFacePack(config.face_pack);
    }

    saveConfig();

    String successHtml = F(
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{background:#121212;color:#eee;font-family:sans-serif;text-align:center;padding:40px;}"
        ".card{background:#1e1e1e;border-radius:16px;padding:32px;max-width:400px;margin:auto;}"
        "h2{color:#4caf50;}</style></head><body><div class='card'>"
        "<h2>✅ Configuración Guardada</h2>"
        "<p>Reiniciando el Asistente Kraken para conectarse a la nueva red...</p>"
        "</div><script>setTimeout(function(){window.location.href='/';}, 5000);</script></body></html>"
    );
    server->send(200, "text/html; charset=utf-8", successHtml);
    delay(1000);
    ESP.restart();
}

void NetworkManager::handleApiLogs() {
    // GET /api/logs?n=80&level=I&src=ram
    // n: 1..120 entradas (las más recientes). level: I (todo), W (warn+error),
    // E (solo errores). src: ram (sesión actual + cola recargada) o flash (/logs.txt).
    size_t n = 80;
    if (server->hasArg("n")) {
        n = (size_t)constrain(server->arg("n").toInt(), 1, 120);
    }
    char minLevel = 'I';
    if (server->hasArg("level")) {
        String lv = server->arg("level");
        lv.toUpperCase();
        if (lv == "W" || lv == "WARN") minLevel = 'W';
        else if (lv == "E" || lv == "ERROR") minLevel = 'E';
    }
    bool fromFlash = server->hasArg("src") && server->arg("src") == "flash";
    String out;
    copitoLog.toJson(out, n, minLevel, fromFlash);
    server->send(200, "application/json", out);
}

void NetworkManager::handleApiLogsClear() {
    copitoLog.clear();
    server->send(200, "application/json", "{\"success\":true,\"msg\":\"Bitacora borrada (RAM + flash)\"}");
}

void NetworkManager::handleApiOtaHold() {
    // POST /api/ota_hold minutes=N (0..480): mantiene la placa despierta N min.
    // Persiste en NVS, así sobrevive al reboot que sigue a un flash OTA.
    // Si había un check-in en curso, el hold toma el control (ventana extendida).
    int m = server->hasArg("minutes") ? server->arg("minutes").toInt() : 0;
    m = constrain(m, 0, 480);
    beginStayAwake((uint16_t)m);
    server->send(200, "application/json",
                 "{\"success\":true,\"stay_awake_min\":" + String(m) + "}");
}

// === Pull-OTA: el llavero se flashea solo desde un .bin staged en el Pi3 ===
// Staging (requiere SSH): scp firmware.bin jorge@pi3:~/bot/copito_fw.bin
// El ESP32 compara MD5 staged vs sketch en ejecucion; si difiere, descarga por
// LAN o funnel y flashea. La descarga es publica a proposito (el firmware no es
// secreto); SUBIRLO exige SSH, asi nadie inyecta firmware por HTTP.
static bool otaGetVersion(bool tls, const String &host, uint16_t port,
                          String &outVer, size_t &outSize) {
    WiFiClient plain;
    WiFiClientSecure sec;
    WiFiClient *cli = &plain;
    if (tls) { sec.setInsecure(); sec.setTimeout(15); cli = &sec; }
    else plain.setTimeout(5);
    if (!cli->connect(host.c_str(), port)) return false;
    cli->print("GET /api/ota_version HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
    unsigned long t0 = millis();
    unsigned long budget = tls ? 25000 : 10000;
    int status = 0;
    String body;
    body.reserve(160);
    bool inBody = false;
    while ((cli->connected() || cli->available()) && (millis() - t0 < budget)) {
        if (!cli->available()) { delay(2); continue; }
        String line = cli->readStringUntil('\n');
        if (!inBody) {
            line.trim();
            if (line.startsWith("HTTP/1.")) {
                int sp = line.indexOf(' ');
                if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
            }
            if (status > 0 && line.length() == 0) inBody = true;
        } else {
            body += line;
            if (body.length() > 200) break;
        }
    }
    cli->stop();
    if (status != 200) return false;
    int vi = body.indexOf("\"version\"");
    int si = body.indexOf("\"size\"");
    if (vi < 0 || si < 0) return false;
    int q1 = body.indexOf('"', vi + 9);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0 || (q2 - q1) != 33) return false; // 32 hex + 1
    outVer = body.substring(q1 + 1, q2);
    int c = body.indexOf(':', si + 6);
    if (c < 0) return false;
    outSize = (size_t)body.substring(c + 1).toInt();
    return outSize > 0;
}

static bool otaDownloadAndFlash(bool tls, const String &host, uint16_t port,
                                 size_t total, const String &expectMd5) {
    if (total < 500000 || total > 4194304) return false; // bin imposible: no tocar flash
    WiFiClient plain;
    WiFiClientSecure sec;
    WiFiClient *cli = &plain;
    if (tls) { sec.setInsecure(); sec.setTimeout(20); cli = &sec; }
    else plain.setTimeout(8);
    if (!cli->connect(host.c_str(), port)) return false;
    cli->print("GET /api/ota_bin HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
    unsigned long t0 = millis();
    unsigned long budget = tls ? 30000 : 15000;
    int status = 0;
    bool inBody = false;
    while ((cli->connected() || cli->available()) && (millis() - t0 < budget)) {
        if (!cli->available()) { delay(2); continue; }
        String line = cli->readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.")) {
            int sp = line.indexOf(' ');
            if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
        }
        if (status > 0 && line.length() == 0) { inBody = true; break; }
    }
    if (!inBody || status != 200) { cli->stop(); return false; }
    if (!Update.begin(total)) { cli->stop(); return false; }
    // Verificación MD5 del staged: si un byte se corrompe en el hotspot, el
    // Update.end() falla y NO se rebootea al firmware roto (se sigue con el actual).
    bool wantMd5 = (expectMd5.length() == 32);
    if (wantMd5) {
        for (int i = 0; i < 32 && wantMd5; i++) {
            char c = expectMd5.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) wantMd5 = false;
        }
        if (wantMd5 && !Update.setMD5(expectMd5.c_str())) {
            CLOGW(TAG, "Pull-OTA: MD5 esperado inválido, sigo sin verificación");
            wantMd5 = false;
        }
    }
    uint8_t chunk[2048];
    size_t written = 0;
    unsigned long lastByte = millis();
    while (written < total && (cli->connected() || cli->available())) {
        size_t avail = cli->available();
        if (avail > 0) {
            size_t want = avail > sizeof(chunk) ? sizeof(chunk) : avail;
            if (want > total - written) want = total - written;
            int n = cli->readBytes((char *)chunk, want);
            if (n > 0) {
                if (Update.write(chunk, n) != (size_t)n) break;
                written += n;
                lastByte = millis();
                continue;
            }
        }
        if (millis() - lastByte > 60000) break; // 60s sin datos: abortar
        delay(2);
    }
    cli->stop();
    if (written < total) { Update.end(); return false; }
    if (!Update.end(true)) {
        CLOGE(TAG, "Pull-OTA: verificación falló (MD5/tamaño), descarto bin corrupto");
        return false;
    }
    return true;
}

int NetworkManager::pollStagedFirmware() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    // Pull diferido con música: descargar+flashear con el I2S en uso es lo
    // que brickió el 2026-09-09. Se reintenta en el próximo poll/boot.
    if (g_musicPlaying || musicBusy()) {
        CLOGI(TAG, "Pull-OTA diferido: musica en curso, se reintenta luego");
        return 0;
    }
    bool hasLan = config.music_host[0] != '\0';
    bool hasPub = config.music_pub_host[0] != '\0';
    if (!hasLan && !hasPub) return 0;
    String cur = ESP.getSketchMD5();
    cur.toLowerCase();
    String lanHost = String(config.music_host);
    uint16_t lanPort = config.music_port ? config.music_port : (uint16_t)DEFAULT_MUSIC_PORT;
    String ver;
    size_t size = 0;
    bool viaTls = false;
    bool gotVer = false;
    if (hasLan) {
        CLOGI(TAG, "Pull-OTA: consultando version en LAN %s:%u...", lanHost.c_str(), lanPort);
        gotVer = otaGetVersion(false, lanHost, lanPort, ver, size);
    }
    if (!gotVer && hasPub) {
        String pubHost = String(config.music_pub_host);
        CLOGI(TAG, "Pull-OTA: consultando version por funnel %s...", pubHost.c_str());
        gotVer = otaGetVersion(true, pubHost, 443, ver, size);
        viaTls = gotVer;
    }
    if (!gotVer) { CLOGI(TAG, "Pull-OTA: Pi3 sin respuesta (nada staged o sin red)"); return 0; }
    ver.toLowerCase();
    if (ver == cur) { CLOGI(TAG, "Pull-OTA: firmware al dia"); return 0; }
    CLOGI(TAG, "Pull-OTA: nuevo fw staged (%u bytes) via %s, descargando...",
          (unsigned)size, viaTls ? "funnel" : "LAN");
    beginStayAwake(20); // no dormir a mitad del flash (el hold sobrevive al reboot)
    powerBoost(); // plan leve: descarga+flash a plena CPU y radio full
    String host = viaTls ? String(config.music_pub_host) : lanHost;
    uint16_t port = viaTls ? 443 : lanPort;
    if (otaDownloadAndFlash(viaTls, host, port, size, ver)) {
        CLOGI(TAG, "Pull-OTA: flash OK, reiniciando...");
        delay(800);
        ESP.restart();
    }
    CLOGW(TAG, "Pull-OTA: fallo descarga/verificacion, sigo con el actual");
    powerRelax(); // la descarga falló: volver al perfil de reposo
    return 2;
}

// === Guardia autónoma: implementación ===
void NetworkManager::enterCheckinMode(uint16_t windowS) {
    checkinMode = true;
    checkinUntilMs = millis() + (uint32_t)windowS * 1000UL;
}

bool NetworkManager::checkinExpired() const {
    return checkinMode && (int32_t)(millis() - checkinUntilMs) >= 0;
}

void NetworkManager::beginStayAwake(uint16_t minutes) {
    config.ota_hold_min = minutes;
    holdUntilMs = (minutes == 0) ? 0 : millis() + (uint32_t)minutes * 60000UL;
    if (minutes > 0) {
        checkinMode = false;  // el hold toma el control
        noteActivity();       // hay alguien desarrollando: intervalo rápido
    }
    prefs.begin("assistant", false);
    prefs.putUShort("ota_hold", config.ota_hold_min);
    prefs.end();
}

void NetworkManager::noteActivity() {
    // Flash OTA u hold explícito = sesión de desarrollo probable.
    // Vuelve al intervalo rápido (5 min) para el próximo ciclo.
    checkinQuiet = 0;
    prefs.begin("assistant", false);
    prefs.putUShort("chk_quiet", checkinQuiet);
    prefs.end();
}

void NetworkManager::noteQuietCycle() {
    // Ciclo sin novedades: duplica el intervalo (5->10->20->...->tope).
    if (checkinQuiet < 10) checkinQuiet++;
    prefs.begin("assistant", false);
    prefs.putUShort("chk_quiet", checkinQuiet);
    prefs.end();
}

uint16_t NetworkManager::nextCheckinMin() const {
    if (config.checkin_min == 0) return 0;  // guardia desactivada
    uint32_t step = 5;
    for (uint16_t i = 0; i < checkinQuiet; i++) {
        step *= 2;
        if (step >= config.checkin_min) return config.checkin_min;
    }
    if (step > config.checkin_min) step = config.checkin_min;
    return (uint16_t)step;
}

uint32_t NetworkManager::stayAwakeLeftMin() {
    if (holdUntilMs == 0) {
        config.ota_hold_min = 0;
        return 0;
    }
    int32_t rest = (int32_t)(holdUntilMs - millis());
    if (rest <= 0) {
        holdUntilMs = 0;
        config.ota_hold_min = 0;
        prefs.begin("assistant", false);
        prefs.putUShort("ota_hold", 0);
        prefs.end();
        return 0;
    }
    uint32_t mins = (uint32_t)(rest + 59999) / 60000UL;
    config.ota_hold_min = (mins > 480) ? 480 : (uint16_t)mins;
    return mins;
}

void NetworkManager::persistHoldForSleep() {
    uint32_t mins = stayAwakeLeftMin();
    prefs.begin("assistant", false);
    prefs.putUShort("ota_hold", (uint16_t)mins);
    prefs.end();
}

void NetworkManager::handleOtaUpdate() {
    server->sendHeader("Connection", "close");
    if (s_otaBlocked) {
        // El upload se descartó sin tocar flash: informar en vez de reiniciar
        String out = "{\"success\":false,\"error\":\"Flash rechazado: " + s_otaBlockReason + "\"}";
        server->send(409, "application/json", out);
        CLOGW(TAG, "OTA push rechazado: %s", s_otaBlockReason.c_str());
        s_otaBlocked = false;
        s_otaBlockReason = "";
        return;
    }
    server->send(200, "text/plain", (Update.hasError()) ? "FALLO DE ACTUALIZACIÓN" : "ACTUALIZACIÓN EXITOSA. REINICIANDO...");
    delay(1000);
    ESP.restart();
}

void NetworkManager::handleOtaUpload() {
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
        s_otaBlocked = false;
        s_otaBlockReason = otaBusyReason();
        if (s_otaBlockReason.length() > 0) {
            // Ocupado: no se abre Update; los chunks se descartan y el
            // handler final responde 409 (ver handleOtaUpdate).
            s_otaBlocked = true;
            CLOGW(TAG, "OTA push bloqueado al inicio: %s (%s)",
                  s_otaBlockReason.c_str(), upload.filename.c_str());
            return;
        }
        log_i("OTA Update starting: %s", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (s_otaBlocked) return; // descartar sin escribir flash
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (s_otaBlocked) return; // el handler final responde 409
        if (Update.end(true)) {
            CLOGI(TAG, "OTA Update complete: %u bytes", upload.totalSize);
            noteActivity(); // flash = probable sesión de desarrollo: check-ins rápidos
        } else {
            Update.printError(Serial);
        }
    }
}

void NetworkManager::handleApiBankList() {
    JsonDocument doc;
    JsonArray arr = doc["files"].to<JsonArray>();
    File dir = LittleFS.open("/bank");
    if (dir && dir.isDirectory()) {
        File f;
        while ((f = dir.openNextFile())) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = String(f.name());
            o["size"] = (int)f.size();
            f.close();
        }
        dir.close();
    }
    doc["success"] = true;
    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

static File s_bankFile;
static bool s_bankOk = false;
static size_t s_bankSize = 0;

void NetworkManager::handleBankUpload() {
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
        s_bankOk = false;
        s_bankSize = 0;
        String name = upload.filename;
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        int dot = name.lastIndexOf('.');
        if (dot >= 0) name = name.substring(0, dot);
        name.trim();
        String clean = "";
        for (size_t i = 0; i < name.length() && clean.length() < 24; i++) {
            char c = name.charAt(i);
            if (isalnum(c) || c == '_' || c == '-') clean += c;
        }
        if (clean.length() == 0) return;
        LittleFS.mkdir("/bank");
        s_bankFile = LittleFS.open("/bank/" + clean + ".mp3", "w");
        s_bankOk = (bool)s_bankFile;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (s_bankFile && s_bankSize + upload.currentSize <= 120000) {
            if (s_bankFile.write(upload.buf, upload.currentSize) == upload.currentSize) {
                s_bankSize += upload.currentSize;
            } else {
                s_bankOk = false;
            }
        } else {
            s_bankOk = false; // muy grande: se descarta al cerrar
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (s_bankFile) s_bankFile.close();
        if (s_bankSize == 0) s_bankOk = false;
        CLOGI(TAG, "Bank upload: %u bytes ok=%d", s_bankSize, s_bankOk);
    }
}

void NetworkManager::handleBankDone() {
    server->send(200, "application/json", s_bankOk
        ? "{\"success\":true}"
        : "{\"success\":false,\"error\":\"Subida invalida o muy grande (max 120KB)\"}");
}

void NetworkManager::handleSavePrompt() {
    if (server->hasArg("prompt")) {
        String p = server->arg("prompt");
        strncpy(config.system_prompt, p.c_str(), sizeof(config.system_prompt) - 1);
        config.system_prompt[sizeof(config.system_prompt) - 1] = '\0';

        // 1. Guardar en LittleFS /prompt.txt (hasta 4096 bytes sin límites de bloque NVS)
        if (LittleFS.begin(true)) {
            File f = LittleFS.open("/prompt.txt", "w");
            if (f) {
                f.print(config.system_prompt);
                f.close();
            }
        }

        // 2. Guardar también en NVS como respaldo
        prefs.begin("assistant", false);
        prefs.putString("prompt", config.system_prompt);
        prefs.end();

        log_i("Prompt y Harness actualizados al vuelo con éxito: %u bytes", strlen(config.system_prompt));

        JsonDocument doc;
        doc["success"] = true;
        doc["len"] = strlen(config.system_prompt);
        doc["msg"] = "¡Prompt y Harness guardados al vuelo!";
        String out;
        serializeJson(doc, out);
        server->send(200, "application/json", out);
    } else {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Falta parametro prompt\"}");
    }
}

void NetworkManager::handleSkills() {
    JsonDocument doc;
    JsonArray skills = doc["skills"].to<JsonArray>();

    JsonObject s1 = skills.add<JsonObject>();
    s1["id"] = "shtc3_sensor";
    s1["name"] = "Sensor Ambiental SHTC3";
    s1["desc"] = "Mide temperatura ambiente y humedad relativa en tiempo real via I2C (0x70)";
    s1["enabled"] = true;

    JsonObject s2 = skills.add<JsonObject>();
    s2["id"] = "battery_sense";
    s2["name"] = "Monitor LiPo ADC";
    s2["desc"] = "Monitoreo de nivel de carga y voltaje de bateria (GPIO 4)";
    s2["enabled"] = true;

    JsonObject s3 = skills.add<JsonObject>();
    s3["id"] = "epaper_faces";
    s3["name"] = "Expresiones E-Paper 1.54";
    s3["desc"] = "Visualizacion de emociones graficas [DRAW:CORAZON, ESTRELLA, SOL, SONRISA]";
    s3["enabled"] = true;

    JsonObject s4 = skills.add<JsonObject>();
    s4["id"] = "audio_es8311";
    s4["name"] = "Audio Codec ES8311 & TTS";
    s4["desc"] = "Reproduccion de audio HD y control dinamico de velocidad 0.75x - 1.50x";
    s4["enabled"] = true;

    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

struct ApiSpeechJob {
    char text[768];
    int drawType;
    char sketch[512];
};

struct ApiMusicJob {
    char query[256]; // YouTube ("query") o radio ("RADIO|url|titulo") para Copito
};

// Guardia anti-solape de audio: dos TTS/música pisándose el I2S suenan
// corruptos. Cada tarea registra su handle; los endpoints responden 409 si
// hay audio en curso en vez de crear otra tarea encima.
static TaskHandle_t s_speechTaskH = NULL;
static TaskHandle_t s_musicTaskH = NULL;
static bool speechBusy() { return s_speechTaskH != NULL; }
static bool musicBusy() { return s_musicTaskH != NULL; }

static void apiMusicTask(void *pv) {
    s_musicTaskH = xTaskGetCurrentTaskHandle();
    // Inicio explícito de reproducción: recién acá se limpia un "para" previo.
    // (Los streamers ya no lo limpian solos: un stop concurrente no se pierde.)
    g_musicStopRequested = false;
    ApiMusicJob *job = (ApiMusicJob *)pv;
    if (job) {
        String qs = String(job->query);
        if (qs.startsWith("RADIO|")) {
            // Formato "RADIO|url|titulo": stream directo sin busqueda
            int sep = qs.indexOf('|', 6);
            String url = (sep > 6) ? qs.substring(6, sep) : qs.substring(6);
            String rtitle = (sep > 6) ? qs.substring(sep + 1) : String("Radio en vivo");
            log_i("API radio-ESP32: '%s'", rtitle.c_str());
            epd.showFallbackText(6, "Sintonizando...", true);
            bool ok = aiEngine.streamPcmUrlOnEsp32(url, rtitle);
            String done = ok ? String("♪ ") + rtitle : String("Sin señal");
            if (done.length() > 90) done = done.substring(0, 90);
            epd.showFallbackText(6, done.c_str(), true);
            if (!ok) {
                aiEngine.streamTtsToSpeaker("Sin señal de radio.", nullptr);
            }
        } else {
            String title;
            log_i("API musica-ESP32: stream '%s'", job->query);
            epd.showFallbackText(6, "Buscando...", true);
            bool ok = aiEngine.streamMusicOnEsp32(String(job->query), title);
            String done = ok ? String("♪ ") + title : String("Sin audio");
            if (done.length() > 90) done = done.substring(0, 90);
            epd.showFallbackText(6, done.c_str(), true);
            if (!ok) {
                aiEngine.streamTtsToSpeaker(
                    String("No pude conseguir ") + String(job->query) + ".", nullptr);
            }
        }
        g_textHold = true; // el "♪ título" queda fijado (si no, el idle lo borra)
        free(job);
    }
    s_musicTaskH = NULL;
    vTaskDelete(NULL);
}

static void apiSpeechTask(void *pv) {
    s_speechTaskH = xTaskGetCurrentTaskHandle();
    ApiSpeechJob *job = (ApiSpeechJob *)pv;
    if (job) {
        if (job->sketch[0] != '\0') {
            String cap = job->text;
            if (cap.length() > 24) cap = cap.substring(0, 24);
            epd.showVectorSketch(job->sketch, cap.c_str(), true);
        } else {
            int fontScale = (strlen(job->text) <= 80) ? 2 : 1;
            paginator.setText(job->text, fontScale);
            if (paginator.getTotalPages() <= 1) {
                epd.showFallbackText(job->drawType, job->text, true);
            } else {
                // NO tocar epd.getBuffer() crudo desde esta tarea: corría sin
                // mutex ni drenaje de cola y mezclaba mitades con la animación
                // de Core 0 (imagen rota). Render a buffer propio + showBitmap
                // (drena cola + mutex internos).
                uint8_t *tmp = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
                if (tmp) {
                    paginator.render(tmp, "KRAKEN AI");
                    epd.showBitmap(tmp, true);
                    free(tmp);
                } else {
                    epd.showFallbackText(job->drawType, job->text, true);
                }
                paginator.notifyRefreshExecuted(true);
            }
        }
        // El texto queda fijado: si no, el idle lo redibuja encima a los 6 s.
        g_textHold = true;
        aiEngine.streamTtsToSpeaker(job->text, nullptr);
        free(job);
    }
    s_speechTaskH = NULL;
    vTaskDelete(NULL);
}

// Cuento por portal (2026-09-09): la misma cascada ilustrada que por voz,
// en tarea de fondo, para probar el cuento desde la PC sin depender del mic.
// Ocupa el slot de voz (s_speechTaskH): /api/say responde 409 mientras dura.
struct ApiStoryJob {
    char topic[160];
};

static void apiStoryTask(void *pv) {
    s_speechTaskH = xTaskGetCurrentTaskHandle();
    ApiStoryJob *job = (ApiStoryJob *)pv;
    if (job) {
        String topic = String(job->topic);
        free(job);
        log_i("API cuento: '%s'", topic.c_str());
        copitoPokeActivity();
        aiEngine.storyTtsResetPref(); // cada cuento redescubre su voz
        // Pantalla de espera inmediata (el JSON + texto tardan ~1 min): si no,
        // el idle sigue mostrando la cara durante la gestación del cuento.
        epd.showFallbackText(8, "Pensando el cuento...", true);
        g_textHold = true;
        if (playStoryCascade(topic)) {
            copitoMem.addTurn(topic, "[cuento en cascada por portal]");
        } else {
            // Plan B clásico por portal: texto largo + voz narradora lenta,
            // igual que por voz (queryTextCascade ya trae modo cuento largo).
            log_w("API cuento: cascada no disponible, sigo con clasico");
            String storyText;
            if (aiEngine.queryTextCascade(String("contame un cuento infantil para ninos sobre ") + topic, storyText) &&
                storyText.length() >= STORY_MIN_CHARS) {
                // Pantalla también sin etiquetas (el parser de voz no corre acá)
                CopitoTags dropTags;
                CopitoTools::parseTags(storyText, dropTags);
                storyText.trim();
                paginator.setText(storyText.c_str(), 2);
                if (paginator.getTotalPages() > 10) paginator.setText(storyText.c_str(), 1);
                epd.renderPaginatorPage(paginator, "CUENTO", true);
                g_textHold = true;
                aiEngine.narrateStoryPart(storyText, nullptr);
                copitoMem.addTurn(topic, "[cuento clasico por portal]");
            } else {
                aiEngine.streamTtsToSpeaker(STORY_FALLBACK_TEXT, nullptr);
            }
        }
    }
    s_speechTaskH = NULL;
    vTaskDelete(NULL);
}

void NetworkManager::handleApiStory() {
    String topic = "";
    if (server->hasArg("topic")) {
        topic = server->arg("topic");
    } else if (server->hasArg("message")) {
        topic = server->arg("message");
    }
    topic.trim();
    if (topic.length() == 0) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Falta 'topic'\"}");
        return;
    }
    if (speechBusy() || musicBusy() || storyCascadeActive() || copitoVoiceBusy()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio o cuento en curso, reintenta en unos segundos\"}");
        return;
    }
    ApiStoryJob *job = (ApiStoryJob *)malloc(sizeof(ApiStoryJob));
    if (!job) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Sin memoria\"}");
        return;
    }
    strncpy(job->topic, topic.c_str(), sizeof(job->topic) - 1);
    job->topic[sizeof(job->topic) - 1] = '\0';
    log_i("API /api/story aceptado: '%s'", job->topic);
    xTaskCreatePinnedToCore(apiStoryTask, "api_story", 20480, job, 2, NULL, 1);
    JsonDocument resDoc;
    resDoc["success"] = true;
    resDoc["topic"] = job->topic;
    resDoc["mode"] = "story";
    String resJson;
    serializeJson(resDoc, resJson);
    server->send(200, "application/json", resJson);
}

void NetworkManager::handleApiChat() {
    String msg = "";
    if (server->hasArg("message")) {
        msg = server->arg("message");
    } else if (server->hasArg("plain")) {
        JsonDocument inDoc;
        if (deserializeJson(inDoc, server->arg("plain")) == DeserializationError::Ok) {
            msg = inDoc["message"] | "";
        }
    }

    if (msg.length() == 0) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Falta campo 'message'\"}");
        return;
    }

    log_i("API /api/chat recibido: '%s'", msg.c_str());

    // 0) Guardia local: sensores/bateria/hora se responden SIN gastar llamada a Gemini
    {
        String localReply;
        int localDraw = 0;
        if (CopitoTools::answerLocalText(msg, localReply, localDraw)) {
            if (speechBusy()) {
                server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio en curso, reintentá en unos segundos\"}");
                return;
            }
            log_i("API /api/chat respondido local (sin Gemini): '%s'", localReply.c_str());
            copitoMem.addTurn(msg, localReply);
            JsonDocument resDoc;
            resDoc["success"] = true;
            resDoc["user_message"] = msg;
            resDoc["reply"] = localReply;
            resDoc["tool_executed"] = "LOCAL";
            resDoc["local"] = true;
            String resJson;
            serializeJson(resDoc, resJson);
            server->send(200, "application/json", resJson);
            ApiSpeechJob *job = (ApiSpeechJob *)malloc(sizeof(ApiSpeechJob));
            if (job) {
                strncpy(job->text, localReply.c_str(), sizeof(job->text) - 1);
                job->text[sizeof(job->text) - 1] = '\0';
                job->drawType = localDraw;
                job->sketch[0] = '\0';
                xTaskCreatePinnedToCore(apiSpeechTask, "api_speak", 20480, job, 2, NULL, 1);
            }
            return;
        }
    }

    // 0b) Sistema sin IA (volumen, radio, hora, repeti, timer) + musica.
    // Sirve para voz y para disparar desde el celu fuera de casa (via Tailscale).
    {
        String sysSpoken, sysScreen, sysUrl, sysTitle;
        if (aiEngine.tryLocalSystemCommand(msg, sysSpoken, sysScreen, sysUrl, sysTitle)) {
            if (sysUrl.length() > 0) {
                if (musicBusy()) {
                    server->send(409, "application/json", "{\"success\":false,\"error\":\"Música en curso\"}");
                    return;
                }
            } else if (speechBusy()) {
                server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio en curso, reintentá en unos segundos\"}");
                return;
            }
            log_i("API /api/chat comando SISTEMA local: '%s'", sysSpoken.c_str());
            copitoMem.addTurn(msg, sysSpoken);
            JsonDocument resDoc;
            resDoc["success"] = true;
            resDoc["user_message"] = msg;
            resDoc["reply"] = sysSpoken;
            resDoc["screen_text"] = sysScreen;
            resDoc["tool_executed"] = "SYSTEM";
            resDoc["local"] = true;
            if (sysUrl.length() > 0) resDoc["plays_on"] = "esp32";
            String resJson;
            serializeJson(resDoc, resJson);
            server->send(200, "application/json", resJson);
            if (sysUrl.length() > 0) {
                ApiMusicJob *mjob = (ApiMusicJob *)malloc(sizeof(ApiMusicJob));
                if (mjob) {
                    String radioQ = String("RADIO|") + sysUrl + "|" + sysTitle;
                    strncpy(mjob->query, radioQ.c_str(), sizeof(mjob->query) - 1);
                    mjob->query[sizeof(mjob->query) - 1] = '\0';
                    xTaskCreatePinnedToCore(apiMusicTask, "api_music", 20480, mjob, 2, NULL, 1);
                }
                return;
            }
            ApiSpeechJob *job = (ApiSpeechJob *)malloc(sizeof(ApiSpeechJob));
            if (job) {
                strncpy(job->text, sysSpoken.c_str(), sizeof(job->text) - 1);
                job->text[sizeof(job->text) - 1] = '\0';
                job->drawType = 0;
                job->sketch[0] = '\0';
                xTaskCreatePinnedToCore(apiSpeechTask, "api_speak", 20480, job, 2, NULL, 1);
            }
            return;
        }
    }

    // 0c) Musica sin IA: TODO suena EN Kraken (Pi3 desactivado como parlante).
    {
        String musicSpoken, musicScreen, esp32Query;
        if (aiEngine.tryLocalMusicCommand(msg, musicSpoken, musicScreen, esp32Query)) {
            if (esp32Query.length() > 0) {
                if (musicBusy()) {
                    server->send(409, "application/json", "{\"success\":false,\"error\":\"Música en curso\"}");
                    return;
                }
            } else if (speechBusy()) {
                server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio en curso, reintentá en unos segundos\"}");
                return;
            }
            log_i("API /api/chat comando MUSICA local: '%s'", musicSpoken.c_str());
            copitoMem.addTurn(msg, musicSpoken);
            JsonDocument resDoc;
            resDoc["success"] = true;
            resDoc["user_message"] = msg;
            resDoc["reply"] = musicSpoken;
            resDoc["screen_text"] = musicScreen;
            resDoc["tool_executed"] = "MUSIC";
            resDoc["local"] = true;
            if (esp32Query.length() > 0) resDoc["plays_on"] = "esp32";
            else resDoc["plays_on"] = "pi3";
            String resJson;
            serializeJson(resDoc, resJson);
            server->send(200, "application/json", resJson);
            if (esp32Query.length() > 0) {
                // Stremear en Copito en tarea aparte (no bloquear el HTTP cuelgue).
                ApiMusicJob *mjob = (ApiMusicJob *)malloc(sizeof(ApiMusicJob));
                if (mjob) {
                    strncpy(mjob->query, esp32Query.c_str(), sizeof(mjob->query) - 1);
                    mjob->query[sizeof(mjob->query) - 1] = '\0';
                    xTaskCreatePinnedToCore(apiMusicTask, "api_music", 20480, mjob, 2, NULL, 1);
                }
                return;
            }
            ApiSpeechJob *job = (ApiSpeechJob *)malloc(sizeof(ApiSpeechJob));
            if (job) {
                strncpy(job->text, musicSpoken.c_str(), sizeof(job->text) - 1);
                job->text[sizeof(job->text) - 1] = '\0';
                job->drawType = 6; // icono MUSICA
                job->sketch[0] = '\0';
                xTaskCreatePinnedToCore(apiSpeechTask, "api_speak", 20480, job, 2, NULL, 1);
            }
            return;
        }
    }

    String aiAnswer;
    bool ok = aiEngine.queryTextCascade(msg, aiAnswer);
    if (!ok || aiAnswer.length() == 0) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Todos los proveedores de IA fallaron (ver /api/logs)\"}");
        return;
    }

    // Parse tools unificado (10 dibujos + FACE + REMEMBER + TEXT + TOOL + SKETCH)
    CopitoTags tags;
    CopitoTools::parseTags(aiAnswer, tags);

    // Si no vino sketch explícito pero hay intención de dibujo, generar fallback procedural
    if (tags.sketchCommands.length() == 0) {
        if (CopitoTools::hasDrawingIntent(msg) || CopitoTools::hasDrawingIntent(aiAnswer)) {
            String combined = msg + " " + aiAnswer;
            CopitoTools::generateFallbackSketch(combined, tags.sketchCommands);
        }
    }

    int drawType = tags.drawType;
    String toolName = "none";
    if (tags.sketchCommands.length() > 0) toolName = "SKETCH";
    else if (drawType > 0) toolName = "DRAW";
    if (tags.hasFace) toolName = "FACE";
    if (tags.rememberFact.length() > 0) {
        copitoMem.saveFact(tags.rememberFact);
        toolName = "REMEMBER";
    }

    String cleanAnswer = aiAnswer; // parseTags ya limpio tags y resolvio tools
    cleanAnswer.trim();
    // Cache para "repeti"
    g_lastSpoken = cleanAnswer.length() > 300 ? cleanAnswer.substring(0, 300) : cleanAnswer;
    String screenText = (tags.showText.length() > 0) ? tags.showText : cleanAnswer;

    JsonDocument resDoc;
    resDoc["success"] = true;
    resDoc["user_message"] = msg;
    resDoc["reply"] = cleanAnswer;
    resDoc["screen_text"] = screenText;
    resDoc["tool_executed"] = toolName;
    if (tags.sketchCommands.length() > 0) {
        resDoc["sketch"] = tags.sketchCommands;
    }

    String resJson;
    serializeJson(resDoc, resJson);
    if (speechBusy()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio en curso, reintentá en unos segundos\"}");
        return;
    }
    server->send(200, "application/json", resJson);

    // Memoria corta para proximo contexto
    {
        String asShort = cleanAnswer;
        if (asShort.length() > 240) asShort = asShort.substring(0, 240);
        String usShort = msg;
        if (usShort.length() > 150) usShort = usShort.substring(0, 150);
        copitoMem.addTurn(usShort, asShort);
    }

    // Launch speech and e-Paper drawing asynchronously in dedicated FreeRTOS task
    ApiSpeechJob *job = (ApiSpeechJob *)malloc(sizeof(ApiSpeechJob));
    if (job) {
        strncpy(job->text, screenText.c_str(), sizeof(job->text) - 1);
        job->text[sizeof(job->text) - 1] = '\0';
        job->drawType = drawType;
        if (tags.sketchCommands.length() > 0) {
            strncpy(job->sketch, tags.sketchCommands.c_str(), sizeof(job->sketch) - 1);
            job->sketch[sizeof(job->sketch) - 1] = '\0';
        } else {
            job->sketch[0] = '\0';
        }
        xTaskCreatePinnedToCore(apiSpeechTask, "api_speak", 20480, job, 2, NULL, 1);
    }
}

void NetworkManager::handleApiSay() {
    String text = "";
    int drawType = 0;
    if (server->hasArg("text")) {
        text = server->arg("text");
        if (server->hasArg("draw")) {
            String d = server->arg("draw");
            drawType = CopitoTools::drawNameToType(d);
        }
    } else if (server->hasArg("plain")) {
        JsonDocument doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            text = doc["text"] | "";
            drawType = doc["draw"] | 0;
        }
    }

    if (text.length() == 0) {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Falta texto\"}");
        return;
    }
    if (speechBusy()) {
        server->send(409, "application/json", "{\"success\":false,\"error\":\"Audio en curso, reintentá en unos segundos\"}");
        return;
    }

    // Respond HTTP 200 immediately to client so connection never times out
    server->send(200, "application/json", "{\"success\":true}");

    // Launch speech and e-Paper drawing asynchronously in dedicated FreeRTOS task
    ApiSpeechJob *sayJob = (ApiSpeechJob *)malloc(sizeof(ApiSpeechJob));
    if (sayJob) {
        strncpy(sayJob->text, text.c_str(), sizeof(sayJob->text) - 1);
        sayJob->text[sizeof(sayJob->text) - 1] = '\0';
        sayJob->drawType = drawType;
        sayJob->sketch[0] = '\0';
        xTaskCreatePinnedToCore(apiSpeechTask, "api_say", 20480, sayJob, 2, NULL, 1);
    }
}

// Colector para vv_split_phrases en el self-test.
struct SelftestPhrases {
    size_t count = 0;
    size_t maxLen = 0;
};
static void selftestPhraseCb(const char *start, size_t len, void *ctx) {
    (void)start;
    SelftestPhrases *s = (SelftestPhrases *)ctx;
    s->count++;
    if (len > s->maxLen) s->maxLen = len;
}

// GET /api/audio_probe?mode=last|synth — sonda de audio para QA remoto sin oído.
//   mode=last: lo que REALMENTE fue al DAC en el último playback (picos, RMS,
//     muestras limitadas/recortadas). Flujo del agente: POST /api/say -> espera
//     -> GET /api/audio_probe?mode=last -> veredicto con números.
//   mode=synth: seno 440Hz 1s por la matemática REAL (dsp_audio.h), sin I2S ni
//     parlante. Test de regresión del incidente "todo saturado".
void NetworkManager::handleApiAudioProbe() {
    String mode = server->hasArg("mode") ? server->arg("mode") : "last";
    JsonDocument doc;
    if (mode == "synth") {
        int freq = server->hasArg("freq") ? server->arg("freq").toInt() : 440;
        int ms = server->hasArg("ms") ? server->arg("ms").toInt() : 1000;
        int gain = server->hasArg("gain") ? server->arg("gain").toInt() : 256;
        if (freq < 100) freq = 100;
        if (freq > 4000) freq = 4000;
        if (ms < 100) ms = 100;
        if (ms > 2000) ms = 2000;
        if (gain < 32) gain = 32;
        if (gain > 256) gain = 256;
        const uint32_t rate = 16000;
        uint32_t n = (rate * (uint32_t)ms) / 1000;
        int32_t peakPre = 0, peakPost = 0;
        uint32_t limited = 0, clipped = 0;
        int64_t sumSq = 0;
        for (uint32_t i = 0; i < n; i++) {
            float ph = 2.0f * 3.14159265f * (float)freq * (float)i / (float)rate;
            int16_t s = (int16_t)(12000.0f * sinf(ph)); // -8.7 dBFS
            bool lim = false, clip = false;
            int32_t v = dsp_apply_sample_ex(s, gain, &lim, &clip);
            int32_t pre = s < 0 ? -(int32_t)s : (int32_t)s;
            int32_t post = v < 0 ? -v : v;
            if (pre > peakPre) peakPre = pre;
            if (post > peakPost) peakPost = post;
            if (lim) limited++;
            if (clip) clipped++;
            sumSq += (int64_t)v * (int64_t)v;
        }
        double rms = n ? sqrt((double)sumSq / (double)n) : 0.0;
        doc["mode"] = "synth";
        doc["freq"] = freq;
        doc["ms"] = ms;
        doc["gain_req"] = gain;
        doc["gain_eff"] = (gain * DSP_TRIM_NUM) / DSP_TRIM_DEN;
        doc["samples"] = n;
        doc["peak_pre"] = peakPre;
        doc["peak_post"] = peakPost;
        doc["rms"] = (int)rms;
        doc["limited"] = limited;
        doc["clipped"] = clipped;
        doc["verdict"] = clipped > 0 ? "CLIPPING" : (limited > 0 ? "LIMITING" : "OK");
    } else {
        AudioProbe p;
        audio.probeSnapshot(p);
        double rms = p.samples ? sqrt((double)p.sumSq / (double)p.samples) : 0.0;
        int32_t dc = p.samples ? (int32_t)(p.sum / (int64_t)p.samples) : 0;
        doc["mode"] = "last";
        doc["samples"] = p.samples;
        doc["peak_pre"] = p.peakPre;
        doc["peak_post"] = p.peakPost;
        doc["rms"] = (int)rms;
        doc["dc_offset"] = dc;
        doc["limited"] = p.limited;
        doc["clipped"] = p.clipped;
        doc["volume"] = config.volume;
        const char *v = "OK";
        if (p.clipped > 0) v = "CLIPPING";
        else if (p.samples > 0 && p.peakPre > DSP_KNEE) v = "HOT_SOURCE";
        else if (p.samples > 2400 && rms < 200) v = "SILENCE?";
        else if (p.samples == 0) v = "NO_DATA";
        doc["verdict"] = v;
        doc["hint"] = "limited>0&&clipped==0=compresion sana; clipped>0=fuente full-scale; rms<200=silencio; |dc|>500=header roto";
    }
    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

// GET /api/selftest — auditoría interna del pipeline voz/música/display.
// Corre las mismas funciones puras que los unit tests (voice_validate) con
// fixtures enlatados + sonda rápida al Pi3 + stats de voz. No reproduce
// audio ni toca la pantalla: es seguro correrlo en casa o en la calle.
void NetworkManager::handleApiSelftest() {
    JsonDocument doc;
    doc["heap"] = ESP.getFreeHeap();
    doc["psram"] = ESP.getFreePsram();

    // 1. Gate MP3: válido con ID3+sync vs JSON de error vs HTML vs basura.
    static const uint8_t mp3ok[] = {'I','D','3',0x04,0x00,0,0,0,0,0,0xFF,0xFB,0x90,0x00,0x11,0x22};
    static const uint8_t badjson[] = {'{','"','e','r','r','o','r','"',':','4','2','9','}'};
    static const uint8_t badhtml[] = {'<','h','t','m','l','>','e','r','r','o','r'};
    static const uint8_t badbin[] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77};
    vv_mp3_t m1 = vv_mp3_scan(mp3ok, sizeof(mp3ok));
    vv_mp3_t m2 = vv_mp3_scan(badjson, sizeof(badjson));
    vv_mp3_t m3 = vv_mp3_scan(badhtml, sizeof(badhtml));
    vv_mp3_t m4 = vv_mp3_scan(badbin, sizeof(badbin));
    doc["mp3_valid_ok"] = (m1.kind == VV_MP3_OK);
    doc["mp3_rejects_json"] = (m2.kind == VV_MP3_JSON_ERR);
    doc["mp3_rejects_html"] = (m3.kind == VV_MP3_HTML_ERR);
    doc["mp3_rejects_bin"] = (m4.kind == VV_MP3_NO_SYNC);

    // 2. Parser WAV: cabecera válida 22050/mono/16 (con pico) vs sin fmt
    // vs data declarado mayor que el presente (truncado de hotspot).
    uint8_t wav[44 + 128];
    memset(wav, 0, sizeof(wav));
    memcpy(wav, "RIFF", 4);
    wav[4] = 36 + 128; // riff size (LE, simplificado)
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    wav[16] = 16;
    wav[20] = 1; wav[22] = 1; // PCM mono
    wav[24] = 0x22; wav[25] = 0x56; // 22050 LE (0x5622)
    wav[28] = 0x44; wav[29] = 0xAC; // byte rate 44100
    wav[32] = 2; wav[34] = 16;      // block align 2, 16 bits
    memcpy(wav + 36, "data", 4);
    wav[40] = 128; // data size
    wav[44] = 0x11; wav[45] = 0x22; // pico 0x2211 = 8721
    vv_wav_t w1 = vv_wav_parse(wav, sizeof(wav), 1);
    uint8_t wavNoFmt[24];
    memset(wavNoFmt, 0, sizeof(wavNoFmt));
    memcpy(wavNoFmt, "RIFF", 4);
    memcpy(wavNoFmt + 8, "WAVE", 4);
    memcpy(wavNoFmt + 12, "JUNK", 4);
    vv_wav_t w2 = vv_wav_parse(wavNoFmt, sizeof(wavNoFmt), 0);
    uint8_t wavTrunc[60];
    memcpy(wavTrunc, wav, sizeof(wavTrunc));
    wavTrunc[40] = 0x10; wavTrunc[41] = 0x27; // declara 10000, hay ~16
    vv_wav_t w3 = vv_wav_parse(wavTrunc, sizeof(wavTrunc), 0);
    doc["wav_valid_ok"] = (w1.ok == 1 && w1.rate == 22050 && w1.channels == 1 && w1.peak == 8721);
    doc["wav_rejects_nofmt"] = (w2.ok == 0);
    doc["wav_rejects_trunc"] = (w3.ok == 0);

    // 3. Splitter de frases TTS: 500 chars -> varias frases, ninguna >155.
    static const char *longText =
        "Hola. Esto es una prueba del divisor de frases del Kraken, que tiene que cortar en puntos, "
        "comas y espacios sin romper palabras a la mitad nunca jamas. Segunda oracion: el hotspot de la "
        "calle mete jitter y cada frase viaja en su propio TLS, asi que si una se pierde las demas igual "
        "suenan. Tercera: ¿funciona con preguntas? ¡Y con exclamaciones! Fin del texto largo de prueba.";
    SelftestPhrases ph;
    vv_split_phrases(longText, selftestPhraseCb, &ph);
    doc["split_count"] = (unsigned)ph.count;
    doc["split_maxlen_ok"] = (ph.count > 2 && ph.maxLen <= 155);

    // 4. Rutas LAN vs funnel + prebuffer + retry.
    vv_route_t rLan = vv_select_route(0, 1, 0, 0);
    vv_route_t rStreet = vv_select_route(0, 1, 1, 1);
    vv_retry_t rp429 = vv_retry_plan(0, 429, 0);
    vv_retry_t rp404 = vv_retry_plan(0, 404, 0);
    vv_retry_t rpCap = vv_retry_plan(2, 503, 0);
    doc["route_lan_ok"] = (rLan.rate == 22050 && rLan.pre_sec == 2 && rLan.use_lan_first == 1);
    doc["route_street_ok"] = (rStreet.rate == 16000 && rStreet.pre_sec == 4 && rStreet.use_lan_first == 0);
    doc["prebuffer_lan"] = (unsigned)vv_prebuffer_bytes(22050, 0);
    doc["prebuffer_street"] = (unsigned)vv_prebuffer_bytes(16000, 1);
    doc["retry_429_ok"] = (rp429.retry == 1 && rp429.backoff_ms == 500);
    doc["retry_404_ok"] = (rp404.retry == 0);
    doc["retry_cap_ok"] = (rpCap.retry == 0);

    // 5. VAD simulado: casa (silencio+habla+silencio) vs silencio total.
    // (Documenta el límite conocido: con umbral fijo 300 el ruido de calle
    //  fuerte también dispara; ver /api/selftest vad_street_detected.)
    uint16_t eCasa[36];
    for (int i = 0; i < 10; i++) eCasa[i] = 50;
    for (int i = 10; i < 16; i++) eCasa[i] = 4000;
    for (int i = 16; i < 36; i++) eCasa[i] = 100;
    vv_vad_t vCasa = vv_vad_decide(eCasa, 36, 300, 3, 10);
    uint16_t eSil[20];
    for (int i = 0; i < 20; i++) eSil[i] = 100;
    vv_vad_t vSil = vv_vad_decide(eSil, 20, 300, 3, 10);
    uint16_t eStreet[20];
    for (int i = 0; i < 20; i++) eStreet[i] = 900;
    vv_vad_t vStreet = vv_vad_decide(eStreet, 20, 300, 3, 10);
    doc["vad_home_ok"] = (vCasa.detected == 1 && vCasa.cut_idx == 26);
    doc["vad_silence_ok"] = (vSil.detected == 0);
    doc["vad_street_detected"] = (vStreet.detected == 1); // límite conocido

    // 6. Sonda rápida al Pi3 (LAN, 2.5 s): ¿hay TTS local disponible?
    bool pi3 = false;
    if (config.music_host[0] != '\0') {
        WiFiClient probe;
        probe.setTimeout(2);
        unsigned long t0 = millis();
        uint16_t pport = config.music_port ? config.music_port : 5000;
        if (probe.connect(config.music_host, pport)) {
            probe.println("GET /api/status HTTP/1.1");
            probe.println(String("Host: ") + config.music_host);
            probe.println("Connection: close");
            probe.println();
            while (!probe.available() && probe.connected() && (millis() - t0 < 2500)) delay(5);
            String ln = probe.readStringUntil('\n');
            pi3 = (ln.indexOf("200") >= 0);
        }
        probe.stop();
    }
    doc["pi3_local_ok"] = pi3;

    // 7. Stats acumuladas del pipeline de voz.
    uint32_t vok = 0, vfail = 0;
    char verr[96];
    aiVoiceStats(vok, vfail, verr, sizeof(verr));
    doc["voice_ok"] = vok;
    doc["voice_fail"] = vfail;
    doc["voice_last_err"] = verr;

    // 8. Diagnóstico de boot: reboots por BROWNOUT/PANIC/WDT explican los
    // "se apagó solo / se tildó". boot_healthy=false => mirar alimentación.
    doc["boot_count"] = g_bootCount;
    doc["reset_reason"] = g_resetReason;
    {
        String rr = String(g_resetReason);
        doc["boot_healthy"] = (rr == "POWERON" || rr == "SW" || rr == "DEEPSLEEP");
    }

    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

void NetworkManager::handleApiSensor() {
    float temp = envSensor.getTemperature();
    float hum = envSensor.getHumidity();

    JsonDocument doc;
    doc["temperature_c"] = round(temp * 10.0f) / 10.0f;
    doc["humidity_pct"] = round(hum * 10.0f) / 10.0f;
    doc["sensor_connected"] = envSensor.isConnected();
    doc["uptime_s"] = millis() / 1000;
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["usb_data"] = copitoUsbDataConnected(); // cable de datos a PC OK
    doc["charging"] = batteryMonitor.getStatus().isCharging; // 5V presente

    String out;
    serializeJson(doc, out);
    server->send(200, "application/json", out);
}

void NetworkManager::handleApiAction() {
    String action = server->arg("action");
    if (action.length() == 0 && server->hasArg("plain")) {
        JsonDocument doc;
        if (deserializeJson(doc, server->arg("plain")) == DeserializationError::Ok) {
            action = doc["action"] | "";
        }
    }

    action.toLowerCase();
    if (action == "happy") {
        epd.showFace(FACE_HAPPY, 0, true);
        audio.playSuccessSound();
    } else if (action == "idle") {
        g_textHold = false; // "sacar foto"/volver a 0: libera pantalla fija
        epd.showFace(FACE_IDLE, 0, true);
    } else if (action == "sleep") {
        epd.showFace(FACE_SLEEP, 0, true);
        audio.playSleepSound();
    } else if (action == "chime") {
        audio.playChimeUp();
    } else if (action == "reloj" || action == "clock") {
        server->send(200, "application/json", "{\"success\":true,\"mode\":\"reloj\"}");
        audio.playChimeUp();
        changeState(STATE_CLOCK);
        return;
    } else if (action == "clima" || action == "weather") {
        server->send(200, "application/json", "{\"success\":true,\"mode\":\"clima\"}");
        audio.playChimeUp();
        changeState(STATE_WEATHER);
        return;
    } else {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Accion desconocida (happy, idle, sleep, chime, reloj, clima)\"}");
        return;
    }

    server->send(200, "application/json", "{\"success\":true}");
}

void NetworkManager::handleApiBitmap() {
    uint8_t *bmpBuf = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
    if (!bmpBuf) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Sin memoria para bitmap\"}");
        return;
    }
    bool ok = false;

    if (server->hasArg("plain")) {
        String body = server->arg("plain");
        if (body.length() == EPD_BUFFER_SIZE) {
            // 1. Raw binary directo de 5000 bytes
            memcpy(bmpBuf, body.c_str(), EPD_BUFFER_SIZE);
            ok = true;
        } else if (body.startsWith("{")) {
            // 2. JSON con payload
            JsonDocument doc;
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                if (doc["bitmap_b64"].is<const char*>()) {
                    const char *b64 = doc["bitmap_b64"];
                    size_t outLen = 0;
                    int b64len = strlen(b64);
                    int val = 0, valb = -8;
                    for (int i = 0; i < b64len && outLen < EPD_BUFFER_SIZE; i++) {
                        char c = b64[i];
                        int v = -1;
                        if (c >= 'A' && c <= 'Z') v = c - 'A';
                        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
                        else if (c >= '0' && c <= '9') v = c - '0' + 52;
                        else if (c == '+') v = 62;
                        else if (c == '/') v = 63;
                        if (v >= 0) {
                            val = (val << 6) | v;
                            valb += 6;
                            if (valb >= 0) {
                                bmpBuf[outLen++] = (uint8_t)((val >> valb) & 0xFF);
                                valb -= 8;
                            }
                        }
                    }
                    if (outLen == EPD_BUFFER_SIZE) ok = true;
                } else if (doc["bitmap_hex"].is<const char*>()) {
                    const char *hex = doc["bitmap_hex"];
                    if (strlen(hex) >= EPD_BUFFER_SIZE * 2) {
                        for (size_t i = 0; i < EPD_BUFFER_SIZE; i++) {
                            char byteStr[3] = { hex[i*2], hex[i*2 + 1], '\0' };
                            bmpBuf[i] = (uint8_t)strtoul(byteStr, NULL, 16);
                        }
                        ok = true;
                    }
                }
            }
        }
    }

    if (!ok) {
        free(bmpBuf);
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Formato de bitmap inválido. Se requieren 5000 bytes raw o JSON con 'bitmap_b64' o 'bitmap_hex'.\"}");
        return;
    }

    bool forceFull = true;
    if (server->hasArg("full")) {
        forceFull = (server->arg("full") == "1" || server->arg("full") == "true");
    }

    g_textHold = true;
    epd.showBitmap(bmpBuf, forceFull);
    free(bmpBuf);
    log_i("API /api/bitmap: 5000 bytes renderizados en e-Paper (refresh=%s)", forceFull ? "FULL" : "PARTIAL");

    server->send(200, "application/json", "{\"success\":true,\"bytes\":5000}");
}

// Foto del framebuffer para emulación en PC (GET /api/screen): devuelve el
// 1-bit 200x200 en hex (bit=1 blanco, MSB primero, fila mayor). Lo usa
// tools/screen_cap.py para guardar PNGs en screens/ (agente UI).
void NetworkManager::handleApiScreen() {
    static uint8_t *snap = nullptr;
    if (!snap) {
        snap = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
    }
    if (!snap || !epd.snapshot(snap)) {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Sin framebuffer\"}");
        return;
    }
    static const char *H = "0123456789ABCDEF";
    String hx;
    hx.reserve(EPD_BUFFER_SIZE * 2 + 64);
    hx += "{\"success\":true,\"w\":200,\"h\":200,\"bits_hex\":\"";
    for (size_t i = 0; i < EPD_BUFFER_SIZE; i++) {
        hx += H[snap[i] >> 4];
        hx += H[snap[i] & 15];
    }
    hx += "\"}";
    server->send(200, "application/json", hx);
}


