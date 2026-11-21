# 🎤 SESIÓN 07 — INTERACCIÓN, BOTONES Y WAKE WORD

**Dominio:** UX de Interacción — Botones, Modos y Activación por Voz  
**Prioridad:** 🔴 Alta — Impacto directo en la experiencia diaria de uso  
**Prerequisitos:** Firmware actual compilado y flasheado ✅

---

## 🎯 Objetivo de la Sesión

Hacer que Copito sea un asistente **verdaderamente manos-libres** y que el único
botón físico disponible (BOOT / GPIO 0) sea mucho más expresivo mediante gestos
de tiempo, sin agregar hardware adicional.

---

## 📦 Mejoras Planificadas

### A) Multi-gestos del botón BOOT (sin hardware extra)

El botón BOOT ya funciona como "iniciar grabación". Extender su comportamiento:

| Gesto | Acción |
|---|---|
| **1 click corto** | Iniciar / cortar grabación (igual que hoy) |
| **2 clicks rápidos** | Repetir última respuesta en voz alta |
| **3 clicks rápidos** | Cambiar modo de respuesta (normal → conciso → técnico) |
| **Long press 2s** | Entrar en reposo manual inmediato |
| **Long press 4s** | Portal de rescate (ya implementado) |

**Implementación:**
- En `checkButtonEvents()` en `main.cpp`, reemplazar la lógica de "un solo click" por una
  máquina de estados con ventana de tiempo `MULTI_CLICK_WINDOW_MS = 400ms`.
- Al soltar el botón, si pasaron >400ms sin nuevo click, disparar la acción correspondiente
  al conteo acumulado.
- La respuesta anterior se guarda en una variable `String lastAiResponse` en `processVoiceInteraction()`.

**Archivos afectados:**
- `src/main.cpp` — `checkButtonEvents()`, `processVoiceInteraction()`
- `include/config.h` — nueva constante `MULTI_CLICK_WINDOW_MS`

---

### B) Wake Word "Hey Copito" — Activación por voz

ESP32-S3 tiene la unidad de vectores ESP-NN que permite correr **WakeNet6/WakeNet9**
sin impacto notorio en el rendimiento. Detecta una frase de activación incluso con la
pantalla en reposo.

**Stack técnico requerido:**
- Librería: `esp-sr` (Espressif Speech Recognition)
- Modelo: `WakeNet9` con comando "Hi ESP" o frase custom entrenada
- Consumo extra en standby: ~2-3 mA adicionales
- Runs en Core 0 (el mismo que maneja el e-paper, sin interferir con el audio)

**Flujo de activación:**
```
[Silencio]  →  WakeNet escucha micrófono siempre
[Wake Word] →  LED parpadea, Copito dice "¿Sí?" (chime corto)
[Habla]     →  VAD detecta fin de frase → envía a Gemini
[Respuesta] →  TTS → parlante → vuelve a escuchar wake word
```

**Archivos a crear/modificar:**
- `src/wake_word.cpp` + `include/wake_word.h` — nueva clase `WakeWordEngine`
- `src/main.cpp` — `setup()` inicializa `wakeWord.begin()`, loop escucha callback
- `platformio.ini` — agregar dependencia `espressif/esp-sr`

**Nota crítica:** WakeNet requiere que el micrófono esté siempre encendido.
Evaluar si el consumo de energía es aceptable para el caso de uso de Jorge.

---

### C) Modo Push-to-Talk vs Toggle

Actualmente: 1 click activa, 2do click corta (toggle).  
Alternativa: **Push-to-Talk** — mantener presionado mientras se habla, soltar para enviar.

- Configurable desde el portal web: checkbox "Modo Push-to-Talk"
- Agrega campo `bool push_to_talk` en `AppConfig`

---

## ✅ Estado Actual del Hardware

```
GPIO 0  → BOOT button (único botón programable)
GPIO 3  → LED onboard (activo LOW, REQUIERE gpio_reset_pin() antes de usar)
GPIO 42 → Audio PA enable (NS4150B)
GPIO 46 → Audio Power Gate
```

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN

```
Retomamos el proyecto Copito AI (ESP32-S3-ePaper-1.54).
Lee primero el workflow en docs/WORKFLOW_AGENTE.md.
Hoy implementaremos la Sesión 07: Multi-gestos del botón BOOT y/o Wake Word.
El firmware actual está en src/main.cpp (checkButtonEvents línea ~220).
Empezamos por el multi-gesto: necesito 1 click = grabar, 2 clicks = repetir última respuesta, long press 2s = sleep.
```
