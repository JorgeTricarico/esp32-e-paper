# ⭐ PLAN PRIORIZADO: lo que falta (orden: se nota mucho / gasta poco)

> Nuevo criterio: primero lo que **más se ve y más se siente**, sin gastar más batería ni hacerlo más lento.
> La pantalla e-paper gasta casi 0 (retiene imagen). El micrófono siempre-escuchando gasta muchísimo. Todo lo que impida deep-sleep va último.

## 🧭 Filosofía del proyecto (acordada)

1. **Menos es más.** Copito hace 3 cosas genial (habla, muestra, recuerda) en vez de 15 a medias.
2. **Si hay que explicar cómo se usa, no va en la placa.** Lo complejo va a la voz ("modo reloj") o a la web, nunca a combinaciones de botones imposibles.
3. **Regla HTML-first:** todo lo nuevo DEBE tener su control en el portal web. Nada se configura solo por IA.
4. **Regla offline-first:** decidir en la placa antes de llamar a la nube. Llamadas ahorradas > llamadas baratas (ya usamos Flash-Lite; un "pre-modelo" sumaría una llamada, no restaría).
5. **Regla 1 gesto + 1 pantalla:** si una función no entra ahí, se recorta.

Verificado en código el 2026-09-06 (2 sesiones en paralelo).

---

## ✅ Ya hecho (no repetir)

- [x] 10 dibujos por voz + `[FACE:...]` + `[TEXT:...]` + `[REMEMBER:...]` + tools sensores reales
- [x] Memoria corta (5 charlas) + larga NVS + **ver/borrar desde la web** (`/api/memory`, `/api/memory_clear`)
- [x] FacePacks / skins (otra sesión: RUDO, CLASICO, FEMENINO, GEEK)
- [x] Pantalla fija hasta hablar de nuevo o dormir (`g_textHold`, sin pisar durante TTS)
- [x] Guardias offline-first: silencio = aviso local sin API, sin WiFi = tarjeta local, Gemini caído = 3 respuestas rotativas
- [x] Ruta texto portal: sensores/batería en local antes de Gemini + parse unificado
- [x] **Modos:** `[MODE:RELOJ|CUENTO|CLIMA|DORMIDO]` por voz + botones Reloj/Clima/Dormir en la web (`/api/action`) + hora NTP (UTC-3) al conectar
- [x] Idioma TTS (`tts_lang`) ahora sí se usa en Google Free TTS (antes estaba fijo en `es`)
- [x] **Voz local LAN primero** ✅ 2026-09-06 | Proveedor `TTS_LOCAL`: PC Kokoro `:8880` → Pi5 Piper `:8881` → nube → pantalla. WAV con header parseado (24k/22k/16k). Campos en portal (hosts/puertos/voz). Skip en ~2-3s si apagados.
- [x] **IP fija opcional ESP** (`esp_ip/esp_gw/esp_sn`, vacío=DHCP) + `WiFi.config()` antes de asociar.
- [x] **Visuales:** batería TopBar con margen derecho 8px (no se corta) + humo del Rudo en cintas disipadas (no más aros-burbuja). Flasheado y verificado en arranque.
- [x] **Método flash híbrido:** USB directo; si duerme → espera ventana check-in → `ota_hold 60` → OTA `/update` → verifica reboot. Script en `Temp/opencode/flash-hybrid.ps1`.

## 🔊 Arquitectura de voz (decidida 2026-09-06, probada por audio)

| Orden | Voz | Dónde | Costo |
|---|---|---|---|
| 1 | Davefx (masc.) / Daniela (fem. AR) — Piper local | PC `:8880` Kokoro (Dora/Alex) primero, Pi5 `:8881` Piper después | $0 |
| 2 | Elena / Tomás — Edge Microsoft | Nube, gratis sin key | $0 |
| 3 | Google Free → Gemini → pantalla | Nube | $0 / cuota |
| Banco | Frases frecuentes pre-generadas con Elena (offline total) | Placa (KBs) | $0 |

- **Servidores persistentes:** PC arranca solo al logon (carpeta Inicio); Pi5 por cron `@reboot`. Verificados vivos el 2026-09-06.
- **IPs hoy en DHCP** (PC `.164`, Pi5 `.169`, placa `.176`): reservar en router + `.180` fija sugerida en portal. ⚠️ Falta: regla firewall admin en PC (`netsh ... 8880`) y reservas en router.
- Edge nativo en ESP32 descartado por ahora (WSS+DRM frágil); va vía nube o futuro puente en Pi.

## 🥇 TIER S — Hacer primero (se nota mucho, costo ~0)

| Orden | Qué | De dónde | Nota simple |
|---|---|---|---|
| S0 | Pantalla pizarra pro: `[CODE:]` legible + QR chico | `04`, `02` | Hoy `{}` `[]` no se dibujan (fuente 5x7 solo hasta `Z`). QR solo links cortos en 200px. |
| S1 | Hora en TopBar + idle útil `22°C 61% 12:35` | `08`, `02` | NTP ya sincroniza; falta mostrarlo siempre arriba y en reposo. RTC PCF85063 después para hora sin WiFi. |
| S2 | Offline total por voz: hora/temp/batería sin internet | `08` | En texto ya funciona. En voz falta STT local (caro) → alternativa honesta: tarjeta "sin internet" + comandos básicos. |
| S3 | Multi-gesto BOOT: 2 clicks = repetir (voz + re-muestra texto), 3 = modo | `07A` | Ojo: 400ms de ventana suma latencia al click simple. La voz ("modo reloj") ya cubre modos sin ese costo. |
| S4 | mDNS `copito.local` (3 líneas) | `06`, `10` | Costo 0 en reposo. |
| S5 | Batería honesta: curva LiPo real + aviso <10% | `05` | Curva `4.2=100%, 3.85=50%, 3.6=15%, 3.3=0%` + promedio. |
| S6 | Personalidades: normal / conciso / cuentos (web + voz) | `09D` | Un selector en HTML + system_prompt por modo. |
| S7 | **Logs persistentes + consola web (`/api/logs`)** ✅ HECHO 2026-09-06 | `06` + incidente audio 2026-09-06 | Ring 200 en PSRAM + WARN/ERROR a `/logs.txt` con rotación 24KB + recarga al boot. Migración de logs de audio/voz/portal a `CLOG`. Endpoints `/api/logs` (n/level/src) + `/api/logs_clear` + visor en el portal con auto-refresh y descarga. OTA por WiFi operativo (`POST /update`). |
| S8 | **Guardia autónoma: check-in periódico + hold OTA** ✅ HECHO 2026-09-06 | trabajo autónomo sin USB | La placa despierta sola con ventana WiFi silenciosa para flashes. **Backoff adaptativo**: tras flash/hold despierta cada 5 min; cada ciclo quiet duplica (5→10→20→30, tope configurable). `POST /api/ota_hold minutes=N` la deja despierta y sobrevive al reboot post-OTA. Controles en el portal + `/status` (`ota_hold_min/checkin_min/checkin_mode/checkin_next_min/checkin_quiet`). 10 tests espejo. Verificado por OTA: `checkin_next_min:5, checkin_quiet:0` tras flashear. |
| S9 | **Tools web: grounding google_search** ✅ desplegado 2026-09-06 (pendiente verificar con cuota) | tools IA | Campo `tools:[{google_search}]` en audio+texto + instrucción runtime + flag `web_search` con toggle en portal + hint de 400 en logs. Techo tokens 300→800 (el largo lo regula el prompt). Prompt reescrito adulto/autorregulado (pusheado por `/save_prompt`) + pools de contingencia 5+5 sobrios. Sin 400s hasta ahora; demo bloqueada por 429 de cuota. |
| 💡 NEXT | **Tools keyless: [TOOL:GET_DOLAR] (dolarapi.com) + [TOOL:GET_CLIMA_EXT] (Open-Meteo)** | tools propios | Ambas APIs gratis sin key, JSON chico, resolvers en `resolveToolTags` con caché de 5 min. |
| 💡 IDEA | **[MODE:MUSICA] ligado al servidor musicpi de la Raspberry** (pendiente: fortalecer ese server primero) | música local-first | Cuando la Raspberry esté sólida: endpoint en musicpi (cola/now-playing) + `[MODE:MUSICA]` en Copito que streamee MP3 de la LAN con corte en BOOT + `music_url` configurable en el portal. Nivel 1 offline (melodías por chimes) no requiere server. |
| 🐛 FIX | **Estática en TTS local (Piper full-scale + WiFi activo)** ✅ 2026-09-06 | audio | Causa real verificada contra el server (WAV 22050 válido, pico 32766): ganancia x1.5 aplastaba el limiter + streaming con RF sobre el PA. Fix: buffer-then-play en PSRAM (WiFi en silencio) + normalización por pico a 26000 + exigencia de chunk `data` + tamaño exacto. Log: `pico 32767, gain 203/256`. 7 tests regresión. |

## 🥈 TIER A — Segunda oleada (mejora fuerte, costo chico)

| Orden | Qué | De dónde | Nota simple |
|---|---|---|---|
| A1 | Más rápido: socket TLS reutilizado + medir tiempos | `01` | De ~3s a <1.8s. |
| A2 | Que suene mejor: filtro 150Hz + puerta de ruido | `03` | Solo números, sin hardware. |
| A3 | Portal: espejo de pantalla + logs + exportar config | `06` | Solo al abrir el navegador. |
| A4 | Function Calling JSON real | `04` | Tags actuales ya cubren; esto es prolijidad. |
| A5 | Transcripción para memoria (hoy usuario = "(voz)") | `09` | Pedir `transcript` a Gemini en el JSON. |
| A6 | `/notify` solo despierto | `10` | Sin despertar de deep-sleep. |

## 🐢 TIER C — Último / solo enchufado (rompe batería)

| Orden | Qué | De dónde | Por qué último |
|---|---|---|---|
| C1 | Wake word "Hey Copito" (WakeNet) | `07B` | Mic + CPU siempre on: semanas → horas. Solo USB. Alternativa: comandos locales MultiNet (vocabulario fijo, mismo costo energía). |
| C2 | MQTT / Home Assistant / Telegram + alertas solas | `10` | Requiere WiFi siempre on. |
| C3 | Fotos IA en pantalla | idea photoframe | Lento y pesado para 200x200 1-bit. |

---

## 🌐 Auditoría HTML (regla: todo lo nuevo toca la web)

**Queda (útil):** WiFi x2, Gemini key + modelo, TTS proveedor + key + idioma + velocidad, servidores locales (hosts/puertos/voz), IP fija placa, volumen, face_pack, prompt, tokens, temperatura, grabación/silencios, sleep, memoria (ver/borrar), modos, chat de prueba, sensor, acciones, OTA.

**Arreglado:** `tts_lang` no hacía nada → ahora sí. Lista DRAW decía 6 → ahora 10 + tags.

**Candidatos a sacar (no suman):** VoiceRSS como proveedor (casi nadie tiene key, ocupa un slot del menú), `elevenlabs_voice` visible aunque no uses ElevenLabs (mostrar solo si se elige), segundo WiFi si nunca se usa (dejarlo: es gratis y salva rescates).

**Falta a futuro:** selector de personalidad (S6), consola de logs persistentes (S7 — IMPORTANTE, va antes que A3), espejo de pantalla (A3), exportar/importar config (A3), hora visible del RTC (S1).

---

## 🧭 Orden sugerido (3 sprints)

1. **Sprint 1:** S1 + S5 → hora y batería honestas siempre visibles.
2. **Sprint 2:** S3 + S4 + S6 → botón útil, nombre fácil, personalidades.
3. **Sprint 3:** A1 + A2 → rápido y suena lindo. Después A3–A6.

## Prompt de arranque para la próxima sesión

```text
Retomamos Copito AI. Lee docs/roadmap/00_PLAN_PRIORIZADO.md primero (filosofía + regla HTML-first).
Hoy hacemos Sprint 1 (S1+S5): hora NTP en TopBar + idle con temp/hum/hora + curva LiPo real.
Reglas: todo lo nuevo toca el portal web; no romper g_textHold, copito_tools ni FacePacks (otra sesión).
```
