# 🌡️ SESIÓN 08 — SENSORES LOCALES Y RESPUESTAS SIN INTERNET

**Dominio:** Sensores físicos on-board + Autonomía offline  
**Prioridad:** 🟡 Media-Alta — Hardware ya disponible, sin costo adicional  
**Prerequisitos:** Firmware actual compilado y flasheado ✅

---

## 🎯 Objetivo de la Sesión

Aprovechar el **sensor SHTC3 de temperatura y humedad** que ya viene soldado
en la placa (I2C @ 0x70) para que Copito pueda responder preguntas ambientales
sin necesidad de internet, y mostrar estos datos en el display en reposo.

---

## 📦 Mejoras Planificadas

### A) Sensor SHTC3 — Temperatura y Humedad en tiempo real

El SHTC3 ya está montado en el bus I2C (SDA=47, SCL=48, addr=0x70).
Solo falta leerlo.

**Librería:** `Sensirion/arduino-shtc3` (disponible en PlatformIO registry)

**Integración:**
- Leer temperatura y humedad en `loop()` cada 30 segundos, guardar en variables globales.
- Si Gemini devuelve texto con intención de pregunta ambiental (palabras "temperatura",
  "calor", "frío", "humedad"), Copito responde directamente desde el sensor SIN llamar a Gemini.
- Ejemplo: "Copito, ¿qué temperatura hay?" → "Ahora mismo hay 22.3 grados y 61% de humedad."

**Archivos afectados:**
- `src/main.cpp` — leer sensor en loop, `processVoiceInteraction()` agregar detección
- `platformio.ini` — agregar `Sensirion/arduino-shtc3`
- `include/config.h` — constante `SHTC3_I2C_ADDR 0x70`

**Display en idle:**  
Mientras Copito está en reposo, mostrar en e-paper:
```
┌──────────────┐
│   (ᵔ ᵕ ᵔ)   │
│  22.3°C  61% │
│  12:35 PM    │
└──────────────┘
```

---

### B) Reloj en tiempo real (NTP → RTC PCF85063)

El RTC **PCF85063** ya está en I2C @ 0x51. Ya se usa para deep-sleep.
Extender su uso para mostrar la hora en el display idle.

**Flujo:**
- Al conectar WiFi → sincronizar NTP → escribir en RTC PCF85063
- En idle → leer RTC → mostrar en e-paper junto con temperatura SHTC3
- Sin WiFi → el RTC mantiene la hora con su batería de respaldo

**Librería necesaria:** `elpaso-tim/PCF85063A` (verificar compatibilidad)

---

### C) Respuestas offline para preguntas frecuentes

Sin internet, Copito podría responder preguntas hardcodeadas:
- "¿Qué hora es?" → Lee el RTC
- "¿Qué temperatura hace?" → Lee SHTC3
- "¿Cuánta batería tenés?" → Lee ADC del raíl de batería

**Implementación:** tabla de intenciones simple por palabras clave en `ai_engine.cpp`,
antes de llamar a Gemini.

---

## ✅ Sensores disponibles en hardware (sin agregar nada)

| Sensor | I2C Addr | Datos disponibles |
|---|---|---|
| **SHTC3** | 0x70 | Temperatura (°C), Humedad relativa (%) |
| **PCF85063** RTC | 0x51 | Fecha y hora con batería de respaldo |
| **ADC GPIO 1** | — | Voltaje de batería LiPo (divisor resistivo) |

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN

```
Retomamos el proyecto Copito AI (ESP32-S3-ePaper-1.54).
Lee primero el workflow en docs/WORKFLOW_AGENTE.md.
Hoy implementaremos la Sesión 08: Sensor SHTC3 y reloj NTP/RTC en el idle display.
Quiero que Copito muestre temperatura, humedad y hora en el e-paper cuando está en reposo,
y que pueda responder preguntas de temperatura/hora sin llamar a Gemini.
El sensor SHTC3 está en I2C addr 0x70 (SDA=47, SCL=48).
```
