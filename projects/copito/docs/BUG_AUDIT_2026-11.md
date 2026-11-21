# Auditoría de Bugs — Firmware Copito / Kraken (ESP32-S3)

> Auditoría estática realizada sobre `projects/copito/src/` e `include/`.

---

## 1. Concurrencia y FreeRTOS

### 1.1. Acceso concurrente sin mutex a `AIEngine::client` (`WiFiClientSecure`) — **Crítico**
- **Archivos:** `include/ai_engine.h:135`, `src/ai_engine.cpp:423-617, 651-737, 3416-3500, 3511-3570, 3581-3670`, `src/wifi_portal.cpp:2848`
- **Símbolo:** `AIEngine::client` (`queryGeminiAudio`, `queryGeminiText`, `streamElevenLabs`, `streamOpenAiTts`, `streamVoiceRss`)
- **Causa raíz:** `client` es un miembro único compartido del singleton `aiEngine` y puede ser usado simultáneamente por `voiceTask`, `loopTask` (`handleApiChat`), `apiSpeechTask`, `apiStoryTask`, `echoSpeakTask` y `localTimerTask`. Dos tareas operando sobre el mismo `WiFiClientSecure` corrompen el estado TLS y provocan panics o bloqueos.
- **Fix sugerido:** Eliminar `client` como miembro de clase y usar instancias locales `WiFiClientSecure` en cada método (como ya hacen `streamGeminiTts` y `chatOpenAI`), o protegerlo con un mutex dedicado y serializar el acceso al audio/TTS con un `SemaphoreHandle_t`.

### 1.2. Acceso concurrente sin mutex al bus I2C (`Wire`) — **Alto**
- **Archivos:** `src/main.cpp:314, 392, 512`, `src/audio_es8311.cpp:27-43, 206-218, 438-442`, `src/sensor_shtc3.cpp:28-102`, `src/rtc_pcf85063.cpp:37-97`
- **Símbolo:** `Wire` (`AudioEngine::writeReg`/`readReg`, `SHTC3Sensor::readSample`, `PCF85063Driver::readTime`)
- **Causa raíz:** `Wire` es compartido entre ES8311, SHTC3 (`loop()` cada 20s con `delay(15)` en medio de la transacción) y PCF85063 (`loop()` y `voiceTask`) sin mutex I2C.
- **Fix sugerido:** Crear un mutex global (`SemaphoreHandle_t g_i2cMutex`) y envolver toda transacción I2C con `xSemaphoreTake/Give`.

### 1.3. Race condition en `CopitoLog::write()` y acceso concurrente a `LittleFS` — **Alto**
- **Archivos:** `src/copito_log.cpp:57-113, 124-213`
- **Símbolo:** `CopitoLog::write`, `CopitoLog::persist`, `CopitoLog::toJson`, `CopitoLog::clear`
- **Causa raíz:** `CLOGI`/`CLOGW`/`CLOGE` se invocan desde `loopTask` (Core 1), `voiceTask` (Core 1), `epdBackgroundTask` (Core 0), `storyPrefetchTask` y `musicArtworkTask` modificando `head`, `stored`, `ring[head]` y `/logs.txt` en `LittleFS` sin mutex.
- **Fix sugerido:** Proteger el ring buffer y todas las operaciones de `LittleFS` con un mutex FreeRTOS.

### 1.4. Acceso concurrente a objetos `String` globales (`g_echoPending`, `g_musicEsp32Title`, `g_lastSpoken`) — **Alto**
- **Archivos:** `src/main.cpp:63, 75, 836-840, 982, 997, 1055`, `src/ai_engine.cpp:71, 73, 1237, 1268, 1343, 1390, 1418, 1473, 4626`, `src/wifi_portal.cpp:2879`
- **Causa raíz:** `Arduino String` no es thread-safe. Reasignaciones concurrentes desde distintas tareas pueden liberar el buffer interno mientras otra tarea lo lee (Use-After-Free / corrupción de heap).
- **Fix sugerido:** Reemplazar por buffers `char[]` fijos protegidos con mutex o spinlock `portMUX_TYPE`.

### 1.5. `g_speaking` se apaga entre bloques de TTS multi-bloque — **Medio**
- **Archivos:** `src/ai_engine.cpp:3346, 3358, 3375-3403`
- **Símbolo:** `AIEngine::streamTtsSingle`, `AIEngine::streamTtsToSpeaker`, `g_speaking`
- **Causa raíz:** `streamTtsSingle()` ejecuta `g_speaking = false;` al terminar cada bloque, dejando `g_speaking == false` entre bloques y durante el `delay(800)` intermedio, lo que permite que `checkButtonEvents()` dispare una nueva grabación sobre el TTS.
- **Fix sugerido:** Quitar `g_speaking = false;` de `streamTtsSingle()` y gestionarlo únicamente en `streamTtsToSpeaker()`.

### 1.6. Race conditions en `BatteryMonitor`, `CopitoMemory`, `localTimerTask` y flags de estado — **Medio**
- **Archivos:** `src/battery_monitor.cpp:24-27, 71-91`, `src/copito_memory.cpp:14-101`, `src/ai_engine.cpp:1766-1822`, `src/main.cpp:57, 60, 82, 969, 992`, `src/wifi_portal.cpp:2532-2535, 2580, 2625`
- **Causa raíz:**
  - `BatteryMonitor::readFilteredMilliVolts()` muta arrays estáticos sin mutex desde `loop()` y `voiceTask`.
  - `copitoMem` (`turns`, `factsCache`) se lee/escribe sin mutex entre múltiples tareas.
  - `localTimerTask` reproduce chimes y TTS sin verificar `voiceTaskRunning`/`speechBusy()`/`musicBusy()`.
  - `voiceTaskRunning`, `s_speechTaskH` y `s_musicTaskH` se asignan dentro de la tarea recién creada (ventana TOCTOU).
- **Fix sugerido:** Añadir mutex en `BatteryMonitor` y `CopitoMemory`; marcar `volatile` las banderas compartidas y asignar handles antes de crear tareas; sincronizar `localTimerTask` con mutex de audio.

### 1.7. Ventana sin mutex en `EPaperDriver::showClockMinutesWindowed` y `epd.sleep()` sin detener `epdBackgroundTask` — **Medio**
- **Archivos:** `src/epaper_driver.cpp:435-466, 865-869`, `src/main.cpp:599-609`
- **Causa raíz:** Se libera `s_epdMutex` en L464 antes de retomar en L466; además `enterSleepMode()` llama a `epd.sleep()` y corta `PIN_EPD_PWR` sin drenar/bloquear `epdBackgroundTask` (Core 0).
- **Fix sugerido:** Mantener `s_epdMutex` tomado durante toda `showClockMinutesWindowed` y drenar/bloquear `s_epdMutex` antes de `epd.sleep()`.

---

## 2. Memoria (Use-After-Free, Stack, OOB)

### 2.1. Use-After-Free en `NetworkManager::handleApiConfigRestore()` — **Crítico**
- **Archivos:** `src/wifi_portal.cpp:502-524`
- **Causa raíz:** `JsonDocument fileDoc;` se declara dentro del `if (from == "flash")`, se asigna `o = fileDoc.as<JsonObjectConst>();` y al cerrar el bloque `fileDoc` se destruye antes de llamar a `configFromJson(o)`.
- **Fix sugerido:** Declarar `JsonDocument fileDoc;` en el ámbito exterior de la función.

### 2.2. Dangling pointer / Use-After-Free en `TextPaginator::setText()` — **Crítico**
- **Archivos:** `src/text_paginator.cpp:135-142, 315`, `src/main.cpp:872-877, 1242, 1486-1489`, `src/wifi_portal.cpp:2589, 2650`
- **Símbolo:** `TextPaginator::textPtr`, `paginator`
- **Causa raíz:** `TextPaginator::setText()` almacena un puntero crudo (`textPtr = rawText`) a `.c_str()` de `String` locales o buffers liberados con `free(job)`. Al pasar de página con `BOOT` (`g_textHold`), `paginator.render()` lee memoria liberada.
- **Fix sugerido:** Hacer que `TextPaginator` almacene su propia copia (`String` o buffer en PSRAM).

### 2.3. Buffer de 5 KB en stack de `loopTask` en `NetworkManager::handleApiBitmap()` — **Alto**
- **Archivos:** `src/wifi_portal.cpp:3251-3252`
- **Causa raíz:** `uint8_t bmpBuf[EPD_BUFFER_SIZE]` (5000 bytes) en el stack de `loopTask` (8 KB por defecto), con riesgo de stack overflow.
- **Fix sugerido:** Alocar `bmpBuf` dinámicamente (`ps_malloc`/`malloc`) y liberarlo al salir.

### 2.4. Lectura fuera de límites en `dias[tm.tm_wday]` y `meses[tm.tm_mon]` — **Alto**
- **Archivos:** `src/main.cpp:399-402, 731-733`, `src/rtc_pcf85063.cpp:62-63`
- **Causa raíz:** `PCF85063Driver::readTime` puede devolver `tm_wday` en `0..7` y `tm_mon` fuera de `0..11` (p.ej. `-1` si `rawMon == 0`), indexando arrays de tamaño 7 y 12 sin validación.
- **Fix sugerido:** Validar rangos de fecha/hora en `readTime()` y acotar los índices en `main.cpp`.

---

## 3. Red / HTTP / TLS / JSON

### 3.1. `AIEngine::streamVoiceRss()` intenta TLS sobre el puerto 80 — **Alto**
- **Archivos:** `src/ai_engine.cpp:3580-3581`, `include/ai_engine.h:135`
- **Causa raíz:** `client` (`WiFiClientSecure`) llama a `client.connect("api.voicerss.org", 80)`, ejecutando handshake TLS contra puerto HTTP plano.
- **Fix sugerido:** Usar puerto `443` con `WiFiClientSecure` o un `WiFiClient` plano local para puerto `80`.

### 3.2. Bucles de lectura HTTP/chunked sin timeout global — **Alto**
- **Archivos:** `src/ai_engine.cpp:152-181, 286-330, 524-536, 716-728, 2299-2309, 2531-2541, 4666-4678, 4761-4791`
- **Símbolos:** `readHttpBody`, `readHttpBinaryBody`, `queryGeminiAudio`, `queryGeminiText`, `chatOpenAI`, `transcribeGroqWhisper`, `fetchAndDisplayCloudImage`
- **Causa raíz:** Bucles `while (cli.connected() || cli.available())` sin comprobación de timeout por `millis()`. Si el servidor deja el socket colgado, `voiceTask` queda bloqueada indefinidamente.
- **Fix sugerido:** Agregar timeout de inactividad y timeout total en todos los bucles de lectura HTTP/chunked.

### 3.3. `searchDuckDuckGo()` usa `std::string::npos` sin validar `hrefEnd` — **Medio**
- **Archivos:** `src/ai_engine.cpp:874-881`
- **Causa raíz:** `html.indexOf('"', href)` retorna `-1` si no encuentra la comilla, convirtiéndose en `size_t` grande y copiando el resto del HTML si está truncado.
- **Fix sugerido:** Usar `int hrefEnd = html.indexOf('"', href);` y validar `hrefEnd < 0`.

---

## 4. Audio / I2S / ES8311, E-Paper y Energía

### 4.1. Reconfiguración concurrente de `i2s_set_clk()` sobre `I2S_NUM_0` — **Alto**
- **Archivos:** `src/audio_es8311.cpp:226, 434, 615, 660, 674`
- **Causa raíz:** Múltiples tareas llaman a `playChimeSequence()` / `startPlayback()` / `i2s_write()` sin exclusión mutua sobre `I2S_NUM_0`.
- **Fix sugerido:** Proteger la propiedad de `I2S_NUM_0` con un mutex o verificar estado activo antes de reproducir chimes desde `loop()`.

### 4.2. `enterSleepMode()` no detiene tareas activas antes de cortar la alimentación — **Medio**
- **Archivos:** `src/main.cpp:586-633`
- **Causa raíz:** No espera ni detiene `epdBackgroundTask`, `voiceTask`, `apiSpeechTask`, `apiMusicTask` o `localTimerTask` antes de llamar `epd.sleep()` y apagar `PIN_MAIN_PWR`.
- **Fix sugerido:** Cancelar tareas activas, esperar su finalización y bloquear `s_epdMutex` antes de apagar rieles de energía.
